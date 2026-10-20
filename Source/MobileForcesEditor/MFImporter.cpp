#include "MFImporter.h"
#include "MobileForcesEditor.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionTextureSampleParameterCube.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Engine/Font.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWave.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* ImportRoot = TEXT("/Game/MFImported");

	TSharedPtr<FJsonObject> LoadJson(const FString& File)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			UE_LOG(LogMFEditor, Error, TEXT("Can't read %s"), *File);
			return nullptr;
		}
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
		{
			UE_LOG(LogMFEditor, Error, TEXT("Bad JSON in %s"), *File);
			return nullptr;
		}
		return Obj;
	}

	/** "rage_warehouse.metal.ware_metal_29" -> ("rage_warehouse/metal", "ware_metal_29") */
	void SplitKey(const FString& Key, FString& OutDir, FString& OutName)
	{
		TArray<FString> Parts;
		Key.ParseIntoArray(Parts, TEXT("."));
		OutName = Parts.Num() ? Parts.Pop() : Key;
		OutDir = FString::Join(Parts, TEXT("/"));
	}

	template <typename T>
	T* FindOrCreateAsset(const FString& PackagePath, const FString& Name, bool& bOutCreated)
	{
		const FString Full = PackagePath / Name;
		T* Existing = LoadObject<T>(nullptr, *(Full + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		bOutCreated = Existing == nullptr;
		if (Existing)
		{
			return Existing;
		}
		UPackage* Pkg = CreatePackage(*Full);
		return NewObject<T>(Pkg, *Name, RF_Public | RF_Standalone);
	}
}

FMFImporter::FMFImporter(const FString& InWorkDir)
	: WorkDir(InWorkDir)
{
}

bool FMFImporter::SaveAsset(UObject* Asset)
{
	if (!Asset)
	{
		return false;
	}
	UPackage* Pkg = Asset->GetOutermost();
	Pkg->MarkPackageDirty();
	const FString Ext = Asset->IsA<UWorld>() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
	const FString File = FPackageName::LongPackageNameToFilename(Pkg->GetName(), Ext);
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.Error = GWarn;   // report problems as warnings (GError would be fatal)
	const bool bOk = UPackage::SavePackage(Pkg, Asset, *File, Args);
	if (!bOk)
	{
		UE_LOG(LogMFEditor, Error, TEXT("Failed to save %s"), *File);
	}
	return bOk;
}

FString FMFImporter::TextureAssetPath(const FString& Key)
{
	FString Dir, Name;
	SplitKey(Key, Dir, Name);
	return FString::Printf(TEXT("%s/Textures/%s/%s.%s"), ImportRoot, *Dir, *Name, *Name);
}

UTexture2D* FMFImporter::FindTexture(const FString& Key) const
{
	return LoadObject<UTexture2D>(nullptr, *TextureAssetPath(Key), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

USoundWave* FMFImporter::FindSound(const FString& MFPath) const
{
	FString Pkg, Name;
	if (!MFPath.Split(TEXT("."), &Pkg, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
	{
		return nullptr;
	}
	FString Package = Pkg;
	Package.Split(TEXT("."), &Package, nullptr);   // drop a group: "levelSFX.Water.Drip" -> levelSFX
	if (Package.IsEmpty())
	{
		Package = Pkg;
	}
	const FString Path = FString::Printf(TEXT("%s/Sounds/%s/%s.%s"), ImportRoot, *Package, *Name, *Name);
	return LoadObject<USoundWave>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

// --- textures --------------------------------------------------------------------------

bool FMFImporter::ImportTextures()
{
	TSharedPtr<FJsonObject> Manifest = LoadJson(WorkDir / TEXT("textures.json"));
	if (!Manifest)
	{
		return false;
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UAssetImportTask*> Tasks;
	TArray<TPair<FString, bool>> Keys;   // key, has alpha
	for (const auto& Pair : Manifest->Values)
	{
		const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
		FString Dir, Name;
		const FString Key(Pair.Key);
		SplitKey(Key, Dir, Name);
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = FPaths::ConvertRelativePathToFull(WorkDir / Info->GetStringField(TEXT("file")));
		Task->DestinationPath = FString::Printf(TEXT("%s/Textures/%s"), ImportRoot, *Dir);
		Task->DestinationName = Name;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;
		Tasks.Add(Task);
		Keys.Emplace(Key, Info->GetBoolField(TEXT("alpha")));
	}
	UE_LOG(LogMFEditor, Display, TEXT("Importing %d textures"), Tasks.Num());
	AssetTools.ImportAssetTasks(Tasks);

	int32 Ok = 0;
	for (int32 i = 0; i < Tasks.Num(); ++i)
	{
		UTexture2D* Tex = FindTexture(Keys[i].Key);
		if (!Tex)
		{
			UE_LOG(LogMFEditor, Warning, TEXT("Texture import failed: %s"), *Keys[i].Key);
			continue;
		}
		// Mobile-friendly: world group, never streamed (they're tiny), no alpha channel unless masked
		Tex->LODGroup = TEXTUREGROUP_World;
		Tex->NeverStream = true;
		Tex->SRGB = true;
		Tex->CompressionSettings = TC_Default;
		Tex->CompressionNoAlpha = !Keys[i].Value;
		Tex->MipGenSettings = TMGS_FromTextureGroup;
		if (IsUITexture(Keys[i].Key))
		{
			// HUD art and bitmap fonts: uncompressed, never mipped; fonts point sampled (crisp
			// glyphs), the art filtered like MF's canvas (soft palette-alpha glows when scaled)
			Tex->LODGroup = TEXTUREGROUP_UI;
			Tex->CompressionSettings = TC_EditorIcon;
			Tex->MipGenSettings = TMGS_NoMipmaps;
			Tex->Filter = Keys[i].Key.StartsWith(TEXT("MobileForceFonts."), ESearchCase::IgnoreCase) ? TF_Nearest : TF_Bilinear;
		}
		Tex->PostEditChange();
		Ok += SaveAsset(Tex) ? 1 : 0;
	}
	UE_LOG(LogMFEditor, Display, TEXT("Textures: %d / %d imported"), Ok, Tasks.Num());
	ImportFonts();
	return Ok > 0;
}

bool FMFImporter::IsUITexture(const FString& Key)
{
	return Key.StartsWith(TEXT("Rage."), ESearchCase::IgnoreCase) || Key.StartsWith(TEXT("RageWeapons."), ESearchCase::IgnoreCase)
		|| Key.StartsWith(TEXT("MobileForceFonts."), ESearchCase::IgnoreCase) || Key.Contains(TEXT("minimap"), ESearchCase::IgnoreCase);
}

void FMFImporter::ImportFonts()
{
	// MF bitmap fonts (fonts.json from mflib/fonts.py): offline-cache UFonts over the page textures
	TSharedPtr<FJsonObject> Fonts = LoadJson(WorkDir / TEXT("fonts.json"));
	if (!Fonts)
	{
		return;
	}
	int32 Count = 0;
	for (const auto& Pair : Fonts->Values)
	{
		const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
		bool bCreated = false;
		UFont* Font = FindOrCreateAsset<UFont>(FString::Printf(TEXT("%s/Fonts"), ImportRoot), FString(Pair.Key), bCreated);
		Font->FontCacheType = EFontCacheType::Offline;
		Font->Textures.Reset();
		Font->Characters.Reset();
		Font->IsRemapped = 0;
		Font->Kerning = 0;
		Font->ScalingFactor = 1.f;
		const int32 PerPage = Info->GetIntegerField(TEXT("chars_per_page"));
		const TArray<TSharedPtr<FJsonValue>>& Pages = Info->GetArrayField(TEXT("pages"));
		for (int32 PageIndex = 0; PageIndex < Pages.Num(); ++PageIndex)
		{
			const TSharedPtr<FJsonObject> Page = Pages[PageIndex]->AsObject();
			Font->Textures.Add(FindTexture(Page->GetStringField(TEXT("texture"))));
			const TArray<TSharedPtr<FJsonValue>>& Chars = Page->GetArrayField(TEXT("chars"));
			for (int32 i = 0; i < PerPage; ++i)
			{
				FFontCharacter Ch;
				if (Chars.IsValidIndex(i))
				{
					const TArray<TSharedPtr<FJsonValue>>& R = Chars[i]->AsArray();
					Ch.StartU = R[0]->AsNumber();
					Ch.StartV = R[1]->AsNumber();
					Ch.USize = R[2]->AsNumber();
					Ch.VSize = R[3]->AsNumber();
				}
				Ch.TextureIndex = PageIndex;
				Ch.VerticalOffset = 0;
				Font->Characters.Add(Ch);
			}
		}
		Font->CacheCharacterCountAndMaxCharHeight();
		Font->PostEditChange();
		Count += SaveAsset(Font) ? 1 : 0;
	}
	UE_LOG(LogMFEditor, Display, TEXT("Fonts: %d imported"), Count);
}

// --- sounds ----------------------------------------------------------------------------

bool FMFImporter::ImportSounds(const FString& SubDir, bool bLooping)
{
	const FString SoundDir = WorkDir / SubDir;
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *SoundDir, TEXT("*.wav"), true, false);
	if (Files.Num() == 0)
	{
		UE_LOG(LogMFEditor, Warning, TEXT("No sounds in %s"), *SoundDir);
		return false;
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UAssetImportTask*> Tasks;
	for (const FString& File : Files)
	{
		FString Rel = File;
		FPaths::MakePathRelativeTo(Rel, *(SoundDir + TEXT("/")));
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = File;
		Task->DestinationPath = FString::Printf(TEXT("%s/%s/%s"), ImportRoot, *SubDir, *FPaths::GetPath(Rel));
		Task->DestinationName = FPaths::GetBaseFilename(Rel);
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = !bLooping;
		Tasks.Add(Task);
	}
	UE_LOG(LogMFEditor, Display, TEXT("Importing %d files from %s"), Tasks.Num(), *SubDir);
	AssetTools.ImportAssetTasks(Tasks);
	if (bLooping)
	{
		for (UAssetImportTask* Task : Tasks)
		{
			for (UObject* Obj : Task->GetObjects())
			{
				if (USoundWave* Wave = Cast<USoundWave>(Obj))
				{
					Wave->bLooping = true;
					SaveAsset(Wave);
				}
			}
		}
	}
	return true;
}

// --- mod plugin art --------------------------------------------------------------------

bool FMFImporter::ImportPluginArt()
{
	const FString Root = WorkDir / TEXT("Plugins");
	TArray<FString> Plugins;
	IFileManager::Get().FindFiles(Plugins, *(Root / TEXT("*")), false, true);
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	for (const FString& Plugin : Plugins)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *(Root / Plugin), TEXT("*.*"), true, false);
		TArray<UAssetImportTask*> Tasks;
		for (const FString& File : Files)
		{
			FString Rel = File;
			FPaths::MakePathRelativeTo(Rel, *(Root / Plugin + TEXT("/")));
			UAssetImportTask* Task = NewObject<UAssetImportTask>();
			Task->Filename = File;
			Task->DestinationPath = FString::Printf(TEXT("/%s/Generated/%s"), *Plugin, *FPaths::GetPath(Rel));
			Task->DestinationName = FPaths::GetBaseFilename(Rel);
			Task->bAutomated = true;
			Task->bReplaceExisting = true;
			Task->bSave = true;
			Tasks.Add(Task);
		}
		UE_LOG(LogMFEditor, Display, TEXT("Plugin %s: importing %d files"), *Plugin, Tasks.Num());
		AssetTools.ImportAssetTasks(Tasks);
	}
	return true;
}

// --- materials -------------------------------------------------------------------------

UMaterialInterface* FMFImporter::EnsureSkyMaterial()
{
	if (UMaterialInterface** Found = MasterCache.Find(TEXT("Sky")))
	{
		return *Found;
	}
	bool bNewRT = false;
	UTextureRenderTargetCube* RT = FindOrCreateAsset<UTextureRenderTargetCube>(TEXT("/Game/MF/Materials"), TEXT("RT_MF_Sky"), bNewRT);
	if (bNewRT)
	{
		RT->Init(256, PF_B8G8R8A8);   // MF skies are 256 px textures; LDR is plenty
		RT->ClearColor = FLinearColor::Black;
		SaveAsset(RT);
	}
	bool bCreated = false;
	UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_Sky"), bCreated);
	if (bCreated)
	{
		// emissive = sky cube sampled along the view ray (camera -> pixel)
		UMaterialExpressionCameraVectorWS* Cam = NewObject<UMaterialExpressionCameraVectorWS>(M);
		M->GetExpressionCollection().AddExpression(Cam);
		UMaterialExpressionMultiply* Neg = NewObject<UMaterialExpressionMultiply>(M);
		Neg->A.Connect(0, Cam);
		Neg->ConstB = -1.f;
		M->GetExpressionCollection().AddExpression(Neg);
		UMaterialExpressionTextureSampleParameterCube* Sky = NewObject<UMaterialExpressionTextureSampleParameterCube>(M);
		Sky->ParameterName = TEXT("Sky");
		Sky->Texture = RT;
		Sky->AutoSetSampleType();
		Sky->Coordinates.Connect(0, Neg);
		M->GetExpressionCollection().AddExpression(Sky);
		M->SetShadingModel(MSM_Unlit);
		M->SetMaterialUsage(MATUSAGE_StaticLighting);
		M->GetEditorOnlyData()->EmissiveColor.Connect(0, Sky);
		M->PreEditChange(nullptr);
		M->PostEditChange();
		SaveAsset(M);
		UE_LOG(LogMFEditor, Display, TEXT("Created M_MF_Sky"));
	}
	MasterCache.Add(TEXT("Sky"), M);
	return M;
}

void FMFImporter::FixMasterMaterialUsage()
{
	for (const TCHAR* Mode : { TEXT("Opaque"), TEXT("Masked"), TEXT("MaskedUnlit"), TEXT("Unlit"), TEXT("Additive"),
		TEXT("Modulated"), TEXT("TeamTint") })
	{
		const FString Path = FString::Printf(TEXT("/Game/MF/Materials/M_MF_%s.M_MF_%s"), Mode, Mode);
		UMaterial* M = LoadObject<UMaterial>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (M && !M->GetUsageByFlag(MATUSAGE_StaticLighting))
		{
			M->SetMaterialUsage(MATUSAGE_StaticLighting);
			M->PostEditChange();
			SaveAsset(M);
			UE_LOG(LogMFEditor, Display, TEXT("%s: static lighting usage set"), *M->GetName());
		}
	}
}

void FMFImporter::EnsureTeamTintMaterial()
{
	bool bCreated = false;
	UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_TeamTint"), bCreated);
	if (!bCreated)
	{
		return;
	}
	UMaterialExpressionVectorParameter* Color = NewObject<UMaterialExpressionVectorParameter>(M);
	Color->ParameterName = TEXT("Color");
	Color->DefaultValue = FLinearColor(0.7f, 0.7f, 0.7f);
	Color->bUseCustomPrimitiveData = true;
	Color->PrimitiveDataIndex = 0;
	M->GetExpressionCollection().AddExpression(Color);
	M->SetShadingModel(MSM_Unlit);
	M->SetMaterialUsage(MATUSAGE_StaticLighting);
	M->GetEditorOnlyData()->EmissiveColor.Connect(0, Color);
	M->PreEditChange(nullptr);
	M->PostEditChange();
	SaveAsset(M);
	UE_LOG(LogMFEditor, Display, TEXT("Created M_MF_TeamTint"));
}

UMaterialInterface* FMFImporter::GetMasterMaterial(const FString& Mode)
{
	if (UMaterialInterface** Found = MasterCache.Find(Mode))
	{
		return *Found;
	}
	bool bCreated = false;
	UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_") + Mode, bCreated);
	if (bCreated)
	{
		// UE1 look: base texture, no specular, fully rough. Blend modes follow UE1 poly flags:
		// Masked = alpha test, Additive = PF_Translucent, Modulated = PF_Modulated (x2), Unlit = PF_Unlit.
		UMaterialEditorOnlyData* E = M->GetEditorOnlyData();
		UMaterialExpressionTextureSampleParameter2D* Tex = NewObject<UMaterialExpressionTextureSampleParameter2D>(M);
		Tex->ParameterName = TEXT("Texture");
		Tex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		M->GetExpressionCollection().AddExpression(Tex);
		auto Const = [M](float V)
		{
			UMaterialExpressionConstant* C = NewObject<UMaterialExpressionConstant>(M);
			C->R = V;
			M->GetExpressionCollection().AddExpression(C);
			return C;
		};
		const bool bLit = Mode == TEXT("Opaque") || Mode == TEXT("Masked");
		if (bLit)
		{
			M->SetShadingModel(MSM_DefaultLit);
			E->BaseColor.Connect(0, Tex);
			E->Specular.Connect(0, Const(0.f));
			E->Roughness.Connect(0, Const(1.f));
		}
		else
		{
			M->SetShadingModel(MSM_Unlit);
			if (Mode == TEXT("Modulated"))
			{
				UMaterialExpressionMultiply* Mul = NewObject<UMaterialExpressionMultiply>(M);
				Mul->A.Connect(0, Tex);
				Mul->ConstB = 2.f;
				M->GetExpressionCollection().AddExpression(Mul);
				E->EmissiveColor.Connect(0, Mul);
			}
			else
			{
				E->EmissiveColor.Connect(0, Tex);
			}
		}
		if (Mode == TEXT("Masked") || Mode == TEXT("MaskedUnlit"))
		{
			M->BlendMode = BLEND_Masked;
			E->OpacityMask.Connect(4, Tex);   // alpha output
		}
		else if (Mode == TEXT("Additive"))
		{
			M->BlendMode = BLEND_Additive;
		}
		else if (Mode == TEXT("Modulated"))
		{
			M->BlendMode = BLEND_Modulate;
		}
		M->SetMaterialUsage(MATUSAGE_StaticLighting);   // lightmapped level geometry
		M->PreEditChange(nullptr);
		M->PostEditChange();
		SaveAsset(M);
		UE_LOG(LogMFEditor, Display, TEXT("Created master material M_MF_%s"), *Mode);
	}
	if ((Mode == TEXT("Opaque") || Mode == TEXT("Masked")) && AddAmbientFloor(M))
	{
		UE_LOG(LogMFEditor, Display, TEXT("M_MF_%s: ambient floor from MPC_MF"), *Mode);
	}
	if (const FMaterialResource* Res = M->GetMaterialResource(GMaxRHIShaderPlatform))
	{
		const_cast<FMaterialResource*>(Res)->FinishCompilation();
		for (const FString& Err : Res->GetCompileErrors())
		{
			UE_LOG(LogMFEditor, Error, TEXT("M_MF_%s: %s"), *Mode, *Err);
		}
	}
	MasterCache.Add(Mode, M);
	return M;
}

UMaterialParameterCollection* FMFImporter::EnsureAmbientCollection()
{
	bool bCreated = false;
	UMaterialParameterCollection* C = FindOrCreateAsset<UMaterialParameterCollection>(TEXT("/Game/MF/Materials"), TEXT("MPC_MF"), bCreated);
	if (!C->VectorParameters.ContainsByPredicate([](const FCollectionVectorParameter& P) { return P.ParameterName == TEXT("Ambient"); }))
	{
		FCollectionVectorParameter P;
		P.ParameterName = TEXT("Ambient");
		P.DefaultValue = FLinearColor::Black;
		C->VectorParameters.Add(P);
		C->PostEditChange();
		SaveAsset(C);
	}
	return C;
}

bool FMFImporter::AddAmbientFloor(UMaterial* M)
{
	// UE1 zone ambient: unshadowed light on every surface. Now mostly baked (sky colour + bounce
	// light in Lightmass); this adds the map's small ambient floor on top, one global value
	// (no per-zone meshes or per-component data). Lightmass ignores it (EmissiveBoost 0).
	UMaterialParameterCollection* Collection = EnsureAmbientCollection();
	UMaterialExpressionVectorParameter* OldCPD = nullptr;
	for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
	{
		if (const UMaterialExpressionCollectionParameter* P = Cast<UMaterialExpressionCollectionParameter>(E); P && P->ParameterName == TEXT("Ambient"))
		{
			// the collection's vectors are float4: multiply the RGB only
			for (const TObjectPtr<UMaterialExpression>& E2 : M->GetExpressions())
			{
				if (UMaterialExpressionMultiply* Mul = Cast<UMaterialExpressionMultiply>(E2); Mul && Mul->B.Expression == P && !Mul->B.Mask)
				{
					Mul->B.SetMask(1, 1, 1, 1, 0);
					M->PreEditChange(nullptr);
					M->PostEditChange();
					SaveAsset(M);
					return true;
				}
			}
			return false;
		}
		if (UMaterialExpressionVectorParameter* P = Cast<UMaterialExpressionVectorParameter>(E); P && P->ParameterName == TEXT("Ambient"))
		{
			OldCPD = P;   // the earlier per-component version
		}
	}
	UMaterialEditorOnlyData* Data = M->GetEditorOnlyData();
	UMaterialExpression* BaseColor = Data->BaseColor.Expression;
	if (!BaseColor)
	{
		return false;
	}
	if (OldCPD)
	{
		TArray<UMaterialExpression*> Remove{ OldCPD };
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			if (UMaterialExpressionMultiply* Mul = Cast<UMaterialExpressionMultiply>(E); Mul && Mul->B.Expression == OldCPD)
			{
				Remove.Add(Mul);
			}
		}
		for (UMaterialExpression* E : Remove)
		{
			M->GetExpressionCollection().RemoveExpression(E);
		}
	}
	UMaterialExpressionCollectionParameter* Amb = NewObject<UMaterialExpressionCollectionParameter>(M);
	Amb->Collection = Collection;
	Amb->ParameterName = TEXT("Ambient");
	Amb->ParameterId = Collection->GetParameterId(TEXT("Ambient"));
	M->GetExpressionCollection().AddExpression(Amb);
	UMaterialExpressionMultiply* Mul = NewObject<UMaterialExpressionMultiply>(M);
	Mul->A.Connect(Data->BaseColor.OutputIndex, BaseColor);
	Mul->B.Connect(0, Amb);
	Mul->B.SetMask(1, 1, 1, 1, 0);   // the collection's vectors are float4
	M->GetExpressionCollection().AddExpression(Mul);
	Data->EmissiveColor.Connect(0, Mul);
	M->PreEditChange(nullptr);
	M->PostEditChange();
	SaveAsset(M);
	return true;
}

UMaterialInterface* FMFImporter::GetMaterial(const FString& Key)
{
	if (UMaterialInterface** Found = MaterialCache.Find(Key))
	{
		return *Found;
	}
	FString TexKey, Mode;
	if (!Key.Split(TEXT("|"), &TexKey, &Mode))
	{
		TexKey = Key;
		Mode = TEXT("opaque");
	}
	if (Mode == TEXT("collision"))
	{
		MaterialCache.Add(Key, nullptr);
		return nullptr;
	}
	const bool bTwoSided = Mode.Contains(TEXT("_2s"));
	const bool bUnlit = Mode.Contains(TEXT("_unlit"));
	FString Base;
	Mode.Split(TEXT("_"), &Base, nullptr);
	if (Base.IsEmpty())
	{
		Base = Mode;
	}
	if (Base == TEXT("sky"))
	{
		// UE1 never drew these polygons' own texture: the sky zone shows through them
		UMaterialInterface* M = EnsureSkyMaterial();
		MaterialCache.Add(Key, M);
		return M;
	}
	FString Master = TEXT("Opaque");
	if (Base == TEXT("additive"))       Master = TEXT("Additive");
	else if (Base == TEXT("modulated")) Master = TEXT("Modulated");
	else if (Base == TEXT("masked"))    Master = bUnlit ? TEXT("MaskedUnlit") : TEXT("Masked");
	else if (bUnlit)                    Master = TEXT("Unlit");

	FString Dir, Name;
	SplitKey(TexKey, Dir, Name);
	if (Dir.IsEmpty() || Dir.StartsWith(TEXT("Engine")))
	{
		// untextured surfaces: the master itself (engine default texture)
		UMaterialInterface* M = GetMasterMaterial(Master);
		MaterialCache.Add(Key, M);
		return M;
	}
	UTexture2D* Tex = FindTexture(TexKey);
	const FString MIName = FString::Printf(TEXT("MI_%s_%s"), *Name, *Mode);
	bool bCreated = false;
	UMaterialInstanceConstant* MIC = FindOrCreateAsset<UMaterialInstanceConstant>(
		FString::Printf(TEXT("%s/Materials/%s"), ImportRoot, *Dir), MIName, bCreated);
	MIC->SetParentEditorOnly(GetMasterMaterial(Master));
	if (Tex)
	{
		MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Texture")), Tex);
	}
	else
	{
		UE_LOG(LogMFEditor, Warning, TEXT("Material %s: texture %s missing"), *Key, *TexKey);
	}
	if (bTwoSided)
	{
		FMaterialInstanceBasePropertyOverrides Overrides = MIC->BasePropertyOverrides;
		Overrides.bOverride_TwoSided = true;
		Overrides.TwoSided = true;
		MIC->BasePropertyOverrides = Overrides;
	}
	MIC->PostEditChange();
	SaveAsset(MIC);
	MaterialCache.Add(Key, MIC);
	return MIC;
}

// --- static meshes ---------------------------------------------------------------------

UStaticMesh* FMFImporter::BuildStaticMesh(const FString& JsonFile, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> J = LoadJson(JsonFile);
	if (!J)
	{
		return nullptr;
	}
	TArray<uint8> Bin;
	if (!FFileHelper::LoadFileToArray(Bin, *(FPaths::GetPath(JsonFile) / J->GetStringField(TEXT("bin")))))
	{
		UE_LOG(LogMFEditor, Error, TEXT("Missing mesh data for %s"), *JsonFile);
		return nullptr;
	}
	const int32 NumVerts = J->GetIntegerField(TEXT("vertex_count"));
	const int32 NumIndices = J->GetIntegerField(TEXT("index_count"));
	const bool bRender = J->GetBoolField(TEXT("render"));
	const FString Collision = J->GetStringField(TEXT("collision"));
	const int32 LightmapRes = J->GetIntegerField(TEXT("lightmap_resolution"));
	if (Bin.Num() < NumVerts * 32 + NumIndices * 4)
	{
		UE_LOG(LogMFEditor, Error, TEXT("Truncated mesh data for %s"), *JsonFile);
		return nullptr;
	}
	const float* V = reinterpret_cast<const float*>(Bin.GetData());
	const uint32* Idx = reinterpret_cast<const uint32*>(Bin.GetData() + NumVerts * 32);

	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
	UVs.SetNumChannels(1);

	// Weld positions so lightmap UV generation sees connected surfaces
	TMap<FVector3f, FVertexID> Welded;
	TArray<FVertexInstanceID> Instances;
	Instances.SetNum(NumVerts);
	for (int32 i = 0; i < NumVerts; ++i)
	{
		const float* P = V + i * 8;
		const FVector3f Pos(P[0], P[1], P[2]);
		FVertexID* VID = Welded.Find(Pos);
		if (!VID)
		{
			const FVertexID New = MD.CreateVertex();
			Positions[New] = Pos;
			VID = &Welded.Add(Pos, New);
		}
		const FVertexInstanceID VI = MD.CreateVertexInstance(*VID);
		Normals[VI] = FVector3f(P[3], P[4], P[5]);
		UVs.Set(VI, 0, FVector2f(P[6], P[7]));
		Instances[i] = VI;
	}

	const TArray<TSharedPtr<FJsonValue>>& Materials = J->GetArrayField(TEXT("materials"));
	TArray<FStaticMaterial> StaticMaterials;
	for (const TSharedPtr<FJsonValue>& Sec : J->GetArrayField(TEXT("sections")))
	{
		const TSharedPtr<FJsonObject> S = Sec->AsObject();
		const int32 MatIndex = S->GetIntegerField(TEXT("material"));
		const int32 First = S->GetIntegerField(TEXT("first_index"));
		const int32 NumTris = S->GetIntegerField(TEXT("num_triangles"));
		const FString Key = Materials.IsValidIndex(MatIndex) ? Materials[MatIndex]->AsString() : FString();
		const FName Slot(*FString::Printf(TEXT("M%d"), StaticMaterials.Num()));
		const FPolygonGroupID PG = MD.CreatePolygonGroup();
		SlotNames[PG] = Slot;
		StaticMaterials.Add(FStaticMaterial(bRender ? GetMaterial(Key) : nullptr, Slot, Slot));
		for (int32 t = 0; t < NumTris; ++t)
		{
			const uint32 A = Idx[First + t * 3], B = Idx[First + t * 3 + 1], C = Idx[First + t * 3 + 2];
			// mfmesh triangles wind cross(v1-v0, v2-v0) along the normal; UE wants the opposite
			TArray<FVertexInstanceID, TInlineAllocator<3>> Tri = { Instances[A], Instances[C], Instances[B] };
			MD.CreateTriangle(PG, Tri);
		}
	}

	FString PackagePath, Name;
	AssetPath.Split(TEXT("/"), &PackagePath, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	bool bCreated = false;
	UStaticMesh* SM = FindOrCreateAsset<UStaticMesh>(PackagePath, Name, bCreated);
	SM->PreEditChange(nullptr);
	SM->GetStaticMaterials() = StaticMaterials;
	SM->SetNumSourceModels(1);
	FStaticMeshSourceModel& Src = SM->GetSourceModel(0);
	Src.BuildSettings.bRecomputeNormals = false;
	Src.BuildSettings.bRecomputeTangents = true;
	Src.BuildSettings.bUseMikkTSpace = true;
	Src.BuildSettings.bRemoveDegenerates = true;
	Src.BuildSettings.bUseFullPrecisionUVs = true;     // world UVs tile far past 0..1
	Src.BuildSettings.bGenerateLightmapUVs = bRender;
	Src.BuildSettings.SrcLightmapIndex = 0;
	Src.BuildSettings.DstLightmapIndex = 1;
	Src.BuildSettings.MinLightmapResolution = FMath::Max(16, LightmapRes);
	Src.BuildSettings.bBuildReversedIndexBuffer = false;
	SM->CreateMeshDescription(0, MoveTemp(MD));
	SM->CommitMeshDescription(0);
	SM->SetLightMapResolution(bRender ? FMath::Max(16, LightmapRes) : 4);
	SM->SetLightMapCoordinateIndex(bRender ? 1 : 0);
	SM->CreateBodySetup();
	UBodySetup* Body = SM->GetBodySetup();
	Body->CollisionTraceFlag = Collision == TEXT("complex") ? CTF_UseComplexAsSimple : CTF_UseDefault;
	if (Collision == TEXT("none"))
	{
		Body->bNeverNeedsCookedCollisionData = true;
	}
	SM->Build(true);
	SM->PostEditChange();
	SaveAsset(SM);
	return SM;
}

UStaticMesh* FMFImporter::LoadOrBuildMesh(const FString& MapDir, const FString& MeshRel, const FString& MapName)
{
	const FString CacheKey = MapName + TEXT(":") + MeshRel;
	if (UStaticMesh** Found = MeshCache.Find(CacheKey))
	{
		return *Found;
	}
	FString Sub, Name;
	MeshRel.Split(TEXT("/"), &Sub, &Name);
	const FString Folder = Sub == TEXT("geo") ? TEXT("Geo") : TEXT("Meshes");
	const FString AssetPath = FString::Printf(TEXT("%s/Maps/%s/%s/%s"), ImportRoot, *MapName, *Folder, *Name);
	UStaticMesh* SM = BuildStaticMesh(MapDir / (MeshRel + TEXT(".mfmesh.json")), AssetPath);
	MeshCache.Add(CacheKey, SM);
	return SM;
}
