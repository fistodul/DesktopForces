#include "Mods/Molotov/MFMolotov.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MFModMolotov"

namespace
{
	void SpawnFire(AActor* From, const FVector& Location)
	{
		FActorSpawnParameters SP;
		SP.Instigator = From->GetInstigator();
		SP.Owner = From->GetInstigator();
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		From->GetWorld()->SpawnActor<AMFMolotovFire>(Location, FRotator::ZeroRotator, SP);
	}
}

UMFDamage_Fire::UMFDamage_Fire()
{
	DeathVerb = LOCTEXT("SetOnFire", "set on fire");
}

AMFMolotovFire::AMFMolotovFire()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMFMolotovFire::BeginPlay()
{
	Super::BeginPlay();
	SetLifeSpan(BurnDuration + FMath::FRand());
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(BurnTimer, this, &AMFMolotovFire::Burn, 0.1f, true);
	}
	// TODO: flame particles (Niagara) + crackle loop once effects are converted
}

void AMFMolotovFire::Burn()
{
	// MolotovFire.Timer: accumulate fractional damage, deal whole points
	Accumulator += DamagePerTick;
	const int32 Damage = FMath::FloorToInt(Accumulator);
	if (Damage < 1)
	{
		return;
	}
	Accumulator -= Damage;
	const FVector Origin = GetActorLocation();
	TArray<FOverlapResult> Hits;
	GetWorld()->OverlapMultiByObjectType(Hits, Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(FireRadius));
	TSet<AActor*> Done;
	for (const FOverlapResult& H : Hits)
	{
		AActor* A = H.GetActor();
		if (!A || Done.Contains(A))
		{
			continue;
		}
		Done.Add(A);
		const FVector D = A->GetActorLocation() - Origin;
		if (FMath::Abs(D.Z) > FireRadius * 0.5f)
		{
			continue;
		}
		FHitResult Block;
		const bool bVisible = !GetWorld()->LineTraceSingleByChannel(Block, Origin, A->GetActorLocation(), ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(MolotovLOS), false, A));
		if (bVisible || D.Size() <= FireRadius * 0.5f)
		{
			FDamageEvent Ev(UMFDamage_Fire::StaticClass());
			A->TakeDamage(float(Damage), Ev, GetInstigatorController(), this);
		}
	}
}

AMFProjectile_Molotov::AMFProjectile_Molotov()
{
	Damage = 44.f;
	DamageType = UMFDamage_Fire::StaticClass();
}

void AMFProjectile_Molotov::Explode(const FVector& Location, const FVector& Normal)
{
	if (!bExploded)
	{
		SpawnFire(this, Location);
	}
	Super::Explode(Location, Normal);
}

AMFProjectile_MolotovImpact::AMFProjectile_MolotovImpact()
{
	Damage = 40.f;
	DamageType = UMFDamage_Fire::StaticClass();
}

void AMFProjectile_MolotovImpact::Explode(const FVector& Location, const FVector& Normal)
{
	if (!bExploded)
	{
		SpawnFire(this, Location);
	}
	Super::Explode(Location, Normal);
}

AMFWeapon_Molotov::AMFWeapon_Molotov()
{
	DisplayName = LOCTEXT("Molotov", "Molotov Cocktail");
	MaxClipAmmo = 2;
	bAddToLoadout = true;   // shows up in every loadout crate while the plugin is enabled
	ProjectileClass = AMFProjectile_Molotov::StaticClass();
	AltProjectileClass = AMFProjectile_MolotovImpact::StaticClass();
	// Icon converted from the mod's own art (SourceArt/Mods/Icons/MolotovIcon.bmp)
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/MF/Mods/Icons/MolotovIcon.MolotovIcon")));
	HUDIconRect = FIntRect(0, 0, 64, 64);
}

#undef LOCTEXT_NAMESPACE
