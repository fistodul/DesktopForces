#include "DFImporter.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "PhysicsEngine/ConvexElem.h"
#include "Materials/MaterialExpressionAdd.h"
#include "DFImportUtil.h"
#include "Sound/SoundAttenuation.h"
#include "DesktopForcesEditor.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Algo/Find.h"
#include "AssetCompilingManager.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "MeshUtilitiesCommon.h"
#include "OverlappingCorners.h"
#include "StaticMeshOperations.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionDecalLifetimeOpacity.h"
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
#include "Engine/PrimaryAssetLabel.h"
#include "FileHelpers.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/SecureHash.h"
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

using namespace DFImportUtil;

FDFPacks FDFImporter::Packs;

void FDFPacks::Load(const FString& File) {
	TSharedPtr<FJsonObject> J = LoadJson(File);
	if (!J) {
		UE_LOG(LogDFEditor, Warning, TEXT("No %s: everything goes to %s"), *File, *ClassicRoot);
		return;
	}
	J->TryGetStringField(TEXT("classic_root"), ClassicRoot);
	J->TryGetStringField(TEXT("community_root"), CommunityRoot);
	J->TryGetStringField(TEXT("community_maps_root"), CommunityMapsRoot);
	J->TryGetStringField(TEXT("replaced_root"), ReplacedRoot);
	J->TryGetBoolField(TEXT("remaster_classic"), bRemasterClassic);
	J->TryGetStringField(TEXT("unverified_root"), UnverifiedRoot);
	CommunityMaps.Reset();
	CommunityPackages.Reset();
	UnverifiedPackages.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* Unverified; J->TryGetArrayField(TEXT("unverified_packages"), Unverified))
		for (const TSharedPtr<FJsonValue>& V : *Unverified)
			UnverifiedPackages.Add(V->AsString().ToLower());
	ClassicPackages.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* Classic; J->TryGetArrayField(TEXT("classic_packages"), Classic))
		for (const TSharedPtr<FJsonValue>& V : *Classic)
			ClassicPackages.Add(V->AsString().ToLower());
	UnverifiedMaps.Reset();
	Replaced.Reset();
	if (const TSharedPtr<FJsonObject>* Maps; J->TryGetObjectField(TEXT("maps"), Maps)) {
		for (const auto& Pair : (*Maps)->Values)
			if (Pair.Value->AsString() == TEXT("community"))
				CommunityMaps.Add(FString(Pair.Key).ToLower());
			else if (Pair.Value->AsString() == TEXT("unverified"))
				UnverifiedMaps.Add(FString(Pair.Key).ToLower());
	}
	for (const TSharedPtr<FJsonValue>& V : J->GetArrayField(TEXT("community_packages")))
		CommunityPackages.Add(V->AsString().ToLower());
	for (const TSharedPtr<FJsonValue>& V : J->GetArrayField(TEXT("replaced")))
		Replaced.Add(V->AsString().ToLower());
	UnverifiedReplaced.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* Only; J->TryGetArrayField(TEXT("unverified_replaced"), Only))
		for (const TSharedPtr<FJsonValue>& V : *Only)
			UnverifiedReplaced.Add(V->AsString().ToLower());
	UE_LOG(LogDFEditor, Display, TEXT("Packs: %d community maps, %d community packages, %d replaced textures"), CommunityMaps.Num(), CommunityPackages.Num(), Replaced.Num());
}

FString FDFPacks::PackageRoot(const FString& Package) const {
	const FString P = Package.ToLower();
	if (CommunityPackages.Contains(P))
		return CommunityRoot;
	if (ClassicPackages.IsEmpty()) // a packs.json from before 2026-10-29: unverified only if listed
		return UnverifiedPackages.Contains(P) ? UnverifiedRoot : ClassicRoot;
	return ClassicPackages.Contains(P) ? ClassicRoot : UnverifiedRoot;
}

FString FDFPacks::TextureRoot(const FString& Key) const {
	FString Package;
	return PackageRoot(Key.Split(TEXT("."), &Package, nullptr) ? Package : Key);
}

FString FDFPacks::MapPackage(const FString& Map) const {
	const FString M = Map.ToLower();
	return (CommunityMaps.Contains(M) ? CommunityMapsRoot : UnverifiedMaps.Contains(M) ? UnverifiedRoot / TEXT("Maps")
																					   : ClassicRoot / TEXT("Maps")) /
		Map;
}

FDFImporter::FDFImporter(const FString& InWorkDir)
	: WorkDir(InWorkDir) {
	Packs.Load(WorkDir / TEXT("packs.json"));
}

bool FDFImporter::SaveAsset(UObject* Asset) {
	if (!Asset)
		return false;
	UPackage* Pkg = Asset->GetOutermost();
	Pkg->MarkPackageDirty();
	const FString Ext = Asset->IsA<UWorld>() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
	const FString File = FPackageName::LongPackageNameToFilename(Pkg->GetName(), Ext);
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.Error = GWarn; // report problems as warnings (GError would be fatal)
	const bool bOk = UPackage::SavePackage(Pkg, Asset, *File, Args);
	if (!bOk)
		UE_LOG(LogDFEditor, Error, TEXT("Failed to save %s"), *File);
	return bOk;
}

FString FDFImporter::TextureAssetPath(const FString& Key) {
	FString Dir, Name;
	SplitKey(Key, Dir, Name);
	return FString::Printf(TEXT("%s/Textures/%s/%s.%s"), *Packs.TextureRoot(Key), *Dir, *Name, *Name);
}

FString FDFImporter::ReplacementAssetPath(const FString& Key) {
	FString Dir, Name;
	SplitKey(Key, Dir, Name);
	return FString::Printf(TEXT("%s/Textures/%s/%s.%s"), *Packs.ReplacementRoot(Key), *Dir, *Name, *Name);
}

UTexture2D* FDFImporter::FindTexture(const FString& Key) const {
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *TextureAssetPath(Key), nullptr, LOAD_NoWarn | LOAD_Quiet);
	return Tex || !Packs.HasReplacement(Key) ? Tex : LoadObject<UTexture2D>(nullptr, *ReplacementAssetPath(Key), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

UTexture2D* FDFImporter::FindTextureFor(const FString& Key, bool bCommunityMap) const {
	if ((bCommunityMap || Packs.bRemasterClassic) && Packs.HasReplacement(Key)) {
		if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *ReplacementAssetPath(Key), nullptr, LOAD_NoWarn | LOAD_Quiet))
			return Tex;
	}
	return FindTexture(Key);
}

void FDFImporter::RepointCommunityMaterials() {
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AR.ScanPathsSynchronous({ Packs.CommunityRoot / TEXT("Materials") }, true);
	int32 Count = 0;
	for (const FString& Key : Packs.Replaced) {
		FString Dir, Name;
		SplitKey(Key, Dir, Name);
		TArray<FAssetData> Assets;
		AR.GetAssetsByPath(FName(*(Packs.CommunityRoot / TEXT("Materials") / Dir)), Assets, false);
		UTexture2D* Tex = FindTextureFor(Key, true);
		for (const FAssetData& A : Assets) {
			if (!Tex || !A.AssetName.ToString().StartsWith(TEXT("MI_") + Name + TEXT("_"), ESearchCase::IgnoreCase))
				continue;
			if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(A.GetAsset())) {
				UTexture* Current = nullptr;
				if (MIC->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Texture")), Current) && Current == Tex)
					continue; // already there: no re-save (every save rewrites the package's bytes)
				MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Texture")), Tex);
				MIC->PostEditChange();
				Count += SaveAsset(MIC) ? 1 : 0;
			}
		}
	}
	UE_LOG(LogDFEditor, Display, TEXT("Replacement art: %d community material instances point at it"), Count);
}

FString FDFImporter::OwnMaterialInstance(const FString& ClassicPackage, const FString& Root) {
	const FString CacheKey = Root + TEXT("|") + ClassicPackage;
	if (const FString* Known = OwnInstances.Find(CacheKey))
		return *Known;
	FString& Own = OwnInstances.Add(CacheKey);
	const FString Name = FPackageName::GetShortName(ClassicPackage);
	UMaterialInstanceConstant* Old = LoadObject<UMaterialInstanceConstant>(nullptr, *(ClassicPackage + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UTexture* OldTex = nullptr;
	if (!Old || !Old->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Texture")), OldTex) || !OldTex)
		return Own;
	FString Key = OldTex->GetOutermost()->GetName();
	const FString TexturesRoot = (Key.StartsWith(Packs.UnverifiedRoot) ? Packs.UnverifiedRoot : Packs.ClassicRoot) / TEXT("Textures/");
	if (FString Book = Key; Book.RemoveFromStart(Packs.ClassicRoot / TEXT("Effects/Textures/"))) {
		// a flipbook: the same grid drawn anew, for every pack that uses it
		const FString Effects = Packs.ReplacedRoot / TEXT("Effects");
		const FString Short = FPackageName::GetShortName(Book);
		UTexture2D* Art = LoadObject<UTexture2D>(nullptr, *(Effects / TEXT("Textures") / Book + TEXT(".") + Short), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Art)
			return Own;
		FString RelDir = FPackageName::GetLongPackagePath(ClassicPackage);
		RelDir.RemoveFromStart(Packs.ClassicRoot / TEXT("Effects/Materials"));
		bool bCreatedBook = false;
		UMaterialInstanceConstant* BookMI = FindOrCreateAsset<UMaterialInstanceConstant>(Effects / TEXT("Materials") + RelDir, Name, bCreatedBook);
		BookMI->SetParentEditorOnly(Old->Parent);
		BookMI->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Texture")), Art);
		BookMI->PostEditChange();
		if (SaveAsset(BookMI))
			Own = Effects / TEXT("Materials") + RelDir / Name;
		return Own;
	}
	if (!Key.RemoveFromStart(TexturesRoot))
		return Own;
	Key.ReplaceInline(TEXT("/"), TEXT("."));
	if (!Packs.HasReplacement(Key)) {
		// asset names lost what packages can't hold ("ap_P&Hd" is asset ap_P_Hd): the manifest's spelling
		const FString Safe = SafeName(Key);
		const FString* Real = Algo::FindByPredicate(Packs.Replaced, [&Safe](const FString& R) { return SafeName(R).Equals(Safe, ESearchCase::IgnoreCase); });
		if (!Real)
			return Own;
		Key = *Real;
	}
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *ReplacementAssetPath(Key), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Tex)
		return Own;
	FString Rel = FPackageName::GetLongPackagePath(ClassicPackage);
	Rel.RemoveFromStart(Packs.ClassicRoot / TEXT("Materials"));
	Rel.RemoveFromStart(Packs.UnverifiedRoot / TEXT("Materials"));
	const FString Folder = Root / TEXT("Materials") + Rel;
	bool bCreated = false;
	UMaterialInstanceConstant* MIC = FindOrCreateAsset<UMaterialInstanceConstant>(Folder, Name, bCreated);
	MIC->SetParentEditorOnly(Old->Parent);
	MIC->BasePropertyOverrides = Old->BasePropertyOverrides;
	MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Texture")), Tex);
	MIC->PostEditChange();
	if (SaveAsset(MIC))
		Own = Folder / Name;
	return Own;
}

int32 FDFImporter::RepointPackages(const TArray<FName>& Names, const FString& Root, bool bFromUnverified, TSet<FString>& Left, int32& Failed) {
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	const FString ClassicMaterials = Packs.ClassicRoot / TEXT("Materials/");
	const FString ClassicEffects = Packs.ClassicRoot / TEXT("Effects/Materials/");
	const FString UnverifiedMaterials = Packs.UnverifiedRoot / TEXT("Materials/");
	int32 Done = 0;
	for (const FName Name : Names) {
		TArray<FName> Deps;
		AR.GetDependencies(Name, Deps);
		TMap<UObject*, UObject*> Swap;
		for (const FName Dep : Deps) {
			const FString From = Dep.ToString();
			if (FString Sound = From; Sound.RemoveFromStart(Packs.ClassicRoot / TEXT("Sounds/"))) {
				const FString Short = FPackageName::GetShortName(Sound);
				UObject* OldSound = LoadObject<USoundBase>(nullptr, *(From + TEXT(".") + Short), nullptr, LOAD_NoWarn | LOAD_Quiet);
				UObject* NewSound = LoadObject<USoundBase>(nullptr, *(Packs.ReplacedRoot / TEXT("Sounds") / Sound + TEXT(".") + Short), nullptr, LOAD_NoWarn | LOAD_Quiet);
				if (OldSound && NewSound)
					Swap.Add(OldSound, NewSound);
				continue;
			}
			if (!From.StartsWith(ClassicMaterials) && !From.StartsWith(ClassicEffects) && !(bFromUnverified && From.StartsWith(UnverifiedMaterials)))
				continue;
			const FString To = OwnMaterialInstance(From, Root);
			if (To.IsEmpty()) {
				Left.Add(From);
				continue;
			}
			UObject* Old = LoadObject<UMaterialInterface>(nullptr, *(From + TEXT(".") + FPackageName::GetShortName(From)), nullptr, LOAD_NoWarn | LOAD_Quiet);
			UObject* New = LoadObject<UMaterialInterface>(nullptr, *(To + TEXT(".") + FPackageName::GetShortName(To)), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (Old && New)
				Swap.Add(Old, New);
		}
		if (Swap.IsEmpty())
			continue;
		UPackage* Pkg = LoadPackage(nullptr, *Name.ToString(), LOAD_None);
		if (!Pkg) {
			++Failed;
			continue;
		}
		FAssetCompilingManager::Get().FinishAllCompilation();
		int64 Count = 0;
		ForEachObjectWithPackage(Pkg, [&](UObject* Object) {
			FArchiveReplaceObjectRef<UObject> Replace(Object, Swap, EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
			Count += Replace.GetCount();
			return true;
		});
		if (Count == 0)
			continue;
		UObject* Asset = Pkg->FindAssetInPackage();
		if (Asset && SaveAsset(Asset))
			++Done;
		else
			++Failed;
	}
	CollectGarbage(RF_NoFlags);
	return Done;
}

bool FDFImporter::RepointMaps(const TArray<FString>& Maps) {
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AR.ScanPathsSynchronous({ Packs.CommunityMapsRoot, Packs.CommunityRoot, Packs.UnverifiedRoot, Packs.ClassicRoot / TEXT("Materials"), Packs.ClassicRoot / TEXT("Effects/Materials"), Packs.ClassicRoot / TEXT("Sounds") }, true);
	int32 Saved = 0, Failed = 0;
	TSet<FString> Left;
	for (const FString& Map : Maps) {
		const bool bCommunity = Packs.IsCommunityMap(Map);
		if (!bCommunity && !Packs.IsUnverifiedMap(Map))
			continue;
		TArray<FName> Names = { FName(*Packs.MapPackage(Map)) };
		TArray<FAssetData> Data;
		AR.GetAssetsByPath(FName(*Packs.MapDataRoot(Map)), Data, true);
		for (const FAssetData& A : Data)
			Names.AddUnique(A.PackageName);
		const int32 Done = RepointPackages(Names, bCommunity ? Packs.CommunityRoot : Packs.UnverifiedRoot, bCommunity, Left, Failed);
		Saved += Done;
		UE_LOG(LogDFEditor, Display, TEXT("Repoint %s: %d of %d packages"), *Map, Done, Names.Num());
	}
	// the community pack's own props (boats, mod vehicles) borrow too
	TArray<FName> Props;
	TArray<FAssetData> PropAssets;
	AR.GetAssetsByPath(FName(*(Packs.CommunityRoot / TEXT("Props"))), PropAssets, true);
	for (const FAssetData& A : PropAssets)
		Props.AddUnique(A.PackageName);
	Saved += RepointPackages(Props, Packs.CommunityRoot, true, Left, Failed);
	UE_LOG(LogDFEditor, Display, TEXT("Repoint: %d packages saved, %d failed, %d material instances without replacement art"), Saved, Failed, Left.Num());
	for (const FString& Package : Left)
		UE_LOG(LogDFEditor, Display, TEXT("  no art: %s"), *Package);
	return Failed == 0;
}

USoundWave* FDFImporter::FindSound(const FString& DFPath) const {
	FString Pkg, Name;
	if (!DFPath.Split(TEXT("."), &Pkg, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		return nullptr;
	FString Package = Pkg;
	Package.Split(TEXT("."), &Package, nullptr); // drop a group: "levelSFX.Water.Drip" -> levelSFX
	if (Package.IsEmpty())
		Package = Pkg;
	const FString Safe = SafeName(Name);
	const FString Path = FString::Printf(TEXT("%s/Sounds/%s/%s.%s"), *Packs.PackageRoot(Package), *Package, *Safe, *Safe);
	return LoadObject<USoundWave>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

// --- textures --------------------------------------------------------------------------

bool FDFImporter::ImportTextures() {
	TSharedPtr<FJsonObject> Manifest = LoadJson(WorkDir / TEXT("textures.json"));
	if (!Manifest)
		return false;
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UAssetImportTask*> Tasks;
	struct FTexKey {
		FString Key;
		bool bAlpha;
		bool bUI;
		bool bReplacement;
		bool bDetail;
	};
	TArray<FTexKey> Keys;
	// a texture already imported from the same file (AssetImportData keeps its MD5) isn't imported
	// again: a re-import and save rewrites the package's bytes, so unchanged art would show up as
	// changed in git after every batch
	TArray<FTexKey> Unchanged;
	auto IsUpToDate = [](const FString& AssetPath, const FString& File) {
		const UTexture2D* Old = LoadObject<UTexture2D>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Old || !Old->AssetImportData || Old->AssetImportData->SourceData.SourceFiles.Num() != 1)
			return false;
		const FMD5Hash Hash = FMD5Hash::HashFile(*File);
		return Hash.IsValid() && Hash == Old->AssetImportData->SourceData.SourceFiles[0].FileHash;
	};
	for (const auto& Pair : Manifest->Values) {
		const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
		FString Dir, Name;
		const FString Key(Pair.Key);
		SplitKey(Key, Dir, Name);
		bool bUI = false;
		if (!Info->TryGetBoolField(TEXT("ui"), bUI))
			bUI = IsUITexture(Key); // manifests from before the "ui" flag
		const FString File = FPaths::ConvertRelativePathToFull(WorkDir / Info->GetStringField(TEXT("file")));
		if (IsUpToDate(TextureAssetPath(Key), File)) {
			Unchanged.Add({ Key, Info->GetBoolField(TEXT("alpha")), bUI, false, Info->HasField(TEXT("detail")) });
		} else {
			UAssetImportTask* Task = NewObject<UAssetImportTask>();
			Task->Filename = File;
			Task->DestinationPath = FString::Printf(TEXT("%s/Textures/%s"), *Packs.TextureRoot(Key), *Dir);
			Task->DestinationName = Name;
			Task->bAutomated = true;
			Task->bReplaceExisting = true;
			Task->bSave = false;
			Tasks.Add(Task);
			Keys.Add({ Key, Info->GetBoolField(TEXT("alpha")), bUI, false, Info->HasField(TEXT("detail")) });
		}
		FString Replacement;
		if (Info->TryGetStringField(TEXT("replacement"), Replacement)) {
			const FString RepFile = FPaths::ConvertRelativePathToFull(WorkDir / Replacement);
			if (IsUpToDate(ReplacementAssetPath(Key), RepFile)) {
				Unchanged.Add({ Key, Info->GetBoolField(TEXT("alpha")), bUI, true, Info->HasField(TEXT("detail")) });
				continue;
			}
			UAssetImportTask* Rep = NewObject<UAssetImportTask>();
			Rep->Filename = RepFile;
			Rep->DestinationPath = FString::Printf(TEXT("%s/Textures/%s"), *Packs.ReplacementRoot(Key), *Dir);
			Rep->DestinationName = Name;
			Rep->bAutomated = true;
			Rep->bReplaceExisting = true;
			Rep->bSave = false;
			Tasks.Add(Rep);
			Keys.Add({ Key, Info->GetBoolField(TEXT("alpha")), bUI, true, Info->HasField(TEXT("detail")) });
		}
	}
	const int32 NumImported = Keys.Num();
	Keys.Append(Unchanged);
	UE_LOG(LogDFEditor, Display, TEXT("Importing %d textures (%d unchanged)"), Tasks.Num(), Unchanged.Num());
	if (!Tasks.IsEmpty())
		AssetTools.ImportAssetTasks(Tasks);

	int32 Ok = 0, Failed = 0;
	for (int32 i = 0; i < Keys.Num(); ++i) {
		const FString Path = Keys[i].bReplacement ? ReplacementAssetPath(Keys[i].Key) : TextureAssetPath(Keys[i].Key);
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Tex) {
			UE_LOG(LogDFEditor, Warning, TEXT("Texture import failed: %s"), *Keys[i].Key);
			++Failed;
			continue;
		}
		// Mobile-friendly: world group, never streamed (they're tiny), no alpha channel unless masked
		const bool bUI = Keys[i].bUI;
		const int32 Longest = FMath::Max(Tex->Source.GetSizeX(), Tex->Source.GetSizeY()), Shortest = FMath::Min(Tex->Source.GetSizeX(), Tex->Source.GetSizeY());
		const bool bStrip = Longest > 512 && Longest >= 4 * Shortest; // a long side phones need more of than their limit
		const TextureGroup Group = bUI ? TEXTUREGROUP_UI : bStrip ? TEXTUREGROUP_Project02
			: Keys[i].bDetail                                     ? TEXTUREGROUP_Project01 // seen up close (Tools/mfport/detail.toml)
																  : TEXTUREGROUP_World;
		const TextureCompressionSettings Compression = bUI ? TC_EditorIcon : TC_Default;
		const TextureMipGenSettings Mips = bUI ? TMGS_NoMipmaps : TMGS_FromTextureGroup;
		// HUD art and bitmap fonts: uncompressed, never mipped; fonts point sampled (crisp glyphs),
		// the art filtered like MF's canvas (soft palette-alpha glows when scaled)
		const TextureFilter Filter = !bUI ? TF_Default : Keys[i].Key.StartsWith(TEXT("MobileForceFonts."), ESearchCase::IgnoreCase) ? TF_Nearest
																																	: TF_Bilinear;
		const bool bSettled = i >= NumImported && Tex->LODGroup == Group && Tex->NeverStream && Tex->SRGB && Tex->CompressionSettings == Compression &&
			Tex->CompressionNoAlpha == !Keys[i].bAlpha && Tex->Filter == Filter && Tex->MipGenSettings == Mips;
		if (bSettled)
			continue; // same file, same settings: leave the package alone
		Tex->LODGroup = Group;
		Tex->NeverStream = true;
		Tex->SRGB = true;
		Tex->CompressionSettings = Compression;
		Tex->CompressionNoAlpha = !Keys[i].bAlpha;
		Tex->Filter = Filter;
		Tex->MipGenSettings = Mips;
		Tex->PostEditChange();
		Ok += SaveAsset(Tex) ? 1 : 0;
	}
	UE_LOG(LogDFEditor, Display, TEXT("Textures: %d saved, %d unchanged, %d failed"), Ok, Keys.Num() - Ok - Failed, Failed);
	RepointCommunityMaterials();
	ImportFonts();
	return Failed < Keys.Num(); // something there: unchanged textures count
}

bool FDFImporter::IsUITexture(const FString& Key) {
	return Key.StartsWith(TEXT("Rage."), ESearchCase::IgnoreCase) || Key.StartsWith(TEXT("RageWeapons."), ESearchCase::IgnoreCase) || Key.StartsWith(TEXT("MobileForceFonts."), ESearchCase::IgnoreCase) || Key.Contains(TEXT("minimap"), ESearchCase::IgnoreCase);
}

void FDFImporter::ImportFonts() {
	// MF bitmap fonts (fonts.json from mflib/fonts.py): offline-cache UFonts over the page textures
	TSharedPtr<FJsonObject> Fonts = LoadJson(WorkDir / TEXT("fonts.json"));
	if (!Fonts)
		return;
	int32 Count = 0;
	for (const auto& Pair : Fonts->Values) {
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
		for (int32 PageIndex = 0; PageIndex < Pages.Num(); ++PageIndex) {
			const TSharedPtr<FJsonObject> Page = Pages[PageIndex]->AsObject();
			Font->Textures.Add(FindTexture(Page->GetStringField(TEXT("texture"))));
			const TArray<TSharedPtr<FJsonValue>>& Chars = Page->GetArrayField(TEXT("chars"));
			for (int32 i = 0; i < PerPage; ++i) {
				FFontCharacter Ch;
				if (Chars.IsValidIndex(i)) {
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
	UE_LOG(LogDFEditor, Display, TEXT("Fonts: %d imported"), Count);
}

// --- sounds ----------------------------------------------------------------------------

USoundAttenuation* FDFImporter::EnsureSoundAttenuation() {
	// UE1 PlaySound: Radius 0 -> TransientSoundRadius 0 -> the engine's 1600 uu; full volume close
	// by, falling off linearly to nothing at the radius
	const TCHAR* Path = TEXT("/Game/MF/Audio/ATT_MF");
	if (USoundAttenuation* Existing = LoadObject<USoundAttenuation>(nullptr, *FString::Printf(TEXT("%s.ATT_MF"), Path), nullptr, LOAD_NoWarn | LOAD_Quiet))
		return Existing;
	UPackage* Pkg = CreatePackage(Path);
	USoundAttenuation* A = NewObject<USoundAttenuation>(Pkg, TEXT("ATT_MF"), RF_Public | RF_Standalone);
	A->Attenuation.bAttenuate = true;
	A->Attenuation.bSpatialize = true;
	A->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
	A->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	A->Attenuation.AttenuationShapeExtents = FVector(225.f, 0.f, 0.f); // 100 uu at full volume
	A->Attenuation.FalloffDistance = 3600.f - 225.f; // silent at 1600 uu
	FAssetRegistryModule::AssetCreated(A);
	SaveAsset(A);
	return A;
}

bool FDFImporter::ImportSounds(const FString& SubDir, bool bLooping) {
	const FString SoundDir = WorkDir / SubDir;
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *SoundDir, TEXT("*.wav"), true, false);
	if (Files.Num() == 0) {
		UE_LOG(LogDFEditor, Warning, TEXT("No sounds in %s"), *SoundDir);
		return false;
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UAssetImportTask*> Tasks;
	for (const FString& File : Files) {
		FString Rel = File;
		FPaths::MakePathRelativeTo(Rel, *(SoundDir + TEXT("/")));
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = File;
		FString Package = Rel;
		Package.Split(TEXT("/"), &Package, nullptr); // <package>/<name>.wav
		Task->DestinationPath = FString::Printf(TEXT("%s/%s/%s"), *Packs.PackageRoot(Package), *SubDir, *FPaths::GetPath(Rel));
		Task->DestinationName = SafeName(FPaths::GetBaseFilename(Rel));
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = !bLooping;
		Tasks.Add(Task);
	}
	UE_LOG(LogDFEditor, Display, TEXT("Importing %d files from %s"), Tasks.Num(), *SubDir);
	AssetTools.ImportAssetTasks(Tasks);
	if (!bLooping) {
		// one-shot sounds fade out with distance like MF's (PlaySound without a radius: 1600 uu)
		USoundAttenuation* Attenuation = EnsureSoundAttenuation();
		for (UAssetImportTask* Task : Tasks) {
			for (UObject* Obj : Task->GetObjects()) {
				if (USoundWave* Wave = Cast<USoundWave>(Obj)) {
					// sounds MF plays as an actor's AmbientSound loop: vehicle engines, skids, fires
					const FString Name = Wave->GetName();
					const bool bLoop = Name.Equals(TEXT("Engine1"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("Engine"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("skid"), ESearchCase::IgnoreCase) || Name.EndsWith(TEXT("Loop"), ESearchCase::IgnoreCase) || Name.EndsWith(TEXT("EngineRunning"), ESearchCase::IgnoreCase);
					if (Wave->AttenuationSettings != Attenuation || Wave->bLooping != bLoop) {
						Wave->AttenuationSettings = Attenuation;
						Wave->bLooping = bLoop;
						SaveAsset(Wave);
					}
				}
			}
		}
	}
	if (bLooping) {
		for (UAssetImportTask* Task : Tasks) {
			for (UObject* Obj : Task->GetObjects()) {
				if (USoundWave* Wave = Cast<USoundWave>(Obj)) {
					Wave->bLooping = true;
					SaveAsset(Wave);
				}
			}
		}
	}
	return true;
}

bool FDFImporter::ImportSoundArt() {
	// Tools/artgen/ambience.py: <package>/<name>.wav beside work/out, for /Game/MF/Replacements
	const FString SoundDir = FPaths::GetPath(WorkDir) / TEXT("generated/sounds");
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *SoundDir, TEXT("*.wav"), true, false);
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UAssetImportTask*> Tasks;
	for (const FString& File : Files) {
		FString Rel = File;
		FPaths::MakePathRelativeTo(Rel, *(SoundDir + TEXT("/")));
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = File;
		Task->DestinationPath = Packs.ReplacedRoot / TEXT("Sounds") / FPaths::GetPath(Rel);
		Task->DestinationName = SafeName(FPaths::GetBaseFilename(Rel));
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;
		Tasks.Add(Task);
	}
	if (!Tasks.IsEmpty())
		AssetTools.ImportAssetTasks(Tasks);
	int32 Ok = 0;
	for (UAssetImportTask* Task : Tasks)
		for (UObject* Object : Task->GetObjects())
			if (USoundWave* Wave = Cast<USoundWave>(Object)) {
				// plays like the sound it stands in for
				FString Classic = Wave->GetOutermost()->GetName();
				Classic.RemoveFromStart(Packs.ReplacedRoot);
				const USoundWave* Old = LoadObject<USoundWave>(nullptr, *(Packs.ClassicRoot + Classic + TEXT(".") + Wave->GetName()), nullptr, LOAD_NoWarn | LOAD_Quiet);
				Wave->bLooping = Old ? Old->bLooping : true;
				Wave->AttenuationSettings = EnsureSoundAttenuation();
				Ok += SaveAsset(Wave) ? 1 : 0;
			}
	UE_LOG(LogDFEditor, Display, TEXT("Sound art: %d / %d"), Ok, Files.Num());
	return Ok == Files.Num();
}

// --- mod plugin art --------------------------------------------------------------------

bool FDFImporter::ImportProps() {
	TSharedPtr<FJsonObject> Props = LoadJson(WorkDir / TEXT("Props/props.json"));
	if (!Props) {
		UE_LOG(LogDFEditor, Warning, TEXT("No Props/props.json - run mfport.py props"));
		return false;
	}
	int32 Ok = 0, Total = 0;
	for (const auto& Pair : Props->GetObjectField(TEXT("meshes"))->Values) {
		const FString Rel = Pair.Value->AsObject()->GetStringField(TEXT("mesh")); // Package/Name
		++Total;
		Ok += BuildStaticMesh(WorkDir / TEXT("Props") / (Rel + TEXT(".mfmesh.json")), FString::Printf(TEXT("%s/Props/%s"), *Packs.PackageRoot(FPaths::GetPath(Rel)), *Rel)) ? 1 : 0;
	}
	UE_LOG(LogDFEditor, Display, TEXT("Props: %d / %d meshes"), Ok, Total);
	return Ok > 0;
}

void FDFImporter::ImportArtFolder(const FString& Folder) {
	// Tools/artgen/out/<folder>/<name>.png|wav -> /Game/MF/<folder>/<name> (artgen.py makes them:
	// generated textures and the team's SourceArt, nothing from the original game)
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/artgen/out"));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *(Dir / Folder), TEXT("*.*"), true, false);
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UAssetImportTask*> Tasks;
	TArray<FString> Textures;
	for (const FString& File : Files) {
		const FString Ext = FPaths::GetExtension(File).ToLower();
		if (Ext != TEXT("png") && Ext != TEXT("wav"))
			continue;
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
			Textures.Add(Task->DestinationPath / Task->DestinationName);
	}
	UE_LOG(LogDFEditor, Display, TEXT("Art: importing %d files (Tools/artgen/out)"), Tasks.Num());
	AssetTools.ImportAssetTasks(Tasks);
	for (UAssetImportTask* Task : Tasks) {
		const FString Path = Task->DestinationPath / Task->DestinationName;
		UObject* Asset = LoadObject<UObject>(nullptr, *(Path + TEXT(".") + Task->DestinationName), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (UTexture2D* Tex = Cast<UTexture2D>(Asset)) {
			// screens and icons: full size, no mips, drawn by Slate / the HUD canvas
			const bool bUI = Path.StartsWith(TEXT("/Game/MF/UI/")) || Path.Contains(TEXT("/Icons/"));
			Tex->LODGroup = bUI ? TEXTUREGROUP_UI : TEXTUREGROUP_World; // world: 512 max, 256 on the lowest texture quality
			Tex->MipGenSettings = bUI ? TMGS_NoMipmaps : TMGS_FromTextureGroup;
			Tex->NeverStream = true;
			Tex->SRGB = true;
			Tex->CompressionSettings = Path.Contains(TEXT("/Icons/")) || Path.Contains(TEXT("/UI/HUD/")) ? TC_EditorIcon : TC_Default; // small icons stay crisp
			Tex->PostEditChange();
			SaveAsset(Tex);
		} else if (USoundWave* Wave = Cast<USoundWave>(Asset)) {
			Wave->bLooping = false;
			Wave->PostEditChange();
			SaveAsset(Wave);
		}
	}
}

bool FDFImporter::ImportArt() {
	ImportArtFolder(FString());
	EnsureCrateMaterial();
	EnsureDecalMaterials();
	EnsureGoalAssets();
	EnsureTracerMaterial();
	EnsureVehicleMaterials();
	EnsureMuzzleFlashMaterial();
	return true;
}

void FDFImporter::MakeCollisionTwoSided() {
	// meshes built before bDoubleSidedGeometry: no rebake, they have no lighting of their own
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	FARFilter Filter;
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	for (const TCHAR* Root : { TEXT("/Game/Maps"), TEXT("/Game/MF/Maps"), TEXT("/MobileForcesUnverified/Maps"), TEXT("/MobileForcesClassic/Maps") })
		Filter.PackagePaths.Add(Root);
	Registry.Get().GetAssets(Filter, Assets);
	int32 Done = 0;
	for (const FAssetData& A : Assets) {
		if (!A.AssetName.ToString().StartsWith(TEXT("Collision_")))
			continue;
		UStaticMesh* Mesh = Cast<UStaticMesh>(A.GetAsset());
		UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
		if (!Body || Body->bDoubleSidedGeometry)
			continue;
		Body->bDoubleSidedGeometry = true;
		Body->InvalidatePhysicsData();
		Body->CreatePhysicsMeshes();
		Done += SaveAsset(Mesh) ? 1 : 0;
	}
	UE_LOG(LogDFEditor, Display, TEXT("Two-sided collision: %d meshes"), Done);
}

bool FDFImporter::ImportHUDArt() {
	// the core game's own HUD and weapons (Tools/artgen/hudart.py, weaponmesh.py), in the core pak;
	// DF::LoadClassicObject turns to them when no pack has the originals
	ImportArtFolder(TEXT("UI/HUD"));
	ImportArtFolder(TEXT("Art/Weapons"));
	UTexture2D* Metal = LoadObject<UTexture2D>(nullptr, TEXT("/Game/MF/Art/Weapons/T_MF_WeaponMetal.T_MF_WeaponMetal"));
	if (!Metal)
		return false;
	bool bCreated = false;
	UMaterialInstanceConstant* MIC = FindOrCreateAsset<UMaterialInstanceConstant>(TEXT("/Game/MF/Materials"), TEXT("MI_MF_WeaponMetal"), bCreated);
	MIC->SetParentEditorOnly(GetMasterMaterial(TEXT("Opaque")));
	MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Texture")), Metal);
	MIC->PostEditChange();
	SaveAsset(MIC);
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/artgen/out/Meshes/Weapons"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.mfmesh.json")), true, false);
	int32 Ok = 0;
	for (const FString& File : Files) {
		const FString Name = File.LeftChop(FString(TEXT(".mfmesh.json")).Len());
		UStaticMesh* Mesh = BuildStaticMesh(Dir / File, TEXT("/Game/MF/Meshes/Weapons") / Name);
		if (!Mesh)
			continue;
		Mesh->GetStaticMaterials()[0].MaterialInterface = MIC;
		Mesh->PostEditChange();
		Ok += SaveAsset(Mesh) ? 1 : 0;
	}
	UE_LOG(LogDFEditor, Display, TEXT("HUD art: %d / %d weapon meshes"), Ok, Files.Num());
	return Ok == Files.Num();
}

void FDFImporter::EnsureDecalMaterials() {
	// bullet holes and scorch marks (Tools/artgen: T_MF_BulletHole0-3, T_MF_Scorch0-1):
	// M_MF_Decal projects them (deferred decal into the DBuffer, fading out with the decal's
	// lifetime); M_MF_DecalQuad draws them on a flat quad (darkens what's behind: modulate),
	// the cheap way for small marks on walls
	auto Build = [this](const TCHAR* Name, bool bDecal) -> UMaterial* {
		bool bCreated = false;
		UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), Name, bCreated);
		if (!bCreated)
			return M;
		UMaterialEditorOnlyData* E = M->GetEditorOnlyData();
		UMaterialExpressionTextureSampleParameter2D* T = NewObject<UMaterialExpressionTextureSampleParameter2D>(M);
		T->ParameterName = TEXT("Texture");
		T->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/MF/Art/Decals/T_MF_BulletHole0.T_MF_BulletHole0"));
		M->GetExpressionCollection().AddExpression(T);
		if (bDecal) {
			M->MaterialDomain = MD_DeferredDecal;
			M->BlendMode = BLEND_Translucent;
			UMaterialExpressionDecalLifetimeOpacity* Fade = NewObject<UMaterialExpressionDecalLifetimeOpacity>(M);
			M->GetExpressionCollection().AddExpression(Fade);
			UMaterialExpressionMultiply* Opacity = NewObject<UMaterialExpressionMultiply>(M);
			Opacity->A.Connect(4, T);
			Opacity->B.Connect(0, Fade);
			M->GetExpressionCollection().AddExpression(Opacity);
			E->BaseColor.Connect(0, T);
			E->Opacity.Connect(0, Opacity);
		} else {
			M->SetShadingModel(MSM_Unlit);
			M->BlendMode = BLEND_Modulate;
			UMaterialExpressionConstant* One = NewObject<UMaterialExpressionConstant>(M);
			One->R = 1.f;
			M->GetExpressionCollection().AddExpression(One);
			UMaterialExpressionLinearInterpolate* Lerp = NewObject<UMaterialExpressionLinearInterpolate>(M);
			Lerp->A.Connect(0, One);
			Lerp->B.Connect(0, T);
			Lerp->Alpha.Connect(4, T);
			M->GetExpressionCollection().AddExpression(Lerp);
			E->EmissiveColor.Connect(0, Lerp);
			M->SetMaterialUsage(MATUSAGE_InstancedStaticMeshes);
		}
		M->PreEditChange(nullptr);
		M->PostEditChange();
		SaveAsset(M);
		UE_LOG(LogDFEditor, Display, TEXT("Created %s"), Name);
		return M;
	};
	UMaterial* Decal = Build(TEXT("M_MF_Decal"), true);
	UMaterial* Quad = Build(TEXT("M_MF_DecalQuad"), false);
	TArray<FString> Textures;
	for (int32 i = 0; i < 4; ++i)
		Textures.Add(FString::Printf(TEXT("BulletHole%d"), i));
	for (int32 i = 0; i < 2; ++i)
		Textures.Add(FString::Printf(TEXT("Scorch%d"), i));
	for (const FString& T : Textures) {
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/MF/Art/Decals/T_MF_%s.T_MF_%s"), *T, *T), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Tex) {
			UE_LOG(LogDFEditor, Warning, TEXT("Decal texture T_MF_%s missing (Tools/artgen)"), *T);
			continue;
		}
		for (const bool bDecal : { true, false }) {
			bool bCreated = false;
			UMaterialInstanceConstant* MI = FindOrCreateAsset<UMaterialInstanceConstant>(TEXT("/Game/MF/Materials/Decals"),
				FString::Printf(TEXT("MI_MF_%s%s"), *T, bDecal ? TEXT("") : TEXT("_Quad")), bCreated);
			MI->SetParentEditorOnly(bDecal ? Decal : Quad);
			MI->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Texture")), Tex);
			MI->PostEditChange();
			SaveAsset(MI);
		}
	}
}

void FDFImporter::EnsureTracerMaterial() {
	bool bCreated = false;
	UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_Tracer"), bCreated);
	UMaterialEditorOnlyData* E = M->GetEditorOnlyData();
	if (!bCreated && E->Opacity.Expression)
		return;
	if (bCreated) {
		UMaterialExpressionVectorParameter* Color = NewObject<UMaterialExpressionVectorParameter>(M);
		Color->ParameterName = TEXT("Color");
		Color->DefaultValue = FLinearColor(6.f, 4.5f, 2.2f);
		M->GetExpressionCollection().AddExpression(Color);
		E->EmissiveColor.Connect(0, Color);
	}
	UMaterialExpressionTextureCoordinate* UV = NewObject<UMaterialExpressionTextureCoordinate>(M);
	M->GetExpressionCollection().AddExpression(UV);
	UMaterialExpressionCustom* Fade = NewObject<UMaterialExpressionCustom>(M);
	Fade->Description = TEXT("Soft tracer edges and tips");
	Fade->Code = TEXT("float2 d = saturate(1 - abs(UV * 2 - 1)); return d.x * d.x * d.y * d.y;");
	Fade->OutputType = CMOT_Float1;
	Fade->Inputs.Reset();
	FCustomInput& Input = Fade->Inputs.AddDefaulted_GetRef();
	Input.InputName = TEXT("UV");
	Input.Input.Connect(0, UV);
	M->GetExpressionCollection().AddExpression(Fade);
	E->Opacity.Connect(0, Fade);
	M->SetShadingModel(MSM_Unlit);
	M->BlendMode = BLEND_Additive;
	M->TwoSided = true;
	M->PreEditChange(nullptr);
	M->PostEditChange();
	SaveAsset(M);
}

void FDFImporter::EnsureVehicleMaterials() {
	bool bCreated = false;
	UMaterial* Flag = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_VehicleFlag"), bCreated);
	if (bCreated) {
		UMaterialExpressionTextureSampleParameter2D* T = NewObject<UMaterialExpressionTextureSampleParameter2D>(Flag);
		T->ParameterName = TEXT("Texture");
		T->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		Flag->GetExpressionCollection().AddExpression(T);
		Flag->GetEditorOnlyData()->EmissiveColor.Connect(0, T);
		Flag->GetEditorOnlyData()->OpacityMask.Connect(4, T);
		Flag->SetShadingModel(MSM_Unlit);
		Flag->BlendMode = BLEND_Masked;
		Flag->TwoSided = true;
		Flag->PreEditChange(nullptr);
		Flag->PostEditChange();
		SaveAsset(Flag);
	}
	UMaterial* Warning = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_VehicleWarning"), bCreated);
	if (bCreated) {
		UMaterialExpressionVectorParameter* Color = NewObject<UMaterialExpressionVectorParameter>(Warning);
		Color->ParameterName = TEXT("Color");
		Color->DefaultValue = FLinearColor(0.55f, 0.12f, 0.05f); // ADFVehicle sets its own
		Warning->GetExpressionCollection().AddExpression(Color);
		UMaterialExpressionScalarParameter* Pulse = NewObject<UMaterialExpressionScalarParameter>(Warning);
		Pulse->ParameterName = TEXT("Pulse");
		Pulse->DefaultValue = 0.f;
		Pulse->bUseCustomPrimitiveData = true;
		Pulse->PrimitiveDataIndex = 4;
		Warning->GetExpressionCollection().AddExpression(Pulse);
		Warning->GetEditorOnlyData()->EmissiveColor.Connect(0, Color);
		Warning->GetEditorOnlyData()->Opacity.Connect(0, Pulse);
		Warning->SetShadingModel(MSM_Unlit);
		Warning->BlendMode = BLEND_Additive;
		Warning->PreEditChange(nullptr);
		Warning->PostEditChange();
		SaveAsset(Warning);
	}
}

void FDFImporter::EnsurePackLabels() {
	struct FPack {
		FString Root;
		const TCHAR* Name;
		int32 Chunk;
	};
	const FPack Packs_[] = {
		// names must differ: a label is a primary asset, its id is "PrimaryAssetLabel:<name>" and a
		// second label of the same name is ignored (its folder then lands in chunk 0)
		{ Packs.CommunityMapsRoot, TEXT("PAL_Community"), 1 },
		{ Packs.CommunityRoot, TEXT("PAL_CommunityArt"), 1 },
		{ Packs.ReplacedRoot, TEXT("PAL_Replacements"), 1 }, // replacement art: the community maps use it
		{ Packs.UnverifiedRoot, TEXT("PAL_Unverified"), 2 },
		{ Packs.ClassicRoot, TEXT("PAL_Classic"), 3 },
	};
	for (const FPack& P : Packs_) {
		FString Dir;
		if (!FPackageName::TryConvertLongPackageNameToFilename(P.Root / TEXT("X"), Dir) || !IFileManager::Get().DirectoryExists(*FPaths::GetPath(Dir)))
			continue; // a pack that isn't here (no classic content converted, a public checkout)
		bool bCreated = false;
		UPrimaryAssetLabel* Label = FindOrCreateAsset<UPrimaryAssetLabel>(P.Root, P.Name, bCreated);
		const bool bChanged = bCreated || Label->Rules.ChunkId != P.Chunk || !Label->bLabelAssetsInMyDirectory || Label->Rules.bApplyRecursively;
		if (!bChanged)
			continue;
		Label->bLabelAssetsInMyDirectory = true;
		Label->bIsRuntimeLabel = false;
		Label->Rules.ChunkId = P.Chunk;
		Label->Rules.Priority = 1;
		Label->Rules.bApplyRecursively = false; // other packs' assets this one uses stay in theirs
		Label->MarkPackageDirty();
		SaveAsset(Label);
		UE_LOG(LogDFEditor, Display, TEXT("Pack label %s/%s: chunk %d"), *P.Root, P.Name, P.Chunk);
	}
}

bool FDFImporter::Repack(bool bPlanOnly) {
	// which pack each imported asset belongs to: maps (<maps folder>/<map>, <map>_Data/...) by
	// name, <root>/<Kind>/<package>/... by package, the skinned meshes (Weapons/<mesh>,
	// Soldiers/<mesh>) by the package skeletal.json names; anything else stays where it is
	if (Packs.ClassicPackages.IsEmpty()) {
		UE_LOG(LogDFEditor, Error, TEXT("Repack: packs.json has no classic_packages (run mfport.py packs first)"));
		return false;
	}
	TMap<FString, FString> MeshPackages;
	if (TSharedPtr<FJsonObject> J = LoadJson(WorkDir / TEXT("Skeletal/skeletal.json")))
		for (const TSharedPtr<FJsonValue>& V : J->GetArrayField(TEXT("meshes")))
			MeshPackages.Add(V->AsObject()->GetStringField(TEXT("name")), V->AsObject()->GetStringField(TEXT("package")));
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	const TArray<FString> PackRoots = { Packs.ClassicRoot, Packs.UnverifiedRoot, Packs.CommunityRoot };
	const TArray<FString> MapFolders = { Packs.CommunityMapsRoot, Packs.ClassicRoot / TEXT("Maps"), Packs.UnverifiedRoot / TEXT("Maps") };
	AR.ScanPathsSynchronous(PackRoots, true);
	AR.ScanPathsSynchronous({ Packs.CommunityMapsRoot }, true);
	TSet<FString> Maps;
	for (const FString& Folder : MapFolders) {
		TArray<FAssetData> Worlds;
		AR.GetAssetsByPath(FName(*Folder), Worlds, false);
		for (const FAssetData& W : Worlds)
			if (W.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
				Maps.Add(W.AssetName.ToString());
	}
	TArray<FAssetRenameData> Renames;
	TMap<FString, int32> Moves; // "from -> to": count, for the log
	auto Move = [&](const FAssetData& A, const FString& From, const FString& To, const FString& RelDir) {
		if (!bPlanOnly)
			Renames.Emplace(A.GetAsset(), RelDir.IsEmpty() ? To : To / RelDir, A.AssetName.ToString());
		// the log: per pack and folder (Textures/<package>, <map>)
		TArray<FString> Parts;
		RelDir.ParseIntoArray(Parts, TEXT("/"));
		const FString What = Parts.Num() >= 2 && From != Packs.CommunityMapsRoot && !From.EndsWith(TEXT("/Maps")) ? Parts[0] / Parts[1] : Parts.Num() ? Parts[0]
																																					  : A.AssetName.ToString();
		Moves.FindOrAdd(From + TEXT(" -> ") + To + TEXT(": ") + What)++;
	};
	TArray<FString> Roots = PackRoots;
	Roots.Add(Packs.CommunityMapsRoot);
	for (const FString& Root : Roots) {
		TArray<FAssetData> Assets;
		AR.GetAssetsByPath(FName(*Root), Assets, true);
		for (const FAssetData& A : Assets) {
			if (A.IsRedirector())
				continue; // fixed up below
			const FString Dir = A.PackagePath.ToString();
			// a map's: <maps folder>/<map>, <maps folder>/<map>_Data/... (its _BuiltData moves with the map)
			const FString* MapFolder = MapFolders.FindByPredicate([&Dir](const FString& F) { return Dir == F || Dir.StartsWith(F + TEXT("/")); });
			if (MapFolder) {
				const FString RelDir = Dir.Mid(MapFolder->Len()).TrimChar(TEXT('/'));
				FString Map = RelDir.IsEmpty() ? A.AssetName.ToString() : RelDir.Left(RelDir.Find(TEXT("/")) == INDEX_NONE ? RelDir.Len() : RelDir.Find(TEXT("/")));
				if (Map.EndsWith(TEXT("_BuiltData")))
					continue;
				Map.RemoveFromEnd(TEXT("_Data"));
				if (!Maps.Contains(Map))
					continue; // labels and the like
				const FString Target = Packs.MapRoot(Map);
				if (Target != *MapFolder)
					Move(A, *MapFolder, Target, RelDir);
				continue;
			}
			if (Root == Packs.CommunityMapsRoot)
				continue;
			const FString RelDir = Dir.Mid(Root.Len()).TrimChar(TEXT('/'));
			TArray<FString> Parts;
			RelDir.ParseIntoArray(Parts, TEXT("/"));
			FString Package;
			// community maps' material instances are theirs, whatever texture they show: they stay
			if (Root == Packs.CommunityRoot && (Parts.Num() && (Parts[0] == TEXT("Materials") || Parts[0] == TEXT("Effects"))))
				continue;
			if (Parts.Num() >= 2 && (Parts[0] == TEXT("Textures") || Parts[0] == TEXT("Materials") || Parts[0] == TEXT("Sounds") || Parts[0] == TEXT("Music") || Parts[0] == TEXT("Props")))
				Package = Parts[1];
			else if (Parts.Num() >= 3 && Parts[0] == TEXT("Effects") && (Parts[1] == TEXT("Textures") || Parts[1] == TEXT("Materials")))
				Package = Parts[2];
			else if (Parts.Num() >= 2 && (Parts[0] == TEXT("Weapons") || Parts[0] == TEXT("Soldiers")))
				if (const FString* P = MeshPackages.Find(Parts[1]))
					Package = *P;
			if (Package.IsEmpty())
				continue;
			const FString Target = Packs.PackageRoot(Package);
			if (Target != Root)
				Move(A, Root, Target, RelDir);
		}
	}
	for (const TPair<FString, int32>& M : Moves)
		UE_LOG(LogDFEditor, Display, TEXT("Repack: %s: %d assets"), *M.Key, M.Value);
	IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	auto SaveDirty = [this]() {
		TArray<UPackage*> Dirty;
		FEditorFileUtils::GetDirtyContentPackages(Dirty);
		FEditorFileUtils::GetDirtyWorldPackages(Dirty);
		for (UPackage* Pkg : Dirty)
			if (UObject* Asset = Pkg->FindAssetInPackage())
				SaveAsset(Asset);
		return Dirty.Num();
	};
	bool bOk = true;
	int32 Saved = 0;
	if (!bPlanOnly && !Renames.IsEmpty()) {
		bOk = Tools.RenameAssets(Renames); // loads and fixes the referencers
		Saved += SaveDirty();
	}
	// a referencer the rename didn't re-save leaves a redirector where the asset was: load the
	// referencers (that follows the redirector), save them (now pointing at the new place), delete
	// the redirector (AssetTools' FixupReferencers wants Slate: not in a commandlet)
	TArray<FAssetData> Redirectors;
	TArray<FString> RedirectorRoots = PackRoots;
	RedirectorRoots.Add(Packs.CommunityMapsRoot);
	AR.ScanPathsSynchronous(RedirectorRoots, true);
	for (const FString& Root : RedirectorRoots) {
		TArray<FAssetData> Assets;
		AR.GetAssetsByPath(FName(*Root), Assets, true);
		for (const FAssetData& A : Assets)
			if (A.IsRedirector())
				Redirectors.Add(A);
	}
	for (const FAssetData& R : Redirectors) {
		TArray<FName> Referencers;
		AR.GetReferencers(R.PackageName, Referencers);
		UE_LOG(LogDFEditor, Display, TEXT("Repack: redirector %s, %d referencers"), *R.PackageName.ToString(), Referencers.Num());
		if (bPlanOnly)
			continue;
		for (const FName& Ref : Referencers)
			if (UPackage* Pkg = LoadPackage(nullptr, *Ref.ToString(), LOAD_None))
				if (UObject* Asset = Pkg->FindAssetInPackage())
					Saved += SaveAsset(Asset) ? 1 : 0;
	}
	if (!bPlanOnly && !Redirectors.IsEmpty()) {
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		for (const FAssetData& R : Redirectors) {
			FString File;
			if (FPackageName::TryConvertLongPackageNameToFilename(R.PackageName.ToString(), File, FPackageName::GetAssetPackageExtension()))
				IFileManager::Get().Delete(*File, false, true, true);
		}
	}
	UE_LOG(LogDFEditor, Display, TEXT("Repack: %d assets moved%s, %d redirectors removed, %d packages saved"), Renames.Num(), bOk ? TEXT("") : TEXT(" (with errors)"), bPlanOnly ? 0 : Redirectors.Num(), Saved);
	return bOk;
}

void FDFImporter::EnsureMuzzleFlashMaterial() {
	// RageWeapon muzzle flash: FlashYellow (classic) drawn additive on a quad at the muzzle
	bool bCreated = false;
	UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_MuzzleFlash"), bCreated);
	if (!bCreated)
		return;
	UMaterialEditorOnlyData* E = M->GetEditorOnlyData();
	UMaterialExpressionTextureSampleParameter2D* T = NewObject<UMaterialExpressionTextureSampleParameter2D>(M);
	T->ParameterName = TEXT("Texture");
	UTexture2D* Flash = LoadObject<UTexture2D>(nullptr, *(Packs.ClassicRoot / TEXT("Textures/RageWeapons/MuzzleFlash/FlashYellow_A01.FlashYellow_A01")), nullptr, LOAD_NoWarn | LOAD_Quiet);
	T->Texture = Flash ? Flash : LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	M->GetExpressionCollection().AddExpression(T);
	UMaterialExpressionMultiply* Bright = NewObject<UMaterialExpressionMultiply>(M);
	Bright->A.Connect(0, T);
	Bright->ConstB = 2.f;
	M->GetExpressionCollection().AddExpression(Bright);
	E->EmissiveColor.Connect(0, Bright);
	M->SetShadingModel(MSM_Unlit);
	M->BlendMode = BLEND_Additive;
	M->TwoSided = true;
	M->PreEditChange(nullptr);
	M->PostEditChange();
	SaveAsset(M);
	UE_LOG(LogDFEditor, Display, TEXT("Created M_MF_MuzzleFlash (%s)"), Flash ? TEXT("FlashYellow") : TEXT("no classic texture yet"));
}

void FDFImporter::EnsureGoalAssets() {
	// MoBall goals (AMoBallGoal): an open ring, 1 m round and 1 m tall from its base (the
	// actor scales it), with an additive team-tinted net (Tools/artgen goalfield) both sides
	bool bCreated = false;
	UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_GoalField"), bCreated);
	if (bCreated) {
		UMaterialEditorOnlyData* E = M->GetEditorOnlyData();
		UMaterialExpressionTextureSampleParameter2D* T = NewObject<UMaterialExpressionTextureSampleParameter2D>(M);
		T->ParameterName = TEXT("Texture");
		T->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/MF/Art/T_MF_GoalField.T_MF_GoalField"));
		M->GetExpressionCollection().AddExpression(T);
		UMaterialExpressionVectorParameter* Color = NewObject<UMaterialExpressionVectorParameter>(M);
		Color->ParameterName = TEXT("Color");
		Color->DefaultValue = FLinearColor(1.f, 0.2f, 0.1f);
		Color->bUseCustomPrimitiveData = true;
		Color->PrimitiveDataIndex = 0;
		M->GetExpressionCollection().AddExpression(Color);
		UMaterialExpressionMultiply* Tint = NewObject<UMaterialExpressionMultiply>(M);
		Tint->A.Connect(0, T);
		Tint->B.Connect(0, Color);
		M->GetExpressionCollection().AddExpression(Tint);
		UMaterialExpressionMultiply* Bright = NewObject<UMaterialExpressionMultiply>(M);
		Bright->A.Connect(0, Tint);
		Bright->ConstB = 3.f; // reads in daylight too
		M->GetExpressionCollection().AddExpression(Bright);
		E->EmissiveColor.Connect(0, Bright);
		M->SetShadingModel(MSM_Unlit);
		M->BlendMode = BLEND_Additive;
		M->TwoSided = true;
		M->PreEditChange(nullptr);
		M->PostEditChange();
		SaveAsset(M);
		UE_LOG(LogDFEditor, Display, TEXT("Created M_MF_GoalField"));
	}

	UStaticMesh* SM = FindOrCreateAsset<UStaticMesh>(TEXT("/Game/MF/Meshes"), TEXT("SM_MF_Ring"), bCreated);
	if (!bCreated)
		return;
	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	UVs.SetNumChannels(1);
	const FPolygonGroupID PG = MD.CreatePolygonGroup();
	Attr.GetPolygonGroupMaterialSlotNames()[PG] = TEXT("Field");
	constexpr int32 Segments = 48;
	constexpr float Tiles = 12.f; // net cells around
	TArray<FVertexInstanceID> Bottom, Top;
	for (int32 i = 0; i <= Segments; ++i) {
		const float A = UE_TWO_PI * i / Segments;
		const FVector3f N(FMath::Cos(A), FMath::Sin(A), 0.f);
		for (int32 Row = 0; Row < 2; ++Row) {
			const FVertexID V = MD.CreateVertex();
			Positions[V] = N * 50.f + FVector3f(0.f, 0.f, Row * 100.f);
			const FVertexInstanceID VI = MD.CreateVertexInstance(V);
			Normals[VI] = N;
			UVs.Set(VI, 0, FVector2f(Tiles * i / Segments, 1.f - Row)); // bright row of the texture at the bottom
			(Row ? Top : Bottom).Add(VI);
		}
	}
	for (int32 i = 0; i < Segments; ++i) {
		// outward-facing (UE: clockwise seen from the front)
		MD.CreateTriangle(PG, { Bottom[i], Top[i], Bottom[i + 1] });
		MD.CreateTriangle(PG, { Top[i], Top[i + 1], Bottom[i + 1] });
	}
	SM->PreEditChange(nullptr);
	SM->GetStaticMaterials() = { FStaticMaterial(M, TEXT("Field"), TEXT("Field")) };
	SM->SetNumSourceModels(1);
	FStaticMeshSourceModel& Src = SM->GetSourceModel(0);
	Src.BuildSettings.bRecomputeNormals = false;
	Src.BuildSettings.bRecomputeTangents = true;
	Src.BuildSettings.bGenerateLightmapUVs = false;
	SM->CreateMeshDescription(0, MoveTemp(MD));
	SM->CommitMeshDescription(0);
	SM->CreateBodySetup();
	SM->GetBodySetup()->bNeverNeedsCookedCollisionData = true;
	SM->Build(true);
	SM->PostEditChange();
	SaveAsset(SM);
	UE_LOG(LogDFEditor, Display, TEXT("Created SM_MF_Ring"));
}

void FDFImporter::EnsureCrateMaterial() {
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/MF/Art/T_MF_Crate.T_MF_Crate"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	bool bCreated = false;
	UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_Crate"), bCreated);
	if (!bCreated)
		return;
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
	Lerp->Alpha.Connect(4, T); // alpha
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
	UE_LOG(LogDFEditor, Display, TEXT("Created M_MF_Crate"));
}

// --- materials -------------------------------------------------------------------------

UMaterialInterface* FDFImporter::EnsureSkyMaterial() {
	if (UMaterialInterface** Found = MasterCache.Find(TEXT("Sky")))
		return *Found;
	bool bNewRT = false;
	UTextureRenderTargetCube* RT = FindOrCreateAsset<UTextureRenderTargetCube>(TEXT("/Game/MF/Materials"), TEXT("RT_MF_Sky"), bNewRT);
	if (bNewRT) {
		RT->Init(256, PF_B8G8R8A8); // MF skies are 256 px textures; LDR is plenty
		RT->ClearColor = FLinearColor::Black;
		SaveAsset(RT);
	}
	bool bCreated = false;
	UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_Sky"), bCreated);
	if (bCreated) {
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
		UE_LOG(LogDFEditor, Display, TEXT("Created M_MF_Sky"));
	}
	if (!M->TwoSided) { // ADFSkyCapture's shell is seen from inside
		M->TwoSided = true;
		M->PreEditChange(nullptr);
		M->PostEditChange();
		SaveAsset(M);
	}
	MasterCache.Add(TEXT("Sky"), M);
	return M;
}

void FDFImporter::FixMasterMaterialUsage() {
	// masked sheets on walls (emblems, signs) lie in the wall's plane: UE1 drew BSP in order, a
	// depth buffer fights over it. A centimetre towards the camera settles it, nothing rebaked
	for (const TCHAR* Mode : { TEXT("Masked"), TEXT("MaskedUnlit") }) {
		const FString Path = FString::Printf(TEXT("/Game/MF/Materials/M_MF_%s.M_MF_%s"), Mode, Mode);
		UMaterial* M = LoadObject<UMaterial>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!M)
			continue;
		UMaterialExpressionMultiply* Bias = Cast<UMaterialExpressionMultiply>(M->GetEditorOnlyData()->WorldPositionOffset.Expression);
		if (Bias && Cast<UMaterialExpressionNormalize>(Bias->A.Expression))
			continue;
		// by hand: the camera vector node is pixel shader only and fails the whole material here
		UMaterialExpressionCameraPositionWS* Eye = NewObject<UMaterialExpressionCameraPositionWS>(M);
		M->GetExpressionCollection().AddExpression(Eye);
		UMaterialExpressionWorldPosition* Here = NewObject<UMaterialExpressionWorldPosition>(M);
		Here->WorldPositionShaderOffset = WPT_ExcludeAllShaderOffsets;
		M->GetExpressionCollection().AddExpression(Here);
		UMaterialExpressionSubtract* ToEye = NewObject<UMaterialExpressionSubtract>(M);
		ToEye->A.Connect(0, Eye);
		ToEye->B.Connect(0, Here);
		M->GetExpressionCollection().AddExpression(ToEye);
		UMaterialExpressionNormalize* View = NewObject<UMaterialExpressionNormalize>(M);
		View->VectorInput.Connect(0, ToEye);
		M->GetExpressionCollection().AddExpression(View);
		if (!Bias) {
			Bias = NewObject<UMaterialExpressionMultiply>(M);
			Bias->ConstB = 1.f;
			M->GetExpressionCollection().AddExpression(Bias);
		}
		Bias->A.Connect(0, View);
		M->GetEditorOnlyData()->WorldPositionOffset.Connect(0, Bias);
		M->PreEditChange(nullptr);
		M->PostEditChange();
		SaveAsset(M);
		UE_LOG(LogDFEditor, Display, TEXT("%s: sheets pulled off their walls"), *M->GetName());
	}
	for (const TCHAR* Mode : { TEXT("Opaque"), TEXT("Masked"), TEXT("MaskedUnlit"), TEXT("Unlit"), TEXT("Additive"),
			 TEXT("Modulated"), TEXT("TeamTint") }) {
		const FString Path = FString::Printf(TEXT("/Game/MF/Materials/M_MF_%s.M_MF_%s"), Mode, Mode);
		UMaterial* M = LoadObject<UMaterial>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		// instanced props (level "instance_props") too: without the flag the game draws them with
		// the default material, dark grey checks (mf-western's church benches, its barrels)
		for (const EMaterialUsage Usage : { MATUSAGE_StaticLighting, MATUSAGE_InstancedStaticMeshes })
			if (M && !M->GetUsageByFlag(Usage)) {
				M->SetMaterialUsage(Usage);
				M->PostEditChange();
				SaveAsset(M);
				UE_LOG(LogDFEditor, Display, TEXT("%s: usage %d set"), *M->GetName(), int32(Usage));
			}
		// a master that doesn't compile takes every instance down with it: say so
		if (M)
			if (const FMaterialResource* Res = M->GetMaterialResource(GMaxRHIShaderPlatform)) {
				const_cast<FMaterialResource*>(Res)->FinishCompilation();
				for (const FString& Err : Res->GetCompileErrors())
					UE_LOG(LogDFEditor, Error, TEXT("%s: %s"), *M->GetName(), *Err);
			}
	}
}

void FDFImporter::EnsureTeamTintMaterial() {
	bool bCreated = false;
	UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_TeamTint"), bCreated);
	if (!bCreated)
		return;
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
	UE_LOG(LogDFEditor, Display, TEXT("Created M_MF_TeamTint"));
}

void FDFImporter::UpdateMasters() {
	GetMasterMaterial(TEXT("Opaque"));
	GetMasterMaterial(TEXT("Masked"));
	GetMasterMaterial(TEXT("MaskedUnlit"));
	EnsureSkyMaterial();
}

UMaterialInterface* FDFImporter::GetMasterMaterial(const FString& Mode) {
	if (UMaterialInterface** Found = MasterCache.Find(Mode))
		return *Found;
	bool bCreated = false;
	UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_") + Mode, bCreated);
	if (bCreated) {
		// UE1 look: base texture, no specular, fully rough. Blend modes follow UE1 poly flags:
		// Masked = alpha test, Additive = PF_Translucent, Modulated = PF_Modulated (x2), Unlit = PF_Unlit.
		UMaterialEditorOnlyData* E = M->GetEditorOnlyData();
		UMaterialExpressionTextureSampleParameter2D* Tex = NewObject<UMaterialExpressionTextureSampleParameter2D>(M);
		Tex->ParameterName = TEXT("Texture");
		Tex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		M->GetExpressionCollection().AddExpression(Tex);
		auto Const = [M](float V) {
			UMaterialExpressionConstant* C = NewObject<UMaterialExpressionConstant>(M);
			C->R = V;
			M->GetExpressionCollection().AddExpression(C);
			return C;
		};
		const bool bLit = Mode == TEXT("Opaque") || Mode == TEXT("Masked");
		if (bLit) {
			M->SetShadingModel(MSM_DefaultLit);
			E->BaseColor.Connect(0, Tex);
			E->Specular.Connect(0, Const(0.f));
			E->Roughness.Connect(0, Const(1.f));
		} else {
			M->SetShadingModel(MSM_Unlit);
			if (Mode == TEXT("Modulated")) {
				UMaterialExpressionMultiply* Mul = NewObject<UMaterialExpressionMultiply>(M);
				Mul->A.Connect(0, Tex);
				Mul->ConstB = 2.f;
				M->GetExpressionCollection().AddExpression(Mul);
				E->EmissiveColor.Connect(0, Mul);
			} else {
				E->EmissiveColor.Connect(0, Tex);
			}
		}
		if (Mode == TEXT("Masked") || Mode == TEXT("MaskedUnlit")) {
			M->BlendMode = BLEND_Masked;
			E->OpacityMask.Connect(4, Tex); // alpha output
		} else if (Mode == TEXT("Additive")) {
			M->BlendMode = BLEND_Additive;
		} else if (Mode == TEXT("Modulated")) {
			M->BlendMode = BLEND_Modulate;
		}
		M->SetMaterialUsage(MATUSAGE_StaticLighting); // lightmapped level geometry
		M->PreEditChange(nullptr);
		M->PostEditChange();
		SaveAsset(M);
		UE_LOG(LogDFEditor, Display, TEXT("Created master material M_MF_%s"), *Mode);
	}
	// MSAA (forward) smooths a masked material's cut-out edges only through alpha to coverage:
	// without it thin bars (the warehouse's floor grates) crawl and flash as you move
	if ((Mode == TEXT("Masked") || Mode == TEXT("MaskedUnlit")) && !M->bUseAlphaToCoverage) {
		M->PreEditChange(nullptr);
		M->bUseAlphaToCoverage = true;
		M->PostEditChange();
		SaveAsset(M);
		UE_LOG(LogDFEditor, Display, TEXT("M_MF_%s: alpha to coverage"), *Mode);
	}
	if ((Mode == TEXT("Opaque") || Mode == TEXT("Masked")) && AddAmbientFloor(M))
		UE_LOG(LogDFEditor, Display, TEXT("M_MF_%s: ambient floor from MPC_MF"), *Mode);
	if ((Mode == TEXT("Opaque") || Mode == TEXT("Masked")) && AddActorLight(M))
		UE_LOG(LogDFEditor, Display, TEXT("M_MF_%s: actor light"), *Mode);
	if (const FMaterialResource* Res = M->GetMaterialResource(GMaxRHIShaderPlatform)) {
		const_cast<FMaterialResource*>(Res)->FinishCompilation();
		for (const FString& Err : Res->GetCompileErrors())
			UE_LOG(LogDFEditor, Error, TEXT("M_MF_%s: %s"), *Mode, *Err);
	}
	MasterCache.Add(Mode, M);
	return M;
}

UMaterialParameterCollection* FDFImporter::EnsureAmbientCollection() {
	bool bCreated = false;
	UMaterialParameterCollection* C = FindOrCreateAsset<UMaterialParameterCollection>(TEXT("/Game/MF/Materials"), TEXT("MPC_MF"), bCreated);
	bool bChanged = false;
	if (!C->VectorParameters.ContainsByPredicate([](const FCollectionVectorParameter& P) { return P.ParameterName == TEXT("Ambient"); })) {
		FCollectionVectorParameter P;
		P.ParameterName = TEXT("Ambient");
		P.DefaultValue = FLinearColor::Black;
		C->VectorParameters.Add(P);
		bChanged = true;
	}
	if (!C->ScalarParameters.ContainsByPredicate([](const FCollectionScalarParameter& P) { return P.ParameterName == TEXT("ShadeFloor"); })) {
		FCollectionScalarParameter P;
		P.ParameterName = TEXT("ShadeFloor");
		P.DefaultValue = 0.f;
		C->ScalarParameters.Add(P);
		bChanged = true;
	}
	if (!C->ScalarParameters.ContainsByPredicate([](const FCollectionScalarParameter& P) { return P.ParameterName == TEXT("AmbientScale"); })) {
		FCollectionScalarParameter P;
		P.ParameterName = TEXT("AmbientScale");
		P.DefaultValue = 0.35f;
		C->ScalarParameters.Add(P);
		bChanged = true;
	}
	if (bChanged) {
		C->PostEditChange();
		SaveAsset(C);
	}
	return C;
}

bool FDFImporter::AddActorLight(UMaterial* M) {
	// Emissive's map ambient + custom primitive data 0: 0 on everything baked, one add in the shader.
	// Its zone ambient + MPC ShadeFloor: outdoors UE1's shade came from its lights, Lightmass
	// leaves faces in zones without ambient near black
	UMaterialExpressionLinearInterpolate* Lerp = nullptr;
	bool bActorLight = false, bShadeFloor = false;
	for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions()) {
		const UMaterialExpressionScalarParameter* P = Cast<UMaterialExpressionScalarParameter>(E);
		bActorLight |= P && P->ParameterName == TEXT("ActorLight");
		const UMaterialExpressionCollectionParameter* CP = Cast<UMaterialExpressionCollectionParameter>(E);
		bShadeFloor |= CP && CP->ParameterName == TEXT("ShadeFloor");
		UMaterialExpressionLinearInterpolate* L = Cast<UMaterialExpressionLinearInterpolate>(E);
		if (L && Cast<UMaterialExpressionVertexColor>(L->Alpha.Expression))
			Lerp = L;
	}
	if (!Lerp || (bActorLight && bShadeFloor))
		return false;
	if (!bActorLight) {
		UMaterialExpressionScalarParameter* Light = NewObject<UMaterialExpressionScalarParameter>(M);
		Light->ParameterName = TEXT("ActorLight");
		Light->DefaultValue = 0.f;
		Light->bUseCustomPrimitiveData = true;
		Light->PrimitiveDataIndex = 0;
		M->GetExpressionCollection().AddExpression(Light);
		UMaterialExpressionAdd* Sum = NewObject<UMaterialExpressionAdd>(M);
		Sum->A.Connect(0, Lerp->A.Expression);
		Sum->A.SetMask(1, 1, 1, 1, 0);
		Sum->B.Connect(0, Light);
		M->GetExpressionCollection().AddExpression(Sum);
		Lerp->A.Connect(0, Sum);
		Lerp->A.SetMask(0, 0, 0, 0, 0);
	}
	if (!bShadeFloor) {
		UMaterialParameterCollection* Collection = EnsureAmbientCollection();
		UMaterialExpressionCollectionParameter* Shade = NewObject<UMaterialExpressionCollectionParameter>(M);
		Shade->Collection = Collection;
		Shade->ParameterName = TEXT("ShadeFloor");
		Shade->ParameterId = Collection->GetParameterId(TEXT("ShadeFloor"));
		M->GetExpressionCollection().AddExpression(Shade);
		UMaterialExpressionAdd* World = NewObject<UMaterialExpressionAdd>(M);
		World->A.Connect(0, Lerp->B.Expression);
		World->B.Connect(0, Shade);
		M->GetExpressionCollection().AddExpression(World);
		Lerp->B.Connect(0, World);
	}
	M->PreEditChange(nullptr);
	M->PostEditChange();
	SaveAsset(M);
	return true;
}

bool FDFImporter::AddAmbientFloor(UMaterial* M) {
	// UE1 zone ambient: unshadowed light on every surface. Mostly baked now (sky colour + bounce
	// light in Lightmass); this keeps a small floor on top: world geometry has its zone's ambient
	// in the vertex colour (alpha 1), props/actors (alpha 0) use the map's main ambient.
	//   Emissive = BaseColor * lerp(MPC.Ambient, VertexColor.rgb * MPC.AmbientScale, VertexColor.a)
	// No per-zone meshes, no per-component data. Lightmass ignores it (EmissiveBoost 0).
	UMaterialParameterCollection* Collection = EnsureAmbientCollection();
	UMaterialEditorOnlyData* Data = M->GetEditorOnlyData();
	TArray<UMaterialExpression*> Remove;
	for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions()) {
		if (Cast<UMaterialExpressionVertexColor>(E))
			return false; // already the current version
		// earlier versions: per-component vector parameter, or one global collection value
		const UMaterialExpressionVectorParameter* VP = Cast<UMaterialExpressionVectorParameter>(E);
		const UMaterialExpressionCollectionParameter* CP = Cast<UMaterialExpressionCollectionParameter>(E);
		if ((VP && VP->ParameterName == TEXT("Ambient")) || (CP && CP->ParameterName == TEXT("Ambient"))) {
			Remove.Add(E);
			for (const TObjectPtr<UMaterialExpression>& E2 : M->GetExpressions())
				if (UMaterialExpressionMultiply* Mul = Cast<UMaterialExpressionMultiply>(E2); Mul && Mul->B.Expression == E)
					Remove.Add(Mul);
		}
	}
	UMaterialExpression* BaseColor = Data->BaseColor.Expression;
	if (!BaseColor)
		return false;
	for (UMaterialExpression* E : Remove)
		M->GetExpressionCollection().RemoveExpression(E);
	auto AddCollectionParam = [&](const TCHAR* Name) {
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
	ZoneAmb->A.Connect(0, VC); // RGB
	ZoneAmb->B.Connect(0, Scale);
	M->GetExpressionCollection().AddExpression(ZoneAmb);
	UMaterialExpressionLinearInterpolate* Lerp = NewObject<UMaterialExpressionLinearInterpolate>(M);
	Lerp->A.Connect(0, MapAmb);
	Lerp->A.SetMask(1, 1, 1, 1, 0); // the collection's vectors are float4
	Lerp->B.Connect(0, ZoneAmb);
	Lerp->Alpha.Connect(4, VC); // A
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

UMaterialInterface* FDFImporter::GetMaterial(const FString& InKey) {
	// community maps get their own instances (/Game/MF/Community/Materials): they point at the
	// original texture until replacement art exists, and re-pointing them needs no map rebuild
	const FString CacheKey = (bBuildingCommunityMap ? TEXT("community:") : TEXT("")) + InKey;
	if (UMaterialInterface** Found = MaterialCache.Find(CacheKey))
		return *Found;
	const FString& Key = InKey;
	FString TexKey, Mode;
	if (!Key.Split(TEXT("|"), &TexKey, &Mode)) {
		TexKey = Key;
		Mode = TEXT("opaque");
	}
	if (Mode == TEXT("collision")) {
		MaterialCache.Add(CacheKey, nullptr);
		return nullptr;
	}
	const bool bTwoSided = Mode.Contains(TEXT("_2s"));
	const bool bUnlit = Mode.Contains(TEXT("_unlit"));
	FString Base;
	Mode.Split(TEXT("_"), &Base, nullptr);
	if (Base.IsEmpty())
		Base = Mode;
	if (Base == TEXT("sky")) {
		// UE1 never drew these polygons' own texture: the sky zone shows through them
		UMaterialInterface* M = EnsureSkyMaterial();
		MaterialCache.Add(CacheKey, M);
		return M;
	}
	FString Master = TEXT("Opaque");
	if (Base == TEXT("additive"))
		Master = TEXT("Additive");
	else if (Base == TEXT("modulated"))
		Master = TEXT("Modulated");
	else if (Base == TEXT("masked"))
		Master = bUnlit ? TEXT("MaskedUnlit") : TEXT("Masked");
	else if (bUnlit)
		Master = TEXT("Unlit");

	FString Dir, Name;
	SplitKey(TexKey, Dir, Name);
	if (Dir.IsEmpty() || Dir.StartsWith(TEXT("Engine"))) {
		// untextured surfaces: the master itself (engine default texture)
		UMaterialInterface* M = GetMasterMaterial(Master);
		MaterialCache.Add(CacheKey, M);
		return M;
	}
	UTexture2D* Tex = FindTextureFor(TexKey, bBuildingCommunityMap);
	if (Tex) {
		// the texture's own folder spelling: maps name packages in any case (UE1 didn't care:
		// "Train.Ammocount" and "train.Ammocount"), and case twins of a folder hide each other's
		// packages from the cooker
		const FString TexDir = FPackageName::GetLongPackagePath(Tex->GetOutermost()->GetName());
		const int32 At = TexDir.Find(TEXT("/Textures/"));
		if (At != INDEX_NONE && TexDir.Mid(At + 10).Equals(Dir, ESearchCase::IgnoreCase))
			Dir = TexDir.Mid(At + 10);
	}
	const FString MIName = FString::Printf(TEXT("MI_%s_%s"), *Name, *Mode);
	bool bCreated = false;
	UMaterialInstanceConstant* MIC = FindOrCreateAsset<UMaterialInstanceConstant>(
		FString::Printf(TEXT("%s/Materials/%s"), bBuildingCommunityMap ? *Packs.CommunityRoot : *Packs.TextureRoot(TexKey), *Dir), MIName, bCreated);
	MIC->SetParentEditorOnly(GetMasterMaterial(Master));
	if (Tex)
		MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Texture")), Tex);
	else
		UE_LOG(LogDFEditor, Warning, TEXT("Material %s: texture %s missing"), *Key, *TexKey);
	if (bTwoSided) {
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

namespace {
	/** Whether triangle ABC covers any of the texel square at (X, Y) (separating axes). */
	bool TouchesTexel(const FVector2f& A, const FVector2f& B, const FVector2f& C, int32 X, int32 Y) {
		const FVector2f Corners[4] = { { X + 0.f, Y + 0.f }, { X + 1.f, Y + 0.f }, { X + 0.f, Y + 1.f }, { X + 1.f, Y + 1.f } };
		const FVector2f Tri[3] = { A, B, C };
		for (int32 E = 0; E < 3; ++E) {
			const FVector2f P = Tri[E], Q = Tri[(E + 1) % 3], R = Tri[(E + 2) % 3];
			const FVector2f N(P.Y - Q.Y, Q.X - P.X);
			const float Inside = FMath::Sign((R - P) | N);
			float Most = -UE_BIG_NUMBER;
			for (const FVector2f& K : Corners)
				Most = FMath::Max(Most, Inside * ((K - P) | N));
			if (Most <= 0.f)
				return false;
		}
		return true;
	}

	/** Share of the lightmap's used texels that two surfaces take, so either bakes the other's
	 *  light. A texel counts for every chart touching it at all: at 32 px a walkway's planks are
	 *  narrower than a texel, and their tops and sides land in the same texels (mf-western's
	 *  walkways: no texel centre inside a plank, 15 % shared; dark, patchy sleepers on
	 *  mf-City_Rush's rails). Charts are triangles joined by corners at the same point and
	 *  lightmap UV; within one, a texel centre inside two triangles is a fold, and a texel no centre
	 *  falls in, touched by faces turned more than 25 degrees apart, bakes them one light (mf-western's
	 *  logs: 15 cm round at 53 cm texels, lit top and dark underside alike). Lightmass's "Lightmap UV
	 *  are overlapping by" counts more: neighbouring triangles of one chart in a texel too. */
	float LightmapOverlap(const FMeshDescription& MD, int32 Res) {
		const TVertexInstanceAttributesConstRef<FVector2f> UVs = FStaticMeshConstAttributes(MD).GetVertexInstanceUVs();
		TArray<FTriangleID> Tris;
		for (const FTriangleID T : MD.Triangles().GetElementIDs())
			Tris.Add(T);
		TArray<int32> Parent;
		for (int32 i = 0; i < Tris.Num(); ++i)
			Parent.Add(i);
		auto Root = [&](int32 i) {
			while (Parent[i] != i)
				i = Parent[i] = Parent[Parent[i]];
			return i;
		};
		TMap<FIntVector, int32> CornerOwner;
		for (int32 i = 0; i < Tris.Num(); ++i)
			for (const FVertexInstanceID VI : MD.GetTriangleVertexInstances(Tris[i])) {
				const FVector2f UV = UVs.Get(VI, 1) * 1048576.f;
				const FIntVector Key(MD.GetVertexInstanceVertex(VI).GetValue(), FMath::RoundToInt(UV.X), FMath::RoundToInt(UV.Y));
				if (const int32* Other = CornerOwner.Find(Key))
					Parent[Root(i)] = Root(*Other);
				else
					CornerOwner.Add(Key, i);
			}
		constexpr int32 None = -1, Shared = -2;
		TArray<int32> Chart;
		Chart.Init(None, Res * Res);
		TArray<uint8> Centres;
		Centres.SetNumZeroed(Res * Res);
		TArray<FVector3f> Facing; // the first face touching a texel
		Facing.SetNumZeroed(Res * Res);
		TArray<bool> Turned;
		Turned.SetNumZeroed(Res * Res);
		const TVertexAttributesConstRef<FVector3f> Positions = FStaticMeshConstAttributes(MD).GetVertexPositions();
		for (int32 i = 0; i < Tris.Num(); ++i) {
			const TArrayView<const FVertexInstanceID> VI = MD.GetTriangleVertexInstances(Tris[i]);
			const FVector2f A = UVs.Get(VI[0], 1) * Res, B = UVs.Get(VI[1], 1) * Res, C = UVs.Get(VI[2], 1) * Res;
			const float Area = (B - A) ^ (C - A);
			if (FMath::Abs(Area) < UE_SMALL_NUMBER)
				continue;
			const int32 Id = Root(i);
			auto At = [&](int32 k) { return Positions[MD.GetVertexInstanceVertex(VI[k])]; };
			const FVector3f Normal = ((At(1) - At(0)) ^ (At(2) - At(0))).GetSafeNormal();
			const int32 X0 = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X)), 0, Res - 1), X1 = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.X, B.X, C.X)), 0, Res - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y)), 0, Res - 1), Y1 = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.Y, B.Y, C.Y)), 0, Res - 1);
			for (int32 Y = Y0; Y <= Y1; ++Y)
				for (int32 X = X0; X <= X1; ++X) {
					if (!TouchesTexel(A, B, C, X, Y))
						continue;
					int32& Owner = Chart[Y * Res + X];
					Owner = Owner == None || Owner == Id ? Id : Shared;
					FVector3f& Face = Facing[Y * Res + X];
					if (Face.IsZero())
						Face = Normal;
					else if ((Face | Normal) < 0.9f)
						Turned[Y * Res + X] = true;
					const FVector2f P(X + 0.5f, Y + 0.5f);
					// strictly inside: a centre on an edge two triangles share (a quad's diagonal) is neither's
					if (((B - P) ^ (C - P)) / Area > 1e-4f && ((C - P) ^ (A - P)) / Area > 1e-4f && ((A - P) ^ (B - P)) / Area > 1e-4f)
						Centres[Y * Res + X] = FMath::Min<uint8>(Centres[Y * Res + X] + 1, 2);
				}
		}
		int32 Used = 0, Overlapped = 0;
		for (int32 i = 0; i < Chart.Num(); ++i) {
			Used += Chart[i] != None;
			Overlapped += Chart[i] == Shared || Centres[i] > 1 || (Centres[i] == 0 && Turned[i]);
		}
		return Used ? float(Overlapped) / Used : 0.f;
	}
} // namespace

void FDFImporter::CheckLightmaps(const TArray<FString>& Maps) {
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FString> Changed;
	for (const FString& Map : Maps) {
		AR.ScanPathsSynchronous({ Packs.MapDataRoot(Map) }, true);
		TArray<FAssetData> Data;
		AR.GetAssetsByPath(FName(*Packs.MapDataRoot(Map)), Data, true);
		int32 Lit = 0, Over = 0;
		for (const FAssetData& A : Data) {
			if (A.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName())
				continue;
			const UStaticMesh* SM = Cast<UStaticMesh>(A.GetAsset());
			const FMeshDescription* MD = SM ? SM->GetMeshDescription(0) : nullptr;
			if (!MD || SM->GetLightMapCoordinateIndex() != 1 || FStaticMeshConstAttributes(*MD).GetVertexInstanceUVs().GetNumChannels() < 2 || A.AssetName.ToString().StartsWith(TEXT("Sky_")))
				continue;
			++Lit;
			const float Shared = LightmapOverlap(*MD, SM->GetLightMapResolution());
			if (Shared <= 0.02f)
				continue;
			++Over;
			UE_LOG(LogDFEditor, Display, TEXT("LightmapCheck %s: %s at %d, %.0f %% texels shared"), *Map, *A.AssetName.ToString(), SM->GetLightMapResolution(), 100.f * Shared);
		}
		UE_LOG(LogDFEditor, Display, TEXT("LightmapCheck %s: %d of %d lit meshes over 2 %%"), *Map, Over, Lit);
		if (Over)
			Changed.Add(Map);
	}
	UE_LOG(LogDFEditor, Display, TEXT("LightmapCheck: %d of %d maps to rebuild: %s"), Changed.Num(), Maps.Num(), *FString::Join(Changed, TEXT(" ")));
}

UStaticMesh* FDFImporter::BuildStaticMesh(const FString& JsonFile, const FString& AssetPath) {
	TSharedPtr<FJsonObject> J = LoadJson(JsonFile);
	if (!J)
		return nullptr;
	TArray<uint8> Bin;
	if (!FFileHelper::LoadFileToArray(Bin, *(FPaths::GetPath(JsonFile) / J->GetStringField(TEXT("bin"))))) {
		UE_LOG(LogDFEditor, Error, TEXT("Missing mesh data for %s"), *JsonFile);
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
	if (Bin.Num() < NumVerts * Stride * 4 + NumIndices * 4) {
		UE_LOG(LogDFEditor, Error, TEXT("Truncated mesh data for %s"), *JsonFile);
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
	UVs.SetNumChannels(2); // 1 is the lightmap's

	// Weld positions so lightmap UV generation sees connected surfaces
	TMap<FVector3f, FVertexID> Welded;
	TArray<FVertexID> VertexOf;
	VertexOf.SetNum(NumVerts);
	for (int32 i = 0; i < NumVerts; ++i) {
		const float* P = V + i * Stride;
		const FVector3f Pos(P[0], P[1], P[2]);
		FVertexID* VID = Welded.Find(Pos);
		if (!VID) {
			const FVertexID New = MD.CreateVertex();
			Positions[New] = Pos;
			VID = &Welded.Add(Pos, New);
		}
		VertexOf[i] = *VID;
	}
	// Lightmap charts from a box projection: each triangle along its dominant axis, neighbours on
	// the same side sharing coordinates (one chart). Packed below against the texture UVs' charts.
	auto Pos = [&](uint32 I) { return FVector3f(V[I * Stride], V[I * Stride + 1], V[I * Stride + 2]); };
	auto BoxAxis = [&](uint32 A, uint32 B, uint32 C) {
		const FVector3f N = (Pos(B) - Pos(A)) ^ (Pos(C) - Pos(A));
		const FVector3f M = N.GetAbs();
		const int32 Axis = M.X >= M.Y && M.X >= M.Z ? 0 : M.Y >= M.Z ? 1
																	 : 2;
		return Axis * 2 + (N[Axis] < 0.f ? 1 : 0);
	};
	auto MakeInstance = [&](uint32 Index, int32 Side) {
		const float* P = V + Index * Stride;
		const FVertexInstanceID VI = MD.CreateVertexInstance(VertexOf[Index]);
		Normals[VI] = FVector3f(P[3], P[4], P[5]);
		UVs.Set(VI, 0, FVector2f(P[6], P[7]));
		const int32 Axis = Side / 2;
		const FVector2f Flat(Axis == 0 ? P[1] : P[0], Axis == 2 ? P[1] : P[2]);
		UVs.Set(VI, 1, FVector2f(Side & 1 ? -Flat.X : Flat.X, Flat.Y) / 100.f); // metres
		// alpha 1 = this zone's ambient in RGB; alpha 0 = use the map's main ambient (props)
		Colors[VI] = bZoneColors ? FVector4f(P[8], P[9], P[10], 1.f) : FVector4f(0.f, 0.f, 0.f, 0.f);
		return VI;
	};

	const TArray<TSharedPtr<FJsonValue>>& Materials = J->GetArrayField(TEXT("materials"));
	TArray<FStaticMaterial> StaticMaterials;
	for (const TSharedPtr<FJsonValue>& Sec : J->GetArrayField(TEXT("sections"))) {
		const TSharedPtr<FJsonObject> S = Sec->AsObject();
		const int32 MatIndex = S->GetIntegerField(TEXT("material"));
		const int32 First = S->GetIntegerField(TEXT("first_index"));
		const int32 NumTris = S->GetIntegerField(TEXT("num_triangles"));
		const FString Key = Materials.IsValidIndex(MatIndex) ? Materials[MatIndex]->AsString() : FString();
		const FName Slot(*FString::Printf(TEXT("M%d"), StaticMaterials.Num()));
		const FPolygonGroupID PG = MD.CreatePolygonGroup();
		SlotNames[PG] = Slot;
		StaticMaterials.Add(FStaticMaterial(bRender ? GetMaterial(Key) : nullptr, Slot, Slot));
		// every corner its own instance: the lightmap packer writes each triangle corner's UV to
		// its instance, so one shared by triangles of two charts (touching at a point) ended up
		// with the last chart's UV and those triangles stretched across the lightmap (mf-western's
		// buildings: 65 % of their texels shared); the build welds the identical ones again
		for (int32 t = 0; t < NumTris; ++t) {
			const uint32 A = Idx[First + t * 3], B = Idx[First + t * 3 + 1], C = Idx[First + t * 3 + 2];
			// no area (three points in a line): the build drops it anyway, but first the lightmap
			// packer would scale its chart by world length over (no) UV length: 100 x the texels,
			// overflowing the lightmap onto everything else (mf-western's buildings: 65 % shared)
			if (((Pos(B) - Pos(A)) ^ (Pos(C) - Pos(A))).SizeSquared() < 1e-4f)
				continue;
			const int32 Side = BoxAxis(A, B, C);
			// mfmesh triangles wind cross(v1-v0, v2-v0) along the normal; UE wants the opposite
			TArray<FVertexInstanceID, TInlineAllocator<3>> Tri = { MakeInstance(A, Side), MakeInstance(C, Side), MakeInstance(B, Side) };
			MD.CreateTriangle(PG, Tri);
		}
	}

	FString PackagePath, Name;
	AssetPath.Split(TEXT("/"), &PackagePath, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	bool bCreated = false;
	UStaticMesh* SM = FindOrCreateAsset<UStaticMesh>(PackagePath, Name, bCreated);
	SM->PreEditChange(nullptr);

	// Lightmap UVs: the packer lays out charts found in a source channel and keeps overlaps within
	// them. Texture UVs suit BSP (one planar mapping per face) but fold and tile on meshes (60-98 %
	// of a prop's texels shared, baking blotchy); the box projection suits meshes but can fold
	// where thin parts stack. Both are packed, the one sharing fewer texels stays.
	int32 LightmapSize = FMath::Max(16, LightmapRes);
	if (bRender) {
		FOverlappingCorners Corners;
		FStaticMeshOperations::FindOverlappingCorners(Corners, MD, THRESH_POINTS_ARE_SAME);
		const ELightmapUVVersion Version = static_cast<ELightmapUVVersion>(SM->GetLightmapUVVersion());
		FMeshDescription TexSource = MD;
		TVertexInstanceAttributesRef<FVector2f> TexUVs = FStaticMeshAttributes(TexSource).GetVertexInstanceUVs();
		for (const FVertexInstanceID VI : TexSource.VertexInstances().GetElementIDs())
			TexUVs.Set(VI, 1, TexUVs.Get(VI, 0));
		const FMeshDescription BoxSource = MD;
		// A mesh of many small pieces may not pack at its share of the map's texels (every chart
		// needs its padding): the packer then leaves the source UVs, all on top of each other
		// (mf-western's buildings: 70 %). Twice the resolution until a layout shares under 2 %.
		float Shared = 2.f;
		const TCHAR* From = TEXT("");
		bool bPacked = false;
		for (int32 Res = LightmapSize; Res <= FMath::Max(LightmapSize, 1024) && Shared > 0.02f; Res *= 2) {
			FMeshDescription Box = BoxSource, Tex = TexSource;
			const bool bBox = FStaticMeshOperations::CreateLightMapUVLayout(Box, 1, 1, Res, Version, Corners);
			const bool bTex = FStaticMeshOperations::CreateLightMapUVLayout(Tex, 1, 1, Res, Version, Corners);
			const float BoxShared = bBox ? LightmapOverlap(Box, Res) : 1.f, TexShared = bTex ? LightmapOverlap(Tex, Res) : 1.f;
			// texture UVs only by a clear margin: Lightmass found a texel each of mf-western's logs
			// packed from them shared (95 %), the box projection none, where both measure 1 % here
			const bool bFromTex = TexShared + 0.01f < BoxShared;
			if ((bFromTex ? TexShared : BoxShared) < Shared) {
				Shared = bFromTex ? TexShared : BoxShared;
				From = bFromTex ? TEXT("texture UVs") : TEXT("box projection");
				bPacked = bFromTex ? bTex : bBox;
				MD = MoveTemp(bFromTex ? Tex : Box);
				LightmapSize = Res;
			}
		}
		UE_LOG(LogDFEditor, Log, TEXT("%s: lightmap UVs from %s at %d (%.0f %% texels shared%s)"), *Name, From, LightmapSize, 100.f * Shared, bPacked ? TEXT("") : TEXT(", not packed"));
	}
	SM->GetStaticMaterials() = StaticMaterials;
	SM->SetNumSourceModels(1);
	FStaticMeshSourceModel& Src = SM->GetSourceModel(0);
	Src.BuildSettings.bRecomputeNormals = false;
	Src.BuildSettings.bRecomputeTangents = true;
	Src.BuildSettings.bUseMikkTSpace = true;
	Src.BuildSettings.bRemoveDegenerates = true;
	Src.BuildSettings.bUseFullPrecisionUVs = true; // world UVs tile far past 0..1
	Src.BuildSettings.bGenerateLightmapUVs = false; // laid out above
	Src.BuildSettings.MinLightmapResolution = LightmapSize;
	Src.BuildSettings.bBuildReversedIndexBuffer = false;
	SM->CreateMeshDescription(0, MoveTemp(MD));
	SM->CommitMeshDescription(0);
	SM->SetLightMapResolution(bRender ? LightmapSize : 4);
	SM->SetLightMapCoordinateIndex(bRender ? 1 : 0);
	SM->CreateBodySetup();
	UBodySetup* Body = SM->GetBodySetup();
	Body->CollisionTraceFlag = Collision == TEXT("complex") ? CTF_UseComplexAsSimple : CTF_UseDefault;
	// UE1's BSP was solid behind a face, a mesh only has the face: one-sided, a wall wound the
	// other way let you walk in and not back out (mf-western's blue base tower)
	Body->bDoubleSidedGeometry = true;
	if (Collision == TEXT("none"))
		Body->bNeverNeedsCookedCollisionData = true;
	if (Collision == TEXT("hull")) {
		// one smooth shape round the mesh: a ramp of rungs walks like a ramp
		FKConvexElem Hull;
		for (int32 i = 0; i < NumVerts; ++i)
			Hull.VertexData.Add(FVector(V[i * Stride], V[i * Stride + 1], V[i * Stride + 2]));
		Hull.UpdateElemBox();
		Body->AggGeom.ConvexElems.Reset();
		Body->AggGeom.ConvexElems.Add(Hull);
		Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		Body->InvalidatePhysicsData();
		Body->CreatePhysicsMeshes();
	}
	SM->Build(true);
	SM->PostEditChange();
	SaveAsset(SM);
	return SM;
}

UStaticMesh* FDFImporter::LoadOrBuildMesh(const FString& MapDir, const FString& MeshRel, const FString& MapName) {
	const FString CacheKey = MapName + TEXT(":") + MeshRel;
	if (UStaticMesh** Found = MeshCache.Find(CacheKey))
		return *Found;
	FString Sub, Name;
	MeshRel.Split(TEXT("/"), &Sub, &Name);
	const FString Folder = Sub == TEXT("geo") ? TEXT("Geo") : TEXT("Meshes");
	const FString AssetPath = FString::Printf(TEXT("%s/%s/%s"), *Packs.MapDataRoot(MapName), *Folder, *SafeName(Name));
	UStaticMesh* SM = BuildStaticMesh(MapDir / (MeshRel + TEXT(".mfmesh.json")), AssetPath);
	MeshCache.Add(CacheKey, SM);
	return SM;
}

bool FDFImporter::RefreshMapCollision(const FString& MapName) {
	const FString MapDir = WorkDir / TEXT("Maps") / MapName;
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *MapDir, TEXT("*.mfmesh.json"), true, false);
	int32 Errors = 0, Updated = 0;
	for (const FString& File : Files) {
		const TSharedPtr<FJsonObject> J = LoadJson(File);
		if (!J) {
			++Errors;
			continue;
		}
		FString Collision;
		J->TryGetStringField(TEXT("collision"), Collision);
		if (Collision != TEXT("complex"))
			continue;
		const bool bDoubleSided = true; // as BuildStaticMesh: every face collides from both sides
		FString Rel = File;
		FPaths::MakePathRelativeTo(Rel, *(MapDir + TEXT("/")));
		const FString Folder = Rel.StartsWith(TEXT("geo/")) ? TEXT("Geo") : TEXT("Meshes");
		const FString Name = SafeName(FPaths::GetCleanFilename(File).LeftChop(12)); // .mfmesh.json
		const FString Path = Packs.MapDataRoot(MapName) / Folder / Name;
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *(Path + TEXT(".") + Name));
		if (!Mesh || !Mesh->GetBodySetup()) {
			++Errors;
			continue;
		}
		UBodySetup* Body = Mesh->GetBodySetup();
		if (Body->bDoubleSidedGeometry == bDoubleSided)
			continue;
		Body->bDoubleSidedGeometry = bDoubleSided;
		Body->InvalidatePhysicsData();
		Body->CreatePhysicsMeshes();
		SaveAsset(Mesh);
		++Updated;
	}
	UE_LOG(LogDFEditor, Display, TEXT("Collision %s: %d updated, %d errors"), *MapName, Updated, Errors);
	return Errors == 0 && Files.Num() > 0;
}
