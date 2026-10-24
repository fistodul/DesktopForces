#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/DFTypes.h"
#include "DFProjectile.generated.h"

class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;
class USoundBase;

/**
 * Rockets, grenades, thrown knives (MF: Projectile subclasses).
 *
 * Explosions use MF's HurtRadius rule: full damage at the centre falling off linearly
 * to zero at DamageRadius. Projectiles replicate their movement; explosion effects are
 * multicast. Movement is UProjectileMovementComponent (cheap, no physics bodies).
 */
UCLASS(Abstract)
class DESKTOPFORCES_API ADFProjectile : public AActor {
	GENERATED_BODY()

public:
	ADFProjectile();

	/** Start flying along Dir. Speed <= 0 uses InitialSpeed. MF adds the shooter's velocity along Dir. */
	virtual void Launch(const FVector& Dir, float Speed, const FVector& InheritVelocity);

	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float Damage = 100.f;

	/** 0 = direct hit only. */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float DamageRadius = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float Momentum = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "DF")
	TSubclassOf<UDFDamageType> DamageType;

	/** Explode after this many seconds (0 = never on a timer). */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float FuseTime = 0.f;

	/** Explode when touching world/pawns (rocket, impact grenade); otherwise bounce. */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	bool bExplodeOnImpact = true;

	/** Added to the launch velocity's Z (MF grenades: Velocity.z += 210 uu). */
	UPROPERTY(EditDefaultsOnly, Category = "DF")
	float LaunchUpVelocity = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "DF|FX")
	TObjectPtr<USoundBase> ExplosionSound;

	UPROPERTY(EditDefaultsOnly, Category = "DF|FX")
	FString ClassicExplosionSound;

	/** Fire and smoke (Effects/P_Explosion) at RageEffect.AddExplosion's Size x 0.4; 0: none. */
	UPROPERTY(EditDefaultsOnly, Category = "DF|FX")
	float ExplosionEffectScale = 0.f;

	/** Looping sound on the way ("Package.Name", MF AmbientSound: the rocket's RocketFly). */
	UPROPERTY(EditDefaultsOnly, Category = "DF|FX")
	FString ClassicAmbientSound;

	/** Grenade1.Tick: "Ticking" every second for the first 3 s. */
	UPROPERTY(EditDefaultsOnly, Category = "DF|FX")
	bool bTicking = false;

	/** Converted MF model ("RageWeapons/RocketMesh", mfport props); a small sphere without it. */
	UPROPERTY(EditDefaultsOnly, Category = "DF|FX")
	FString PropMesh;

	/** RageWeapons.SmartRocket: after Delay, steer at Target (UE's projectile homing) while it
	 *  stays ahead of the launch direction. */
	void HomeOn(AActor* Target, float Delay);

protected:
	virtual void BeginPlay() override;
	virtual void LifeSpanExpired() override;
	void UpdateHoming();
	TWeakObjectPtr<AActor> HomingTarget;
	FVector LaunchDir = FVector::ZeroVector;
	FTimerHandle HomingTimer;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TObjectPtr<UProjectileMovementComponent> Movement;

	UFUNCTION()
	void OnStop(const FHitResult& Hit);
	UFUNCTION()
	void OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity);
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);

	/** Server: deal damage and destroy. */
	virtual void Explode(const FVector& Location, const FVector& Normal);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastExplosionFX(FVector_NetQuantize Location);

	bool bExploded = false;
};

/** RageWeapons.Rocket: 1200 uu/s, 135 damage, radius 400. */
UCLASS()
class DESKTOPFORCES_API ARocket : public ADFProjectile {
	GENERATED_BODY()
public:
	ARocket();
};

/** RageWeapons.Grenade1: timed fuse 3 s, bounces (50% damping), 160 damage, radius 400. */
UCLASS()
class DESKTOPFORCES_API AGrenade : public ADFProjectile {
	GENERATED_BODY()
public:
	AGrenade();
};

/** RageWeapons.Grenade2 (alt fire): explodes on impact, 120 damage. */
UCLASS()
class DESKTOPFORCES_API AImpactGrenade : public AGrenade {
	GENERATED_BODY()
public:
	AImpactGrenade();
};

/**
 * RageWeapons.Knife_Thrown: 60 damage (headshot 250 'decapitated'). Sticks in walls, bounces
 * off floors (40 %), stops in whoever it hits; then lies there (state Safe) until its thrower
 * touches it or ReturnTime runs out, and goes back to their knife.
 */
UCLASS()
class DESKTOPFORCES_API AThrownKnife : public ADFProjectile {
	GENERATED_BODY()
public:
	AThrownKnife();
	virtual void Destroyed() override;

	TWeakObjectPtr<class AKnife> OwnerKnife;
	float ReturnTime = 6.f;
	/** Above 80% of a soldier's height: "decapitated" (Knife_Thrown 250). */
	float HeadDamage = 250.f;

protected:
	virtual void BeginPlay() override;
	UFUNCTION()
	void OnKnifeBounce(const FHitResult& Hit, const FVector& ImpactVelocity);
	UFUNCTION()
	void OnKnifeStop(const FHitResult& Hit);
	UFUNCTION()
	void OnKnifeOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);
	/** Dangerous.ProcessTouch: hurt Other once (not the thrower until it has hit a wall). */
	void HitVictim(AActor* Other, const FHitResult& Hit);
	/** state Safe: lying still, waiting for its owner. */
	void BecomeSafe(USceneComponent* StickTo);
	void Return();

	bool bSafe = false;
	bool bCanHitOwner = false;
	FTimerHandle ReturnTimer;
};
