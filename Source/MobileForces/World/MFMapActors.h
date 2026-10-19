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
class MOBILEFORCES_API AMFMapInfo : public AInfo
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

	/** MF "Fog end is the draw distance": cull distance for the whole map (cm, 0 = unlimited). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MF")
	float DrawDistance = 0.f;

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
class MOBILEFORCES_API AMFLegacyActor : public AActor
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
