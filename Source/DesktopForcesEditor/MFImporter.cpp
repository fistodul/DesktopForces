#include "MFImporter.h"
#include "Sound/SoundAttenuation.h"
#include "DesktopForcesEditor.h"
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
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
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
#include "AssetRegistry/AssetRegistryModule.h"
#include "Sound/SoundWave.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{

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

	/** UE1 names may hold characters UE package / object names can't ("ap_P&Hd", "chest,box"). */
	FString SafeName(const FString& In)
	{
		FString Out = In;
		for (TCHAR& Ch : Out)
		{
			if (FCString::Strchr(TEXT("\\:*?\"<>|' ,.&!~@#(){}[]=;^%$`"), Ch))
			{
				Ch = TEXT('_');
			}
		}
		return Out;
	}

	/** "rage_warehouse.metal.ware_metal_29" -> ("rage_warehouse/metal", "ware_metal_29") */
	void SplitKey(const FString& Key, FString& OutDir, FString& OutName)
	{
		TArray<FString> Parts;
		Key.ParseIntoArray(Parts, TEXT("."));
		OutName = SafeName(Parts.Num() ? Parts.Pop() : Key);
		for (FString& P : Parts)
		{
			P = SafeName(P);
		}
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

FMFPacks FMFImporter::Packs;

void FMFPacks::Load(const FString& File)
{
	TSharedPtr<FJsonObject> J = LoadJson(File);
	if (!J)
	{
		UE_LOG(LogMFEditor, Warning, TEXT("No %s: everything goes to %s"), *File, *ClassicRoot);
		return;
	}
	J->TryGetStringField(TEXT("classic_root"), ClassicRoot);
	J->TryGetStringField(TEXT("core_root"), CoreRoot);
	J->TryGetStringField(TEXT("core_maps_root"), CoreMapsRoot);
	J->TryGetStringField(TEXT("replaced_root"), ReplacedRoot);
	J->TryGetBoolField(TEXT("remaster_classic"), bRemasterClassic);
	CoreMaps.Reset();
	CorePackages.Reset();
	Replaced.Reset();
	if (const TSharedPtr<FJsonObject>* Maps; J->TryGetObjectField(TEXT("maps"), Maps))
	{
		for (const auto& Pair : (*Maps)->Values)
		{
			if (Pair.Value->AsString() == TEXT("core"))
			{
				CoreMaps.Add(FString(Pair.Key).ToLower());
			}
		}
	}
	for (const TSharedPtr<FJsonValue>& V : J->GetArrayField(TEXT("core_packages")))
	{
		CorePackages.Add(V->AsString().ToLower());
	}
	for (const TSharedPtr<FJsonValue>& V : J->GetArrayField(TEXT("replaced")))
	{
		Replaced.Add(V->AsString().ToLower());
	}
	UE_LOG(LogMFEditor, Display, TEXT("Packs: %d core maps, %d core packages, %d replaced textures"), CoreMaps.Num(), CorePackages.Num(), Replaced.Num());
}

FString FMFPacks::PackageRoot(const FString& Package) const
{
	return CorePackages.Contains(Package.ToLower()) ? CoreRoot : ClassicRoot;
}

FString FMFPacks::TextureRoot(const FString& Key) const
{
	FString Package;
	return PackageRoot(Key.Split(TEXT("."), &Package, nullptr) ? Package : Key);
}

FString FMFPacks::MapPackage(const FString& Map) const
{
	return (CoreMaps.Contains(Map.ToLower()) ? CoreMapsRoot : ClassicRoot / TEXT("Maps")) / Map;
}

FMFImporter::FMFImporter(const FString& InWorkDir)
	: WorkDir(InWorkDir)
{
	Packs.Load(WorkDir / TEXT("packs.json"));
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
	return FString::Printf(TEXT("%s/Textures/%s/%s.%s"), *Packs.TextureRoot(Key), *Dir, *Name, *Name);
}

FString FMFImporter::ReplacementAssetPath(const FString& Key)
{
	FString Dir, Name;
	SplitKey(Key, Dir, Name);
	return FString::Printf(TEXT("%s/Textures/%s/%s.%s"), *Packs.ReplacedRoot, *Dir, *Name, *Name);
}

UTexture2D* FMFImporter::FindTexture(const FString& Key) const
{
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *TextureAssetPath(Key), nullptr, LOAD_NoWarn | LOAD_Quiet);
	return Tex || !Packs.HasReplacement(Key) ? Tex : LoadObject<UTexture2D>(nullptr, *ReplacementAssetPath(Key), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

UTexture2D* FMFImporter::FindTextureFor(const FString& Key, bool bCoreMap) const
{
	if ((bCoreMap || Packs.bRemasterClassic) && Packs.HasReplacement(Key))
	{
		if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *ReplacementAssetPath(Key), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Tex;
		}
	}
	return FindTexture(Key);
}

void FMFImporter::RepointCoreMaterials()
{
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AR.ScanPathsSynchronous({ Packs.CoreRoot / TEXT("Materials") }, true);
	int32 Count = 0;
	for (const FString& Key : Packs.Replaced)
	{
		FString Dir, Name;
		SplitKey(Key, Dir, Name);
		TArray<FAssetData> Assets;
		AR.GetAssetsByPath(FName(*(Packs.CoreRoot / TEXT("Materials") / Dir)), Assets, false);
		UTexture2D* Tex = FindTextureFor(Key, true);
		for (const FAssetData& A : Assets)
		{
			if (!Tex || !A.AssetName.ToString().StartsWith(TEXT("MI_") + Name + TEXT("_"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(A.GetAsset()))
			{
				MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Texture")), Tex);
				MIC->PostEditChange();
				Count += SaveAsset(MIC) ? 1 : 0;
			}
		}
	}
	UE_LOG(LogMFEditor, Display, TEXT("Replacement art: %d core material instances point at it"), Count);
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
	const FString Safe = SafeName(Name);
	const FString Path = FString::Printf(TEXT("%s/Sounds/%s/%s.%s"), *Packs.PackageRoot(Package), *Package, *Safe, *Safe);
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
	struct FTexKey { FString Key; bool bAlpha; bool bUI; bool bReplacement; };
	TArray<FTexKey> Keys;
	for (const auto& Pair : Manifest->Values)
	{
		const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
		FString Dir, Name;
		const FString Key(Pair.Key);
		SplitKey(Key, Dir, Name);
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = FPaths::ConvertRelativePathToFull(WorkDir / Info->GetStringField(TEXT("file")));
		Task->DestinationPath = FString::Printf(TEXT("%s/Textures/%s"), *Packs.TextureRoot(Key), *Dir);
		Task->DestinationName = Name;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;
		Tasks.Add(Task);
		bool bUI = false;
		if (!Info->TryGetBoolField(TEXT("ui"), bUI))
		{
			bUI = IsUITexture(Key);   // manifests from before the "ui" flag
		}
		Keys.Add({ Key, Info->GetBoolField(TEXT("alpha")), bUI, false });
		FString Replacement;
		if (Info->TryGetStringField(TEXT("replacement"), Replacement))
		{
			UAssetImportTask* Rep = NewObject<UAssetImportTask>();
			Rep->Filename = FPaths::ConvertRelativePathToFull(WorkDir / Replacement);
			Rep->DestinationPath = FString::Printf(TEXT("%s/Textures/%s"), *Packs.ReplacedRoot, *Dir);
			Rep->DestinationName = Name;
			Rep->bAutomated = true;
			Rep->bReplaceExisting = true;
			Rep->bSave = false;
			Tasks.Add(Rep);
			Keys.Add({ Key, Info->GetBoolField(TEXT("alpha")), bUI, true });
		}
	}
	UE_LOG(LogMFEditor, Display, TEXT("Importing %d textures"), Tasks.Num());
	AssetTools.ImportAssetTasks(Tasks);

	int32 Ok = 0;
	for (int32 i = 0; i < Tasks.Num(); ++i)
	{
		const FString Path = Keys[i].bReplacement ? ReplacementAssetPath(Keys[i].Key) : TextureAssetPath(Keys[i].Key);
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
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
		Tex->CompressionNoAlpha = !Keys[i].bAlpha;
		Tex->Filter = TF_Default;
		Tex->MipGenSettings = TMGS_FromTextureGroup;
		if (Keys[i].bUI)
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
	RepointCoreMaterials();
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
		UFont* Font = FindOrCreateAsset<UFont>(Packs.ClassicRoot / TEXT("Fonts"), FString(Pair.Key), bCreated);
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

USoundAttenuation* FMFImporter::EnsureSoundAttenuation()
{
	// UE1 PlaySound: Radius 0 -> TransientSoundRadius 0 -> the engine's 1600 uu; full volume close
	// by, falling off linearly to nothing at the radius
	const TCHAR* Path = TEXT("/Game/MF/Audio/ATT_MF");
	if (USoundAttenuation* Existing = LoadObject<USoundAttenuation>(nullptr, *FString::Printf(TEXT("%s.ATT_MF"), Path), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Existing;
	}
	UPackage* Pkg = CreatePackage(Path);
	USoundAttenuation* A = NewObject<USoundAttenuation>(Pkg, TEXT("ATT_MF"), RF_Public | RF_Standalone);
	A->Attenuation.bAttenuate = true;
	A->Attenuation.bSpatialize = true;
	A->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
	A->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	A->Attenuation.AttenuationShapeExtents = FVector(225.f, 0.f, 0.f);   // 100 uu at full volume
	A->Attenuation.FalloffDistance = 3600.f - 225.f;                     // silent at 1600 uu
	FAssetRegistryModule::AssetCreated(A);
	SaveAsset(A);
	return A;
}

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
		FString Package = Rel;
		Package.Split(TEXT("/"), &Package, nullptr);   // <package>/<name>.wav
		Task->DestinationPath = FString::Printf(TEXT("%s/%s/%s"), *Packs.PackageRoot(Package), *SubDir, *FPaths::GetPath(Rel));
		Task->DestinationName = SafeName(FPaths::GetBaseFilename(Rel));
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = !bLooping;
		Tasks.Add(Task);
	}
	UE_LOG(LogMFEditor, Display, TEXT("Importing %d files from %s"), Tasks.Num(), *SubDir);
	AssetTools.ImportAssetTasks(Tasks);
	if (!bLooping)
	{
		// one-shot sounds fade out with distance like MF's (PlaySound without a radius: 1600 uu)
		USoundAttenuation* Attenuation = EnsureSoundAttenuation();
		for (UAssetImportTask* Task : Tasks)
		{
			for (UObject* Obj : Task->GetObjects())
			{
				if (USoundWave* Wave = Cast<USoundWave>(Obj))
				{
					// sounds MF plays as an actor's AmbientSound loop: vehicle engines, skids, fires
					const FString Name = Wave->GetName();
					const bool bLoop = Name.Equals(TEXT("Engine1"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("Engine"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("skid"), ESearchCase::IgnoreCase)
						|| Name.EndsWith(TEXT("Loop"), ESearchCase::IgnoreCase) || Name.EndsWith(TEXT("EngineRunning"), ESearchCase::IgnoreCase);
					if (Wave->AttenuationSettings != Attenuation || Wave->bLooping != bLoop)
					{
						Wave->AttenuationSettings = Attenuation;
						Wave->bLooping = bLoop;
						SaveAsset(Wave);
					}
				}
			}
		}
	}
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

bool FMFImporter::ImportProps()
{
	TSharedPtr<FJsonObject> Props = LoadJson(WorkDir / TEXT("Props/props.json"));
	if (!Props)
	{
		UE_LOG(LogMFEditor, Warning, TEXT("No Props/props.json - run mfport.py props"));
		return false;
	}
	int32 Ok = 0, Total = 0;
	for (const auto& Pair : Props->GetObjectField(TEXT("meshes"))->Values)
	{
		const FString Rel = Pair.Value->AsObject()->GetStringField(TEXT("mesh"));   // Package/Name
		++Total;
		Ok += BuildStaticMesh(WorkDir / TEXT("Props") / (Rel + TEXT(".mfmesh.json")), FString::Printf(TEXT("%s/Props/%s"), *Packs.PackageRoot(FPaths::GetPath(Rel)), *Rel)) ? 1 : 0;
	}
	UE_LOG(LogMFEditor, Display, TEXT("Props: %d / %d meshes"), Ok, Total);
	return Ok > 0;
}

bool FMFImporter::ImportArt()
{
	// Tools/artgen/out/<folder>/<name>.png|wav -> /Game/MF/<folder>/<name> (artgen.py makes them:
	// generated textures and the team's SourceArt, nothing from the original game)
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/artgen/out"));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Dir, TEXT("*.*"), true, false);
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UAssetImportTask*> Tasks;
	TArray<FString> Textures;
	for (const FString& File : Files)
	{
		const FString Ext = FPaths::GetExtension(File).ToLower();
		if (Ext != TEXT("png") && Ext != TEXT("wav"))
		{
			continue;
		}
		FString Rel = FPaths::GetPath(File);
		FPaths::MakePathRelativeTo(Rel, *(Dir + TEXT("/")));
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = File;
		Task->DestinationPath = TEXT("/Game/MF") / Rel;
		Task->DestinationName = FPaths::GetBaseFilename(File);
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;
		Tasks.Add(Task);
		if (Ext == TEXT("png"))
		{
			Textures.Add(Task->DestinationPath / Task->DestinationName);
		}
	}
	UE_LOG(LogMFEditor, Display, TEXT("Art: importing %d files (Tools/artgen/out)"), Tasks.Num());
	AssetTools.ImportAssetTasks(Tasks);
	for (UAssetImportTask* Task : Tasks)
	{
		const FString Path = Task->DestinationPath / Task->DestinationName;
		UObject* Asset = LoadObject<UObject>(nullptr, *(Path + TEXT(".") + Task->DestinationName), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (UTexture2D* Tex = Cast<UTexture2D>(Asset))
		{
			// screens and icons: full size, no mips, drawn by Slate / the HUD canvas
			const bool bUI = Path.StartsWith(TEXT("/Game/MF/UI/")) || Path.Contains(TEXT("/Icons/"));
			Tex->LODGroup = bUI ? TEXTUREGROUP_UI : TEXTUREGROUP_World;   // world: 512 max, 256 on the lowest texture quality
			Tex->MipGenSettings = bUI ? TMGS_NoMipmaps : TMGS_FromTextureGroup;
			Tex->NeverStream = true;
			Tex->SRGB = true;
			Tex->CompressionSettings = Path.Contains(TEXT("/Icons/")) ? TC_EditorIcon : TC_Default;   // small icons stay crisp
			Tex->PostEditChange();
			SaveAsset(Tex);
		}
		else if (USoundWave* Wave = Cast<USoundWave>(Asset))
		{
			Wave->bLooping = false;
			Wave->PostEditChange();
			SaveAsset(Wave);
		}
	}
	EnsureCrateMaterial();
	return true;
}

void FMFImporter::EnsureCrateMaterial()
{
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/MF/Art/T_MF_Crate.T_MF_Crate"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	bool bCreated = false;
	UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_Crate"), bCreated);
	if (!bCreated)
	{
		return;
	}
	// lit wood; the texture's alpha is the stencil, painted in the team colour (custom primitive data 0-2)
	UMaterialEditorOnlyData* E = M->GetEditorOnlyData();
	UMaterialExpressionTextureSampleParameter2D* T = NewObject<UMaterialExpressionTextureSampleParameter2D>(M);
	T->ParameterName = TEXT("Texture");
	T->Texture = Tex ? Tex : LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	M->GetExpressionCollection().AddExpression(T);
	UMaterialExpressionVectorParameter* Color = NewObject<UMaterialExpressionVectorParameter>(M);
	Color->ParameterName = TEXT("Color");
	Color->DefaultValue = FLinearColor(0.9f, 0.75f, 0.2f);
	Color->bUseCustomPrimitiveData = true;
	Color->PrimitiveDataIndex = 0;
	M->GetExpressionCollection().AddExpression(Color);
	UMaterialExpressionLinearInterpolate* Lerp = NewObject<UMaterialExpressionLinearInterpolate>(M);
	Lerp->A.Connect(0, T);
	Lerp->B.Connect(0, Color);
	Lerp->B.SetMask(1, 1, 1, 1, 0);
	Lerp->Alpha.Connect(4, T);   // alpha
	M->GetExpressionCollection().AddExpression(Lerp);
	E->BaseColor.Connect(0, Lerp);
	UMaterialExpressionConstant* Zero = NewObject<UMaterialExpressionConstant>(M);
	Zero->R = 0.f;
	M->GetExpressionCollection().AddExpression(Zero);
	UMaterialExpressionConstant* One = NewObject<UMaterialExpressionConstant>(M);
	One->R = 1.f;
	M->GetExpressionCollection().AddExpression(One);
	E->Specular.Connect(0, Zero);
	E->Roughness.Connect(0, One);
	M->SetShadingModel(MSM_DefaultLit);
	M->SetMaterialUsage(MATUSAGE_StaticLighting);
	M->PreEditChange(nullptr);
	M->PostEditChange();
	SaveAsset(M);
	AddAmbientFloor(M);
	UE_LOG(LogMFEditor, Display, TEXT("Created M_MF_Crate"));
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
	bool bChanged = false;
	if (!C->VectorParameters.ContainsByPredicate([](const FCollectionVectorParameter& P) { return P.ParameterName == TEXT("Ambient"); }))
	{
		FCollectionVectorParameter P;
		P.ParameterName = TEXT("Ambient");
		P.DefaultValue = FLinearColor::Black;
		C->VectorParameters.Add(P);
		bChanged = true;
	}
	if (!C->ScalarParameters.ContainsByPredicate([](const FCollectionScalarParameter& P) { return P.ParameterName == TEXT("AmbientScale"); }))
	{
		FCollectionScalarParameter P;
		P.ParameterName = TEXT("AmbientScale");
		P.DefaultValue = 0.35f;
		C->ScalarParameters.Add(P);
		bChanged = true;
	}
	if (bChanged)
	{
		C->PostEditChange();
		SaveAsset(C);
	}
	return C;
}

bool FMFImporter::AddAmbientFloor(UMaterial* M)
{
	// UE1 zone ambient: unshadowed light on every surface. Mostly baked now (sky colour + bounce
	// light in Lightmass); this keeps a small floor on top: world geometry has its zone's ambient
	// in the vertex colour (alpha 1), props/actors (alpha 0) use the map's main ambient.
	//   Emissive = BaseColor * lerp(MPC.Ambient, VertexColor.rgb * MPC.AmbientScale, VertexColor.a)
	// No per-zone meshes, no per-component data. Lightmass ignores it (EmissiveBoost 0).
	UMaterialParameterCollection* Collection = EnsureAmbientCollection();
	UMaterialEditorOnlyData* Data = M->GetEditorOnlyData();
	TArray<UMaterialExpression*> Remove;
	for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
	{
		if (Cast<UMaterialExpressionVertexColor>(E))
		{
			return false;   // already the current version
		}
		// earlier versions: per-component vector parameter, or one global collection value
		const UMaterialExpressionVectorParameter* VP = Cast<UMaterialExpressionVectorParameter>(E);
		const UMaterialExpressionCollectionParameter* CP = Cast<UMaterialExpressionCollectionParameter>(E);
		if ((VP && VP->ParameterName == TEXT("Ambient")) || (CP && CP->ParameterName == TEXT("Ambient")))
		{
			Remove.Add(E);
			for (const TObjectPtr<UMaterialExpression>& E2 : M->GetExpressions())
			{
				if (UMaterialExpressionMultiply* Mul = Cast<UMaterialExpressionMultiply>(E2); Mul && Mul->B.Expression == E)
				{
					Remove.Add(Mul);
				}
			}
		}
	}
	UMaterialExpression* BaseColor = Data->BaseColor.Expression;
	if (!BaseColor)
	{
		return false;
	}
	for (UMaterialExpression* E : Remove)
	{
		M->GetExpressionCollection().RemoveExpression(E);
	}
	auto AddCollectionParam = [&](const TCHAR* Name)
	{
		UMaterialExpressionCollectionParameter* P = NewObject<UMaterialExpressionCollectionParameter>(M);
		P->Collection = Collection;
		P->ParameterName = Name;
		P->ParameterId = Collection->GetParameterId(Name);
		M->GetExpressionCollection().AddExpression(P);
		return P;
	};
	UMaterialExpressionCollectionParameter* MapAmb = AddCollectionParam(TEXT("Ambient"));
	UMaterialExpressionCollectionParameter* Scale = AddCollectionParam(TEXT("AmbientScale"));
	UMaterialExpressionVertexColor* VC = NewObject<UMaterialExpressionVertexColor>(M);
	M->GetExpressionCollection().AddExpression(VC);
	UMaterialExpressionMultiply* ZoneAmb = NewObject<UMaterialExpressionMultiply>(M);
	ZoneAmb->A.Connect(0, VC);                 // RGB
	ZoneAmb->B.Connect(0, Scale);
	M->GetExpressionCollection().AddExpression(ZoneAmb);
	UMaterialExpressionLinearInterpolate* Lerp = NewObject<UMaterialExpressionLinearInterpolate>(M);
	Lerp->A.Connect(0, MapAmb);
	Lerp->A.SetMask(1, 1, 1, 1, 0);            // the collection's vectors are float4
	Lerp->B.Connect(0, ZoneAmb);
	Lerp->Alpha.Connect(4, VC);                // A
	M->GetExpressionCollection().AddExpression(Lerp);
	UMaterialExpressionMultiply* Mul = NewObject<UMaterialExpressionMultiply>(M);
	Mul->A.Connect(Data->BaseColor.OutputIndex, BaseColor);
	Mul->B.Connect(0, Lerp);
	M->GetExpressionCollection().AddExpression(Mul);
	Data->EmissiveColor.Connect(0, Mul);
	M->PreEditChange(nullptr);
	M->PostEditChange();
	SaveAsset(M);
	return true;
}

UMaterialInterface* FMFImporter::GetMaterial(const FString& InKey)
{
	// core maps get their own instances (/Game/MF/Community/Materials): they point at the
	// original texture until replacement art exists, and re-pointing them needs no map rebuild
	const FString CacheKey = (bBuildingCoreMap ? TEXT("core:") : TEXT("")) + InKey;
	if (UMaterialInterface** Found = MaterialCache.Find(CacheKey))
	{
		return *Found;
	}
	const FString& Key = InKey;
	FString TexKey, Mode;
	if (!Key.Split(TEXT("|"), &TexKey, &Mode))
	{
		TexKey = Key;
		Mode = TEXT("opaque");
	}
	if (Mode == TEXT("collision"))
	{
		MaterialCache.Add(CacheKey, nullptr);
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
		MaterialCache.Add(CacheKey, M);
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
		MaterialCache.Add(CacheKey, M);
		return M;
	}
	UTexture2D* Tex = FindTextureFor(TexKey, bBuildingCoreMap);
	const FString MIName = FString::Printf(TEXT("MI_%s_%s"), *Name, *Mode);
	bool bCreated = false;
	UMaterialInstanceConstant* MIC = FindOrCreateAsset<UMaterialInstanceConstant>(
		FString::Printf(TEXT("%s/Materials/%s"), bBuildingCoreMap ? *Packs.CoreRoot : *Packs.TextureRoot(TexKey), *Dir), MIName, bCreated);
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
	MaterialCache.Add(CacheKey, MIC);
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
	// pos3f_nrm3f_uv2f[_col3f]: world geometry carries its UE1 zone ambient as vertex colour
	const bool bZoneColors = J->GetStringField(TEXT("vertex_layout")).EndsWith(TEXT("col3f"));
	const int32 Stride = bZoneColors ? 11 : 8;
	if (Bin.Num() < NumVerts * Stride * 4 + NumIndices * 4)
	{
		UE_LOG(LogMFEditor, Error, TEXT("Truncated mesh data for %s"), *JsonFile);
		return nullptr;
	}
	const float* V = reinterpret_cast<const float*>(Bin.GetData());
	const uint32* Idx = reinterpret_cast<const uint32*>(Bin.GetData() + NumVerts * Stride * 4);

	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> Colors = Attr.GetVertexInstanceColors();
	TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
	UVs.SetNumChannels(1);

	// Weld positions so lightmap UV generation sees connected surfaces
	TMap<FVector3f, FVertexID> Welded;
	TArray<FVertexInstanceID> Instances;
	Instances.SetNum(NumVerts);
	for (int32 i = 0; i < NumVerts; ++i)
	{
		const float* P = V + i * Stride;
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
		// alpha 1 = this zone's ambient in RGB; alpha 0 = use the map's main ambient (props)
		Colors[VI] = bZoneColors ? FVector4f(P[8], P[9], P[10], 1.f) : FVector4f(0.f, 0.f, 0.f, 0.f);
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
	const FString AssetPath = FString::Printf(TEXT("%s/%s/%s"), *Packs.MapDataRoot(MapName), *Folder, *SafeName(Name));
	UStaticMesh* SM = BuildStaticMesh(MapDir / (MeshRel + TEXT(".mfmesh.json")), AssetPath);
	MeshCache.Add(CacheKey, SM);
	return SM;
}
