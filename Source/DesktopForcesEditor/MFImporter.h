#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UMaterialInterface;
class UParticleSystem;
class UStaticMesh;
class UTexture2D;
class USoundWave;
class UWorld;

/**
 * Builds UE assets from the intermediate files written by Tools/mfport
 * (work/out: textures.json + PNGs, Sounds/ WAVs, Maps/<map>/level.json + .mfmesh files).
 *
 * Output layout (all generated, git-ignored). <Root> is the content pack (packs.json written by
 * mfport from Tools/mfport/packs.toml): /MobileForcesClassic for anything from the original game
 * (the plugin), /Game/MF/Community for community content whose authors agreed,
 * /Game/MF/Replacements for textures with replacement art:
 *   <Root>/Textures/<package>/<group>/<name>                Texture2D
 *   <Root>/Materials/<package>/<group>/MI_<name>_<mode>
 *   <Root>/Sounds|Music/<package>/<name>                    SoundWave
 *   /MobileForcesClassic/Fonts, /MobileForcesClassic/Props  MF fonts, gameplay meshes
 *   <MapRoot>/<map>, <MapRoot>/<map>_Data/Geo|Meshes/<name> the level and its meshes
 *     (MapRoot: /Game/Maps for core maps, /MobileForcesClassic/Maps for the rest)
 * Master materials (our own content, committed): /Game/MF/Materials/M_MF_<Mode>.
 */

/** Where converted assets go: Tools/mfport/mflib/packs.py (work/out/packs.json). */
struct FMFPacks
{
	FString ClassicRoot = TEXT("/MobileForcesClassic");
	FString CoreRoot = TEXT("/Game/MF/Community");
	FString CoreMapsRoot = TEXT("/Game/Maps");
	FString ReplacedRoot = TEXT("/Game/MF/Replacements");
	TSet<FString> CoreMaps;        // lower case
	TSet<FString> CorePackages;    // lower case
	TSet<FString> Replaced;        // lower case texture keys with replacement art
	bool bRemasterClassic = false; // classic maps use replacement art too

	void Load(const FString& File);
	FString PackageRoot(const FString& Package) const;
	/** "rage_warehouse.metal.x" -> its package's root (the original texture). */
	FString TextureRoot(const FString& Key) const;
	bool HasReplacement(const FString& Key) const { return Replaced.Contains(Key.ToLower()); }
	FString MapPackage(const FString& Map) const;
	FString MapDataRoot(const FString& Map) const { return MapPackage(Map) + TEXT("_Data"); }
	bool IsCoreMap(const FString& Map) const { return CoreMaps.Contains(Map.ToLower()); }
};
class FMFImporter
{
public:
	explicit FMFImporter(const FString& InWorkDir);

	bool ImportTextures();
	/** Import WAVs under work/out/<SubDir>/<package>/... to <package root>/<SubDir>/<package>/... */
	class USoundAttenuation* EnsureSoundAttenuation();
	bool ImportSounds(const FString& SubDir = TEXT("Sounds"), bool bLooping = false);
	bool BuildMap(const FString& MapName);

	/** M_MF_TeamTint: unlit, colour from custom primitive data 0-3 (UMFGameSettings::TeamTintMaterial). */
	void EnsureTeamTintMaterial();
	/**
	 * Our master materials must allow static lighting (UE 5.8: MATUSAGE_StaticLighting),
	 * otherwise every lightmapped surface falls back to the default checker material.
	 */
	void FixMasterMaterialUsage();
	/** M_MF_Sky + RT_MF_Sky: sky windows show the sky zone's cube capture (AMFSkyCapture). */
	UMaterialInterface* EnsureSkyMaterial();
	/** Lit masters: Emissive = BaseColor * MPC_MF.Ambient (map ambient floor, see AMFMapInfo). */
	bool AddAmbientFloor(UMaterial* M);
	class UMaterialParameterCollection* EnsureAmbientCollection();
	/** HUD art / fonts (Rage, RageWeapons, MobileForceFonts packages). */
	static bool IsUITexture(const FString& Key);
	void ImportFonts();


	/**
	 * Our own generated art (PNGs in Tools/artgen/out, committed) -> /Game/MF/Art/<name>, plus
	 * the materials using it (M_MF_Crate: loadout crates with a team-coloured stencil).
	 */
	bool ImportArt();
	/** Gameplay meshes from the game packages (work/out/Props, mfport.py props) -> /MobileForcesClassic/Props/<Package>/<Mesh>. */
	bool ImportProps();
	void EnsureCrateMaterial();
	/** M_MF_Decal / M_MF_DecalQuad and their instances for Tools/artgen's bullet holes and scorch marks. */
	void EnsureDecalMaterials();

	/**
	 * MF sprite particles (MFEffectsImport.cpp, work/out/Effects from mfport.py effects):
	 * flipbook atlases -> <root>/Effects/Textures/..., the scripted effects (explosions,
	 * burning wrecks, smoke puffs, blood) -> /MobileForcesClassic/Effects/P_<Name>.
	 */
	bool ImportEffects();
	/** A particle system from emitter descriptions (mflib/effects.py EMITTER); bLoop: forever. */
	UParticleSystem* BuildParticleSystem(const FString& PackagePath, const FString& Name, const TArray<TSharedPtr<FJsonValue>>& Emitters, bool bLoop);

	/** Small blockout map from engine shapes only (no MF assets): /Game/MF/Maps/MF_TestRange. */
	bool BuildTestMap();
	/** Zone actors (water / pain / current, loadout zones, captain boxes) and the road graph
	 *  into a built map, in place (no rebuild, keeps the bake). */
	bool PatchMapData(const FString& MapName);

	/** Material for an mfport material key "package.group.texture|mode". Created on demand. */
	UMaterialInterface* GetMaterial(const FString& Key);

	/** Build (or rebuild) a static mesh asset from a .mfmesh.json file. */
	UStaticMesh* BuildStaticMesh(const FString& JsonFile, const FString& AssetPath);

	USoundWave* FindSound(const FString& MFPath) const;
	/** The original texture (its replacement if the original is missing). */
	UTexture2D* FindTexture(const FString& Key) const;
	/** What a map of that pack shows: core maps prefer replacement art. */
	UTexture2D* FindTextureFor(const FString& Key, bool bCoreMap) const;
	/** Point existing core material instances at replacement art (no map rebuild needed). */
	void RepointCoreMaterials();

	static bool SaveAsset(UObject* Asset);
	static FString TextureAssetPath(const FString& Key);
	static FString ReplacementAssetPath(const FString& Key);

	/** The pack routing of this import (loaded from work/out/packs.json). */
	static FMFPacks Packs;

private:
	UMaterialInterface* GetMasterMaterial(const FString& Mode);
	/** M_MF_Particle_add / _sub / _alpha and their instances per flipbook. */
	UMaterialInterface* GetParticleMaster(const FString& Draw);
	UMaterialInterface* GetParticleMaterial(const FString& Key, const FString& Draw);
	bool LoadEffects();
	/** A map's ParticleEffect (level.json particle_effect): <map>_Data/Effects/P_<crc of its emitters>. */
	UParticleSystem* GetMapEffect(const FString& MapName, const TArray<TSharedPtr<FJsonValue>>& Emitters);
	TMap<FString, UParticleSystem*> MapEffectCache;
	FString FlipbookAssetPath(const FString& Key) const;
	TSharedPtr<FJsonObject> EffectsManifest;
	TMap<FString, TSharedPtr<FJsonObject>> Flipbooks;
	UStaticMesh* LoadOrBuildMesh(const FString& MapDir, const FString& MeshRel, const FString& MapName);

	FString WorkDir;
	/** BuildMap of a core map: materials go to the core root (see GetMaterial). */
	bool bBuildingCoreMap = false;
	TMap<FString, UMaterialInterface*> MaterialCache;
	TMap<FString, UMaterialInterface*> MasterCache;
	TMap<FString, UStaticMesh*> MeshCache;
};
