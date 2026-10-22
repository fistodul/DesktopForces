#include "Weapons/Molotov/Molotov.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "Molotov"

namespace MolotovLocal
{
	void SpawnFire(AActor* From, const FVector& Location)
	{
		FActorSpawnParameters SP;
		SP.Instigator = From->GetInstigator();
		SP.Owner = From->GetInstigator();
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		From->GetWorld()->SpawnActor<AMolotovFire>(Location, FRotator::ZeroRotator, SP);
	}
}

UDFDamage_Fire::UDFDamage_Fire()
{
	DeathVerb = LOCTEXT("SetOnFire", "set on fire");
	VehicleDamageScale = 0.125f;   // burning: like any other non-explosive hit (changeDamage)
}

AMolotovFire::AMolotovFire()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMolotovFire::BeginPlay()
{
	Super::BeginPlay();
	SetLifeSpan(BurnDuration + FMath::FRand());
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(BurnTimer, this, &AMolotovFire::Burn, 0.1f, true);
	}
	// TODO: flame particles (Niagara) + crackle loop once effects are converted
}

void AMolotovFire::Burn()
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
			FDamageEvent Ev(UDFDamage_Fire::StaticClass());
			A->TakeDamage(float(Damage), Ev, GetInstigatorController(), this);
		}
	}
}

AMolotovBottle::AMolotovBottle()
{
	Damage = 44.f;
	DamageType = UDFDamage_Fire::StaticClass();
}

void AMolotovBottle::Explode(const FVector& Location, const FVector& Normal)
{
	if (!bExploded)
	{
		MolotovLocal::SpawnFire(this, Location);
	}
	Super::Explode(Location, Normal);
}

AMolotovImpact::AMolotovImpact()
{
	Damage = 40.f;
	DamageType = UDFDamage_Fire::StaticClass();
}

void AMolotovImpact::Explode(const FVector& Location, const FVector& Normal)
{
	if (!bExploded)
	{
		MolotovLocal::SpawnFire(this, Location);
	}
	Super::Explode(Location, Normal);
}

AMolotov::AMolotov()
{
	DisplayName = LOCTEXT("Molotov", "Molotov Cocktail");
	MaxClipAmmo = 2;
	bAddToLoadout = true;   // shows up in every loadout crate while the plugin is enabled
	ProjectileClass = AMolotovBottle::StaticClass();
	AltProjectileClass = AMolotovImpact::StaticClass();
	// Icon converted from the mod's own art (SourceArt/Mods/Icons/MolotovIcon.bmp)
	HUDIcon = TEXT("/Game/MF/Mods/Icons/MolotovIcon");
	HUDIconRect = FIntRect(0, 0, 64, 64);
}

#undef LOCTEXT_NAMESPACE
