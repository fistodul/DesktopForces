#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MFEffectActor.generated.h"

/**
 * A server's explosion seen everywhere (RageEffect.AddExplosionServer / AddVehicleExplosionServer:
 * a replicated ExplosionGlobal / VehicleExplosion): spawned on the server, it shows the effect
 * and plays the bang on every machine that gets it, then goes. For code that runs on the server
 * only (mods' turrets, mines, missiles); multicasts do the same for actors that stay a moment.
 */
UCLASS(NotBlueprintable)
class DESKTOPFORCES_API AMFEffectActor : public AActor
{
	GENERATED_BODY()

public:
	AMFEffectActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: Effects/P_<Effect> at Scale (MF Size x 0.4); bVehicleExplosion: VehicleExplosion's
	 *  after-fires too; Sound ("Sounds/MiscSFX/Explode") heard out to SoundRadiusUU. */
	static void Spawn(UWorld* World, FName Effect, const FVector& At, float Scale, bool bVehicleExplosion = false,
		const FString& Sound = TEXT("Sounds/MiscSFX/Explode"), float SoundRadiusUU = 8000.f, float SoundVolume = 1.f);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(Replicated)
	FName Effect;
	UPROPERTY(Replicated)
	float Scale = 1.f;
	UPROPERTY(Replicated)
	bool bVehicleExplosion = false;
	UPROPERTY(Replicated)
	FString Sound;
	UPROPERTY(Replicated)
	float SoundRadiusUU = 8000.f;
	UPROPERTY(Replicated)
	float SoundVolume = 1.f;
};
