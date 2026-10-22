#include "Weapons/TripLasers/TripLasers.h"
#include "Player/DFCharacter.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "TripLasers"

namespace TripLasersLocal
{
	/** MF GetDamageInstigator: in team games the planter only gets the kill while still on the planting team. */
	AController* Credit(const AActor* Device, uint8 PlacedTeam)
	{
		const ADFCharacter* Planter = Cast<ADFCharacter>(Device->GetInstigator());
		if (!Planter || (PlacedTeam != DF::NoTeam && Planter->GetTeam() != PlacedTeam))
		{
			return nullptr;
		}
		return Planter->GetController();
	}
}

UDFDamage_TripLaser::UDFDamage_TripLaser()
{
	DeathVerb = LOCTEXT("Disintegrated", "disintegrated");
	VehicleDamageScale = 0.125f;   // a burning beam: like any other non-explosive hit (changeDamage)
}

// --- planted ------------------------------------------------------------------------------

ATripLaser::ATripLaser()
{
	InitialLifeSpan = 480.f;
	MaxLaserDistance = 3375.f;   // MF LaserMaxDistance 1500 uu
}

void ATripLaser::Arm()
{
	// TripLaser.Timer: the beam runs through friends to the first other thing, or into the open
	const FVector Start = GetActorLocation();
	BeamEnd = Start + GetActorForwardVector() * MaxLaserDistance;
	LaserEnd = BeamEnd;
	bAnchored = false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFTripLaserArm), false, this);
	FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
	for (int32 i = 0; i < 16; ++i)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByObjectType(Hit, Start, BeamEnd, Objects, Params))
		{
			break;
		}
		if (IsFriendlyTo(Hit.GetActor()))
		{
			Params.AddIgnoredActor(Hit.GetActor());
			continue;
		}
		LaserEnd = Hit.ImpactPoint;
		bAnchored = true;
		break;
	}
	UpdateBeam();
	GetWorldTimerManager().SetTimer(Timer, this, &ATripLaser::CheckBeam, 0.05f, true);
}

void ATripLaser::CheckBeam()
{
	// state Active.Tick: the first thing along the beam; an enemy gets burnt and the laser is spent
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFTripLaserBeam), false, this);
	FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
	if (!GetWorld()->LineTraceSingleByObjectType(Hit, GetActorLocation(), BeamEnd, Objects, Params))
	{
		return;
	}
	ADFCharacter* C = Cast<ADFCharacter>(Hit.GetActor());
	if (C && C->IsAlive() && !IsFriendlyTo(C))
	{
		FDamageEvent Ev(UDFDamage_TripLaser::StaticClass());
		C->TakeDamage(bAnchored ? 280.f : 185.f, Ev, TripLasersLocal::Credit(this, Team), this);
		Destroy();
	}
}

float ATripLaser::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (HasAuthority() && Damage > 0.f)
	{
		Destroy();   // breaks, no explosion
	}
	return Damage;
}

// --- thrown -----------------------------------------------------------------------------------

ATripLaserMine::ATripLaserMine()
{
	InitialLifeSpan = 480.f;
}

void ATripLaserMine::BeginPlay()
{
	Super::BeginPlay();
	if (const ADFCharacter* Planter = Cast<ADFCharacter>(GetInstigator()))
	{
		PlacedTeam = Planter->GetTeam();
	}
}

bool ATripLaserMine::ShouldTrigger(const ADFCharacter* Who) const
{
	return Who && Who != GetInstigator() && (PlacedTeam == DF::NoTeam || Who->GetTeam() != PlacedTeam);
}

void ATripLaserMine::Trigger(ADFCharacter* Who)
{
	// TripLaserOnGround.Touch: 280 to whoever stepped on it, no blast
	FDamageEvent Ev(UDFDamage_TripLaser::StaticClass());
	Who->TakeDamage(280.f, Ev, TripLasersLocal::Credit(this, PlacedTeam), this);
	Destroy();
}

float ATripLaserMine::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (HasAuthority() && Damage > 0.f)
	{
		Destroy();
	}
	return Damage;
}

// --- the weapon -------------------------------------------------------------------------------

ATripLasers::ATripLasers()
{
	DisplayName = LOCTEXT("TripLasers", "Trip Laser");
	MaxClipAmmo = 3;
	MaxLaserDistance = 3375.f;   // MF 1500 uu
	bNeedLaserTarget = false;
	BombClass = ATripLaser::StaticClass();
	AltProjectileClass = ATripLaserMine::StaticClass();
	bAddToLoadout = true;
	// Icon converted from the mod's own art (SourceArt/Mods/Icons/TripLaserIcon.bmp)
	HUDIcon = TEXT("/Game/MF/Mods/Icons/TripLaserIcon");
	HUDIconRect = FIntRect(0, 0, 64, 64);
}

bool ATripLasers::CanPlace() const
{
	const ADFCharacter* Me = GetOwnerCharacter();
	int32 Active = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->GetInstigator() == Me && (It->IsA<ATripLaser>() || It->IsA<ATripLaserMine>()) && !It->IsActorBeingDestroyed())
		{
			++Active;
		}
	}
	if (Active < MaxActiveLasers)
	{
		return true;
	}
	if (APlayerController* PC = Me ? Cast<APlayerController>(Me->GetController()) : nullptr)
	{
		PC->ClientMessage(FString::Printf(TEXT("Max active Trip Lasers reached: %d"), MaxActiveLasers));
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
