#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Info.h"
#include "Core/DFTypes.h"
#include "DFMapActors.generated.h"

class USoundBase;
class UAudioComponent;

/** A UE1 zone's distance fog (ZoneInfo bDistanceFog). */
USTRUCT()
struct FDFZoneFog {
	GENERATED_BODY()

	/** DistanceFogColor (sRGB). */
	UPROPERTY(VisibleAnywhere, Category = "DF")
	FColor Color = FColor::Black;

	/** DistanceFogStart / End (cm): clear before Start, all fog at End. */
	UPROPERTY(VisibleAnywhere, Category = "DF")
	float Start = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	float End = 0.f;
};

/** Per-map settings (MF LevelInfo + LevelSummary). One per map. */
UCLASS()
class DESKTOPFORCES_API ADFMapInfo : public AInfo {
	GENERATED_BODY()

public:
	ADFMapInfo();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DF")
	FString Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DF")
	FString Author;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DF")
	FString IdealPlayerCount;

	/** Original map package name (e.g. mf-warehouse). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF")
	FString SourceMap;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DF")
	TObjectPtr<USoundBase> Music;

	/** MF MiniMapInfo: top-down map texture (64 uu per pixel) and the world box it covers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DF")
	TSoftObjectPtr<class UTexture2D> MiniMap;

	/** World X of the texture's left edge, Y of its top, X right, Y bottom (cm; MF MapLeft/Top/Right/Bottom). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DF")
	FVector4 MiniMapBounds = FVector4(0, 0, 0, 0);

	/**
	 * Ambient light of the map's main zone (MF ZoneInfo ambient). Mostly baked (Lightmass sky
	 * colour + bounces); the unshadowed floor in the lit materials is UDFGameSettings::AmbientFloor
	 * x the player's brightness (MPC_MF.AmbientScale) x the zone ambient in the world's vertex
	 * colour, or x this for props and actors (MPC_MF.Ambient).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DF")
	FLinearColor Ambient = FLinearColor::Black;

	/**
	 * The zones' distance fog: UE1 fogs what a viewer sees by the zone they are in, linearly from
	 * DistanceFogStart to End. Empty in maps without. Which fog a spot has comes from a grid the
	 * converter sampled from the BSP (mflib/actors.py zone_fog): FogCell cm cells from FogOrigin,
	 * FogSize of them, x fastest, run-length coded (FogRunEnds: one past each run's last cell;
	 * FogRunValues: 0 clear, n ZoneFog[n - 1], 255 keep the fog the view has: solid, zone 0).
	 */
	UPROPERTY(VisibleAnywhere, Category = "DF")
	TArray<FDFZoneFog> ZoneFog;
	UPROPERTY(VisibleAnywhere, Category = "DF")
	FVector FogOrigin = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "DF")
	FIntVector FogSize = FIntVector::ZeroValue;
	UPROPERTY(VisibleAnywhere, Category = "DF")
	float FogCell = 300.f;
	UPROPERTY(VisibleAnywhere, Category = "DF")
	TArray<int32> FogRunEnds;
	UPROPERTY(VisibleAnywhere, Category = "DF")
	TArray<uint8> FogRunValues;

	/** The fog at a spot: 0 clear, n ZoneFog[n - 1], 255 unknown (solid, zone 0, off the grid). */
	uint8 FogAt(const FVector& Spot) const;

	/** The map's info, or null (a map without one). */
	static ADFMapInfo* Get(const UWorld* World);
	/** The local view went under water or came up (brightens the ambient floor). */
	static void SetUnderwater(UWorld* World, bool bUnder);
	static bool bUnderwater;
	static constexpr float UnderwaterLight = 1.3f;
	/** Push the current map's ambient floor into MPC_MF (map start, brightness change). */
	static void ApplyAmbient(UWorld* World);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComponent;

	/** The view's fog: UE's height fog, set to the camera's zone (a client's own, not a map actor). */
	UPROPERTY(Transient)
	TObjectPtr<class AExponentialHeightFog> Fog;
	uint8 FogZone = 254;
	FTimerHandle FogTimer;
	/** Which zone the local camera is in; that zone's fog (a few times a second). */
	void UpdateFog();
};

/**
 * An original MF actor with no UE implementation yet (flag bases, trailer parks,
 * holdout timers, vehicle generators, road points ...). The map builder keeps them so
 * future game modes find their data in the map: class name, team and raw MF properties.
 * Costs nothing at runtime (no tick, no collision, hidden).
 */
UCLASS()
class DESKTOPFORCES_API ADFLegacyActor : public AActor {
	GENERATED_BODY()

public:
	ADFLegacyActor();

	/** Original UE1 class, e.g. "RageFlagBase". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF")
	FName MFClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF")
	FName MFName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF")
	uint8 Team = DF::NoTeam;

	/** Raw T3D properties of the original actor (values as text, MF units). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF")
	TMap<FString, FString> Properties;

	/** Find all legacy actors of an MF class in a world. */
	static TArray<ADFLegacyActor*> FindAll(const UWorld* World, FName Class);

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<class UBillboardComponent> Sprite;
#endif
};

/**
 * A box area standing in for an MF zone with a special meaning (Tags: e.g.
 * "CaptainProhibited" = MF CaptainProhibitedZone). No collision; game modes test IsInside.
 */
UCLASS()
class DESKTOPFORCES_API ADFZoneBox : public AActor {
	GENERATED_BODY()

public:
	ADFZoneBox();
	bool IsInside(const FVector& Point) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DF")
	TObjectPtr<class UBoxComponent> Volume;
};

/**
 * The map's sky (MF SkyZoneInfo): UE1 rendered the sky zone, seen from this point with the
 * player's view rotation, through sky-window polygons (PF_FakeBackdrop). Here a cube capture
 * from this point fills RT_MF_Sky once when the map starts; the sky material on those
 * polygons samples it by view direction. One capture at load, nothing per frame.
 */
UCLASS()
class DESKTOPFORCES_API ADFSkyCapture : public AActor {
	GENERATED_BODY()

public:
	ADFSkyCapture();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<class USceneCaptureComponentCube> Capture;
};
