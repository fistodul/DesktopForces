#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UStaticMesh;
class UTexture2D;
class USoundWave;
class UWorld;

/**
 * Builds UE assets from the intermediate files written by Tools/mfport
 * (work/out: textures.json + PNGs, Sounds/ WAVs, Maps/<map>/level.json + .mfmesh files).
 *
 * Output layout (all generated, git-ignored):
 *   /Game/MFImported/Textures/<package>/<group>/<name>      Texture2D
 *   /Game/MFImported/Materials/<package>/<group>/MI_<name>_<mode>
 *   /Game/MFImported/Sounds/<package>/<name>                SoundWave
 *   /Game/MFImported/Maps/<map>/Geo|Meshes/<name>           StaticMesh
 *   /Game/MFMaps/Generated/<map>                            the level
 * Master materials (our own content, committed): /Game/MF/Materials/M_MF_<Mode>.
 */
class FMFImporter
{
public:
	explicit FMFImporter(const FString& InWorkDir);

	bool ImportTextures();
	/** Import WAVs under work/out/<SubDir> to /Game/MFImported/<SubDir>. */
	bool ImportSounds(const FString& SubDir = TEXT("Sounds"), bool bLooping = false);
	bool BuildMap(const FString& MapName);

	/** Small blockout map from engine shapes only (no MF assets): /Game/MF/Maps/MF_TestRange. */
	bool BuildTestMap();

	/** Material for an mfport material key "package.group.texture|mode". Created on demand. */
	UMaterialInterface* GetMaterial(const FString& Key);

	/** Build (or rebuild) a static mesh asset from a .mfmesh.json file. */
	UStaticMesh* BuildStaticMesh(const FString& JsonFile, const FString& AssetPath);

	USoundWave* FindSound(const FString& MFPath) const;
	UTexture2D* FindTexture(const FString& Key) const;

	static bool SaveAsset(UObject* Asset);
	static FString TextureAssetPath(const FString& Key);

private:
	UMaterialInterface* GetMasterMaterial(const FString& Mode);
	UStaticMesh* LoadOrBuildMesh(const FString& MapDir, const FString& MeshRel, const FString& MapName);

	FString WorkDir;
	TMap<FString, UMaterialInterface*> MaterialCache;
	TMap<FString, UMaterialInterface*> MasterCache;
	TMap<FString, UStaticMesh*> MeshCache;
};
