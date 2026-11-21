#include "Weapons/TripLasers/TripLasers.h"
#include "Weapons/DFDevices.h"
#include "Core/DFProps.h"
#include "Weapons/SentryGun/SentryGun.h"
#include "Weapons/SeekerMines/SeekerMines.h"
#include "Vehicles/DFVehicle.h"
#include "Player/DFCharacter.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "TripLasers"

UDFDamage_TripLaser::UDFDamage_TripLaser() {
	DeathVerb = LOCTEXT("Disintegrated", "disintegrated");
	// TripLaser hits a vehicle with GetDamage() (280 anchored, 185 into the open) like a pawn.
	// changeDamage scales it x 10 of 8000 health (x 0.125 here) unless Rage's
	// IsExplosiveDamageType counts 'TripLasersDOTTripLasers' (x 80, no armour); that list is in
	// Rage.u, whose script text doesn't export. Filip, 2026-11-21: one of the strongest single
	// hits, vehicles too - as an explosive here: x 1.5 x (1 - Armour), a Humvee gone in one
	VehicleDamageScale = 1.5f;
}

// --- planted ------------------------------------------------------------------------------

ATripLaser::ATripLaser() {
	InitialLifeSpan = 480.f;
	MaxLaserDistance = 3375.f; // MF LaserMaxDistance 1500 uu
}

void ATripLaser::Arm() {
	// what it stuck to, if that can move (Owner, AttachedLoc)
	FHitResult Stuck;
	FCollisionQueryParams Behind(SCENE_QUERY_STAT(DFTripLaserBase), false, this);
	if (GetWorld()->LineTraceSingleByObjectType(Stuck, GetActorLocation(), GetActorLocation() - GetActorForwardVector() * 30.f, DF::SolidObjects(), Behind) && Cast<APawn>(Stuck.GetActor())) {
		PlantedOn = Stuck.GetActor();
		PlantedAt = Stuck.GetActor()->GetActorTransform();
	}
	// TripLaser.Timer: the beam runs through friends to the first other thing, or into the open
	const FVector Start = GetActorLocation();
	BeamEnd = Start + GetActorForwardVector() * MaxLaserDistance;
	LaserEnd = BeamEnd;
	bAnchored = false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFTripLaserArm), false, this);
	const FCollisionObjectQueryParams Objects = DF::SolidObjects();
	for (int32 i = 0; i < 16; ++i) {
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByObjectType(Hit, Start, BeamEnd, Objects, Params))
			break;
		if (IsFriendlyTo(Hit.GetActor())) {
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

void ATripLaser::CheckBeam() {
	// state Active.Tick: planted on something that moved, it goes
	if (const AActor* Base = PlantedOn.Get(); Base && !Base->GetActorTransform().Equals(PlantedAt, 0.5f)) {
		Destroy();
		return;
	}
	// the first thing along the whole beam; a pawn that isn't friendly gets burnt and the laser is
	// spent. Vehicles are pawns: an empty one sets it off too
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFTripLaserBeam), false, this);
	const FCollisionObjectQueryParams Objects = DF::SolidObjects();
	if (!GetWorld()->LineTraceSingleByObjectType(Hit, GetActorLocation(), BeamEnd, Objects, Params))
		return;
	AActor* A = Hit.GetActor();
	const bool bPawn = Cast<APawn>(A) || Cast<IDFDevice>(A); // devices: StationaryPawns in MF
	if (bPawn && !IsFriendlyTo(A)) {
		FPointDamageEvent Ev(bAnchored ? 280.f : 185.f, Hit, GetActorForwardVector(), UDFDamage_TripLaser::StaticClass());
		A->TakeDamage(Ev.Damage, Ev, DF::PlacerCredit(Shooter.Get(), Team), this);
		Destroy();
	}
}

bool ATripLaser::IsFriendlyTo(const AActor* Other) const {
	// TripLaser.IsFriendly: a team mate, a team's device, a vehicle with only friends in it (an
	// empty one is no friend: it stops the beam and sets it off)
	return DF::SideOf(Other, this, Team, PlacerState.Get()) == DF::ESide::Friend;
}

float ATripLaser::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) {
	if (HasAuthority() && Damage > 0.f)
		Destroy(); // breaks, no explosion
	return Damage;
}

// --- thrown -----------------------------------------------------------------------------------

ATripLaserMine::ATripLaserMine() {
	InitialLifeSpan = 480.f;
}

bool ATripLaserMine::ShouldTrigger(const ADFCharacter* Who) const {
	return DF::SideOf(Who, this, PlacedTeam, PlacerState.Get()) == DF::ESide::Enemy; // TripLaserOnGround.IsFriendly
}

void ATripLaserMine::Trigger(ADFCharacter* Who) {
	// TripLaserOnGround.Touch: 280 to whoever stepped on it, no blast
	FDamageEvent Ev(UDFDamage_TripLaser::StaticClass());
	Who->TakeDamage(280.f, Ev, DF::PlacerCredit(Shooter.Get(), PlacedTeam), this);
	Destroy();
}

void ATripLaserMine::SetOff() {
	if (HasAuthority())
		Destroy();
}

// --- the weapon -------------------------------------------------------------------------------

ATripLasers::ATripLasers() {
	DisplayName = LOCTEXT("TripLasers", "Trip Laser");
	MaxClipAmmo = 3;
	MaxLaserDistance = 3375.f; // MF 1500 uu
	bNeedLaserTarget = false;
	BombClass = ATripLaser::StaticClass();
	AltProjectileClass = ATripLaserMine::StaticClass();
	bAddToLoadout = true;
	// Icon converted from the mod's own art (SourceArt/Mods/Icons/TripLaserIcon.bmp)
	HUDIcon = TEXT("/Game/MF/Mods/Icons/TripLaserIcon");
	HUDIconRect = FIntRect(0, 0, 64, 64);
}

bool ATripLasers::CanPlace() const {
	const ADFCharacter* Me = GetOwnerCharacter();
	int32 Active = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (It->GetInstigator() == Me && (It->IsA<ATripLaser>() || It->IsA<ATripLaserMine>()) && !It->IsActorBeingDestroyed())
			++Active;
	if (Active < MaxActiveLasers)
		return true;
	if (APlayerController* PC = Me ? Cast<APlayerController>(Me->GetController()) : nullptr)
		PC->ClientMessage(FString::Printf(TEXT("Max active Trip Lasers reached: %d"), MaxActiveLasers));
	return false;
}

#undef LOCTEXT_NAMESPACE
