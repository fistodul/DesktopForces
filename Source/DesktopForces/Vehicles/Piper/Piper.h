#pragma once

#include "CoreMinimal.h"
#include "Vehicles/DFVehicle.h"
#include "Piper.generated.h"

/**
 * MF-mods Piper (Gregaras; plane model from the Airport map; MPL-2.0 with his agreement):
 * on the ground a car on three wheels (handling PIPER: 1000 kg, 50 mph). Throttle + Jump above
 * takeoffSpeed (10 m/s) while rolling flat lifts it off; then it flies along its nose at its own
 * speed (piper state Driving.Tick, m/s per 60 Hz tick): throttle adds 0.1 a tick up to 30, none
 * loses 1 % a tick; climbing bleeds speed, diving gains it (to 35 / 45 with throttle). Jump /
 * Crouch pitch it, steering yaws and rolls, the fire buttons roll; under 15 m/s (10 going up or
 * down) the nose drops. Touching down on its wheels lands it, anything else crashes it (no
 * flying until it is back on its wheels); water crashes it too.
 */
UCLASS()
class DESKTOPFORCES_API APiper : public ADFVehicle
{
	GENERATED_BODY()

public:
	APiper();
	virtual void Tick(float DeltaSeconds) override;
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation,
		FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void SimulateDrive(float Dt) override;

	/** bodyModels: A4 (BodyMesh), A1, A3 paint, picked at random. */
	UPROPERTY(ReplicatedUsing = OnRep_Livery)
	uint8 Livery = 0;
	UFUNCTION()
	void OnRep_Livery();
	TArray<FString> Liveries;

	UPROPERTY(VisibleAnywhere, Category = "Piper")
	TObjectPtr<UStaticMeshComponent> Propeller;
	float PropellerRoll = 0.f;
	/** soundPitch 15 -> 60 at 0.15 a tick: the propeller turns (pitch - 15) x 100 a tick. */
	float EnginePitch = 15.f;

	bool bAirborne = false;
	bool bCrashed = false;
	/** smallSpeed (m/s): the speed it flies at. */
	float AirSpeed = 0.f;
	/** ignoreCollision: just took off, until its wheels are off the ground. */
	bool bIgnoreCollision = false;
	bool WheelsDown() const;
};

/** piperTs: the two-seat Piper (cabin model, a passenger facing back). */
UCLASS()
class DESKTOPFORCES_API APiperTs : public APiper
{
	GENERATED_BODY()

public:
	APiperTs();
};
