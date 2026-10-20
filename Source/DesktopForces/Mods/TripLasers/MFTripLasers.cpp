#include "Mods/TripLasers/MFTripLasers.h"
#include "Player/MFCharacter.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MFModTripLasers"

namespace
{
	/** MF GetDamageInstigator: in team games the planter only gets the kill while still on the planting team. */
	AController* Credit(const AActor* Device, uint8 PlacedTeam)
	{
		const AMFCharacter* Planter = Cast<AMFCharacter>(Device->GetInstigator());
		if (!Planter || (PlacedTeam != MF::NoTeam && Planter->GetTeam() != PlacedTeam))
		{
			return nullptr;
		}
		return Planter->GetController();
	}
}

UMFDamage_TripLaser::UMFDamage_TripLaser()
{
	DeathVerb = LOCTEXT("Disintegrated", "disintegrated");
}

// --- planted ------------------------------------------------------------------------------

AMFTripLaser::AMFTripLaser()
{
	InitialLifeSpan = 480.f;
	MaxLaserDistance = 3375.f;   // MF LaserMaxDistance 1500 uu
}

void AMFTripLaser::Arm()
{
	// TripLaser.Timer: the beam runs through friends to the first other thing, or into the open
	const FVector Start = GetActorLocation();
	BeamEnd = Start + GetActorForwardVector() * MaxLaserDistance;
	LaserEnd = BeamEnd;
	bAnchored = false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFTripLaserArm), false, this);
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
	GetWorldTimerManager().SetTimer(Timer, this, &AMFTripLaser::CheckBeam, 0.05f, true);
}

void AMFTripLaser::CheckBeam()
{
	// state Active.Tick: the first thing along the beam; an enemy gets burnt and the laser is spent
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFTripLaserBeam), false, this);
	FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
	if (!GetWorld()->LineTraceSingleByObjectType(Hit, GetActorLocation(), BeamEnd, Objects, Params))
	{
		return;
	}
	AMFCharacter* C = Cast<AMFCharacter>(Hit.GetActor());
	if (C && C->IsAlive() && !IsFriendlyTo(C))
	{
		FDamageEvent Ev(UMFDamage_TripLaser::StaticClass());
		C->TakeDamage(bAnchored ? 280.f : 185.f, Ev, Credit(this, Team), this);
		Destroy();
	}
}

float AMFTripLaser::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (HasAuthority() && Damage > 0.f)
	{
		Destroy();   // breaks, no explosion
	}
	return Damage;
}

// --- thrown -----------------------------------------------------------------------------------

AMFTripLaserMine::AMFTripLaserMine()
{
	InitialLifeSpan = 480.f;
}

void AMFTripLaserMine::BeginPlay()
{
	Super::BeginPlay();
	if (const AMFCharacter* Planter = Cast<AMFCharacter>(GetInstigator()))
	{
		PlacedTeam = Planter->GetTeam();
	}
}

bool AMFTripLaserMine::ShouldTrigger(const AMFCharacter* Who) const
{
	return Who && Who != GetInstigator() && (PlacedTeam == MF::NoTeam || Who->GetTeam() != PlacedTeam);
}

void AMFTripLaserMine::Trigger(AMFCharacter* Who)
{
	// TripLaserOnGround.Touch: 280 to whoever stepped on it, no blast
	FDamageEvent Ev(UMFDamage_TripLaser::StaticClass());
	Who->TakeDamage(280.f, Ev, Credit(this, PlacedTeam), this);
	Destroy();
}

float AMFTripLaserMine::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (HasAuthority() && Damage > 0.f)
	{
		Destroy();
	}
	return Damage;
}

// --- the weapon -------------------------------------------------------------------------------

AMFWeapon_TripLasers::AMFWeapon_TripLasers()
{
	DisplayName = LOCTEXT("TripLasers", "Trip Laser");
	MaxClipAmmo = 3;
	MaxLaserDistance = 3375.f;   // MF 1500 uu
	bNeedLaserTarget = false;
	BombClass = AMFTripLaser::StaticClass();
	AltProjectileClass = AMFTripLaserMine::StaticClass();
	bAddToLoadout = true;
	// Icon converted from the mod's own art (SourceArt/Mods/Icons/TripLaserIcon.bmp)
	HUDIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/MF/Mods/Icons/TripLaserIcon.TripLaserIcon")));
	HUDIconRect = FIntRect(0, 0, 64, 64);
}

bool AMFWeapon_TripLasers::CanPlace() const
{
	const AMFCharacter* Me = GetOwnerCharacter();
	int32 Active = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->GetInstigator() == Me && (It->IsA<AMFTripLaser>() || It->IsA<AMFTripLaserMine>()) && !It->IsActorBeingDestroyed())
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
