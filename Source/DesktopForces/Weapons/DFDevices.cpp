#include "Weapons/DFDevices.h"
#include "Core/DFTypes.h"
#include "Player/DFCharacter.h"
#include "Vehicles/DFVehicle.h"
#include "Weapons/SeekerMines/SeekerMines.h"
#include "Weapons/SentryGun/SentryGun.h"
#include "Weapons/TripLasers/TripLasers.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"

bool DF::GetPlacedTeam(const AActor* A, uint8& OutTeam) {
	if (const ASeekerMine* M = Cast<ASeekerMine>(A))
		OutTeam = M->GetPlacedTeam();
	else if (const ASentryTurret* S = Cast<ASentryTurret>(A))
		OutTeam = S->GetTeam();
	else if (const ATripLaser* L = Cast<ATripLaser>(A))
		OutTeam = L->Team;
	else if (const ATripLaserMine* G = Cast<ATripLaserMine>(A))
		OutTeam = G->GetPlacedTeam();
	else
		return false;
	return true;
}

bool DF::IsDeviceTargetAlive(const AActor* A) {
	if (!A || A->IsActorBeingDestroyed())
		return false;
	if (const ADFCharacter* C = Cast<ADFCharacter>(A))
		return C->IsAlive();
	if (const ASentryTurret* S = Cast<ASentryTurret>(A))
		return !S->IsDead();
	if (const ASeekerMine* M = Cast<ASeekerMine>(A))
		return !M->IsDetonated();
	if (const ADFVehicle* V = Cast<ADFVehicle>(A))
		return V->Health > 0 && !V->IsBlownUp();
	return true;
}

bool DF::IsSoldierFriendly(const ADFCharacter* C, uint8 Team, const APlayerState* Placer) {
	if (!C || !C->IsAlive())
		return true;
	if (Team != DF::NoTeam)
		return C->GetTeam() == Team;
	// whoever drives has no player state of their own: the controller is on the vehicle
	const APlayerState* PS = C->GetPlayerState();
	if (!PS)
		if (const APawn* Ride = Cast<APawn>(C->GetAttachParentActor()))
			PS = Ride->GetPlayerState();
	return Placer && PS == Placer;
}

uint8 DF::VehicleStatus(const ADFVehicle* V, uint8 Team, const APlayerState* Placer) {
	if (!V)
		return 0;
	bool bFriend = false;
	auto Check = [&](const ADFCharacter* C) {
		if (!C)
			return false;
		if (!IsSoldierFriendly(C, Team, Placer))
			return true;
		bFriend = true;
		return false;
	};
	if (Check(V->GetDriver()))
		return 2;
	for (const ADFCharacter* P : V->GetPassengers())
		if (Check(P))
			return 2;
	return bFriend ? 1 : 0;
}

bool DF::IsDeviceFriendly(const AActor* Other, const AActor* Device, uint8 Team, const APlayerState* Placer) {
	if (!Other || Other == Device)
		return true;
	uint8 OtherTeam;
	if (GetPlacedTeam(Other, OtherTeam)) {
		if (Team != DF::NoTeam)
			return OtherTeam == Team;
		const APawn* OtherPlacer = Other->GetInstigator();
		return OtherPlacer == Device->GetInstigator() || (Placer && OtherPlacer && OtherPlacer->GetPlayerState() == Placer);
	}
	if (const ADFVehicle* V = Cast<ADFVehicle>(Other))
		return VehicleStatus(V, Team, Placer) != 2;
	if (const ADFCharacter* C = Cast<ADFCharacter>(Other))
		return IsSoldierFriendly(C, Team, Placer);
	return true;
}

AActor* DF::FindDeviceTarget(const AActor* Device, uint8 Team, const APlayerState* Placer, float Range, TFunctionRef<bool(const AActor*)> Usable) {
	AActor* Best = nullptr;
	float BestDist = Range;
	const FVector From = Device->GetActorLocation();
	auto Consider = [&](AActor* A) {
		const float D = FVector::Dist(From, A->GetActorLocation());
		if (D < BestDist && IsDeviceTargetAlive(A) && !IsDeviceFriendly(A, Device, Team, Placer) && Usable(A)) {
			BestDist = D;
			Best = A;
		}
	};
	UWorld* World = Device->GetWorld();
	// 1. occupied enemy vehicles, 2. everything with a pulse (soldiers, turrets, seeker mines)
	for (TActorIterator<ADFVehicle> It(World); It; ++It)
		Consider(*It);
	for (TActorIterator<ADFCharacter> It(World); It; ++It)
		Consider(*It);
	for (TActorIterator<ASentryTurret> It(World); It; ++It)
		Consider(*It);
	for (TActorIterator<ASeekerMine> It(World); It; ++It)
		Consider(*It);
	return Best;
}

bool DF::IsRunOverByFriend(const FDamageEvent& Event, const AController* Driver, const AActor* Device, uint8 Team, const APlayerState* Placer) {
	if (!Event.DamageTypeClass || !Event.DamageTypeClass->IsChildOf(UDFDamage_RunOver::StaticClass()))
		return false;
	const ADFCharacter* C = Driver ? Cast<ADFCharacter>(Driver->GetPawn()) : nullptr;
	if (!C)
		if (const ADFVehicle* V = Driver ? Cast<ADFVehicle>(Driver->GetPawn()) : nullptr)
			C = V->GetDriver();
	return C && C->IsAlive() && IsSoldierFriendly(C, Team, Placer);
}
