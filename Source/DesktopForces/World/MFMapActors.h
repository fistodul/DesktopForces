#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Info.h"
#include "Core/MFTypes.h"
#include "MFMapActors.generated.h"

class USoundBase;
class UAudioComponent;

/** Per-map settings (MF LevelInfo + LevelSummary). One per map. */
UCLASS()
class DESKTOPFORCES_API AMFMapInfo : public AInfo
{
	GENERATED_BODY()

public:
	AMFMapInfo();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	FString Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	FString Author;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	FString IdealPlayerCount;

	/** Original map package name (e.g. mf-warehouse). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MF")
	FString SourceMap;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	TObjectPtr<USoundBase> Music;

	/** MF MiniMapInfo: top-down map texture (64 uu per pixel) and the world box it covers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	TSoftObjectPtr<class UTexture2D> MiniMap;

	/** World X of the texture's left edge, Y of its top, X right, Y bottom (cm; MF MapLeft/Top/Right/Bottom). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	FVector4 MiniMapBounds = FVector4(0, 0, 0, 0);

	/** MF "Fog end is the draw distance": cull distance for the whole map (cm, 0 = unlimited). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	float DrawDistance = 0.f;

	/**
	 * Ambient light of the map's main zone (MF ZoneInfo ambient). Mostly baked (Lightmass sky
	 * colour + bounces); the unshadowed floor in the lit materials is UMFGameSettings::AmbientFloor
	 * x the player's brightness (MPC_MF.AmbientScale) x the zone ambient in the world's vertex
	 * colour, or x this for props and actors (MPC_MF.Ambient).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	FLinearColor Ambient = FLinearColor::Black;

	/** Push the current map's ambient floor into MPC_MF (map start, brightness change). */
	static void ApplyAmbient(UWorld* World);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComponent;
};

/**
 * An original MF actor with no UE implementation yet (flag bases, trailer parks,
 * holdout timers, vehicle generators, road points ...). The map builder keeps them so
 * future game modes find their data in the map: class name, team and raw MF properties.
 * Costs nothing at runtime (no tick, no collision, hidden).
 */
UCLASS()
class DESKTOPFORCES_API AMFLegacyActor : public AActor
{
	GENERATED_BODY()

public:
	AMFLegacyActor();

	/** Original UE1 class, e.g. "RageFlagBase". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MF")
	FName MFClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MF")
	FName MFName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MF")
	uint8 Team = MF::NoTeam;

	/** Raw T3D properties of the original actor (values as text, MF units). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MF")
	TMap<FString, FString> Properties;

	/** Find all legacy actors of an MF class in a world. */
	static TArray<AMFLegacyActor*> FindAll(const UWorld* World, FName Class);

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
class DESKTOPFORCES_API AMFZoneBox : public AActor
{
	GENERATED_BODY()

public:
	AMFZoneBox();
	bool IsInside(const FVector& Point) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MF")
	TObjectPtr<class UBoxComponent> Volume;
};

/**
 * The map's sky (MF SkyZoneInfo): UE1 rendered the sky zone, seen from this point with the
 * player's view rotation, through sky-window polygons (PF_FakeBackdrop). Here a cube capture
 * from this point fills RT_MF_Sky once when the map starts; the sky material on those
 * polygons samples it by view direction. One capture at load, nothing per frame.
 */
UCLASS()
class DESKTOPFORCES_API AMFSkyCapture : public AActor
{
	GENERATED_BODY()

public:
	AMFSkyCapture();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MF")
	TObjectPtr<class USceneCaptureComponentCube> Capture;
};
