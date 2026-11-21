#include "Weapons/DFDevices.h"
#include "Core/DFTypes.h"
#include "Player/DFCharacter.h"
#include "Vehicles/DFVehicle.h"
#include "Weapons/SeekerMines/SeekerMines.h"
#include "Weapons/SentryGun/SentryGun.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"

bool DF::GetPlacedTeam(const AActor* A, uint8& OutTeam) {
	const IDFDevice* Device = Cast<IDFDevice>(A);
	if (Device)
		OutTeam = Device->GetPlacedTeam();
	return Device != nullptr;
}

bool DF::IsDeviceTargetAlive(const AActor* A) {
	if (!A || A->IsActorBeingDestroyed())
		return false;
	if (const ADFCharacter* C = Cast<ADFCharacter>(A))
		return C->IsAlive();
	if (const ADFVehicle* V = Cast<ADFVehicle>(A))
		return V->Health > 0 && !V->IsBlownUp();
	const IDFDevice* Device = Cast<IDFDevice>(A);
	return !Device || Device->IsWorking();
}

bool DF::IsSoldierFriendly(const ADFCharacter* C, uint8 Team, const APlayerState* Placer) {
	if (!C || !C->IsAlive())
		return true;
	if (Team != DF::NoTeam)
		return C->GetTeam() == Team;
	return Placer && C->GetEffectivePlayerState() == Placer;
}

DF::ESide DF::SideOf(const AActor* Other, const AActor* Device, uint8 Team, const APlayerState* Placer) {
	if (!Other || Other == Device)
		return ESide::Friend;
	if (const IDFDevice* OtherDevice = Cast<IDFDevice>(Other)) {
		if (Team != DF::NoTeam)
			return OtherDevice->GetPlacedTeam() == Team ? ESide::Friend : ESide::Enemy;
		return Placer && OtherDevice->GetPlacer() == Placer ? ESide::Friend : ESide::Enemy;
	}
	if (const ADFVehicle* V = Cast<ADFVehicle>(Other)) {
		// GetVehicleStatus: an enemy inside makes it an enemy, only friends a friend
		bool bFriend = false;
		auto Rider = [&](const ADFCharacter* C) {
			if (!C)
				return false;
			const bool bFriendly = IsSoldierFriendly(C, Team, Placer);
			bFriend |= bFriendly;
			return !bFriendly;
		};
		if (Rider(V->GetDriver()))
			return ESide::Enemy;
		for (const ADFCharacter* P : V->GetPassengers())
			if (Rider(P))
				return ESide::Enemy;
		return bFriend ? ESide::Friend : ESide::Neutral;
	}
	if (const ADFCharacter* C = Cast<ADFCharacter>(Other))
		return IsSoldierFriendly(C, Team, Placer) ? ESide::Friend : ESide::Enemy;
	return ESide::Neutral;
}

AActor* DF::FindDeviceTarget(const AActor* Device, uint8 Team, const APlayerState* Placer, float Range, TFunctionRef<bool(const AActor*)> Usable) {
	AActor* Best = nullptr;
	float BestDist = Range;
	const FVector From = Device->GetActorLocation();
	auto Consider = [&](AActor* A) {
		const float D = FVector::Dist(From, A->GetActorLocation());
		if (D < BestDist && IsDeviceTargetAlive(A) && SideOf(A, Device, Team, Placer) == ESide::Enemy && Usable(A)) {
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
	// the vehicle's controller is its driver's: their player state is on it
	if (!Driver || !Driver->PlayerState)
		return false;
	if (Team != DF::NoTeam) {
		const ADFVehicle* V = Cast<ADFVehicle>(Driver->GetPawn());
		const ADFCharacter* C = V ? V->GetDriver() : Cast<ADFCharacter>(Driver->GetPawn());
		return C && IsSoldierFriendly(C, Team, Placer);
	}
	return Driver->PlayerState == Placer;
}
