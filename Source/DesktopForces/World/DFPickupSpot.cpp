#include "World/DFPickupSpot.h"
#include "Components/CapsuleComponent.h"
#include "Player/DFCharacter.h"
#include "Game/DFGameState.h"
#include "Weapons/DFWeapon.h"
#include "Engine/World.h"
#include "TimerManager.h"

ADFPickupSpot::ADFPickupSpot() {
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
	PickupTouch = CreateDefaultSubobject<UCapsuleComponent>(TEXT("PickupTouch"));
	PickupTouch->SetupAttachment(RootComponent);
	PickupTouch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PickupTouch->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupTouch->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PickupTouch->SetGenerateOverlapEvents(true);
	PickupTouch->SetCanEverAffectNavigation(false);
}

void ADFPickupSpot::BeginPlay() {
	Super::BeginPlay();
	if (!HasAuthority() || !ItemClass)
		return;
	if (bAutoPickup) {
		SetActorEnableCollision(true);
		PickupTouch->SetCapsuleSize(TouchRadius, FMath::Max(TouchRadius, TouchHalfHeight));
		PickupTouch->OnComponentBeginOverlap.AddDynamic(this, &ADFPickupSpot::OnTouch);
		PickupTouch->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	SpawnItem();
	GetWorldTimerManager().SetTimer(CheckTimer, this, &ADFPickupSpot::Check, 1.f, true, 1.f);
}

void ADFPickupSpot::SpawnItem() {
	const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
	if (GS && !GS->IsItemAllowed(ItemClass))
		return;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADFWeapon* W = GetWorld()->SpawnActor<ADFWeapon>(ItemClass, GetActorLocation(), GetActorRotation(), SP);
	if (!W)
		return;
	W->MaxClipAmmo = MaxClipAmmo > 0 ? MaxClipAmmo : W->MaxClipAmmo;
	W->MaxClips = MaxClips > 0 ? MaxClips : W->MaxClips;
	W->GiveFullAmmo();
	W->bMapPickup = true;
	W->PlaceAsPickup(GetActorLocation(), GetActorRotation(), !bAutoPickup);
	Item = W;
	CheckTouching();
}

void ADFPickupSpot::Check() {
	CheckTouching();
	// taken (or gone): back after RespawnTime
	const ADFWeapon* W = Item.Get();
	if ((W && W->IsDropped()) || GetWorldTimerManager().IsTimerActive(RespawnTimer))
		return;
	Item = nullptr;
	if (RespawnTime <= 0.f) {
		GetWorldTimerManager().ClearTimer(CheckTimer);
		PickupTouch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}
	GetWorldTimerManager().SetTimer(RespawnTimer, this, &ADFPickupSpot::SpawnItem, RespawnTime, false);
}

void ADFPickupSpot::OnTouch(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit) {
	TryPickup(Cast<ADFCharacter>(Other));
}

void ADFPickupSpot::CheckTouching() {
	if (!bAutoPickup || !HasAuthority() || !Item.IsValid() || !Item->IsDropped())
		return;
	TArray<AActor*> Touching;
	PickupTouch->GetOverlappingActors(Touching, ADFCharacter::StaticClass());
	for (AActor* Other : Touching)
		TryPickup(Cast<ADFCharacter>(Other));
}

void ADFPickupSpot::TryPickup(ADFCharacter* Soldier) {
	ADFWeapon* Weapon = Item.Get();
	if (!HasAuthority() || !bAutoPickup || !Soldier || !Soldier->IsAlive() || !Soldier->IsPlayerControlled() || Soldier->IsInLoadout() || !Weapon || !Weapon->IsDropped())
		return;
	// PickupWeapon enforces both carry blocks and this item's MaxCanCarry. Failed
	// attempts leave the item available; a successful one follows normal weapon priority.
	if (Soldier->PickupWeapon(Weapon)) {
		ADFWeapon* Current = Soldier->GetCurrentWeapon();
		if (Weapon->bSelectable && (!Current || Weapon->AutoSwitchPriority > Current->AutoSwitchPriority))
			Soldier->EquipWeapon(Weapon);
	}
}
