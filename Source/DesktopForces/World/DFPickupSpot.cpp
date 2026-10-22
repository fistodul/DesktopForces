#include "World/DFPickupSpot.h"
#include "Game/DFGameState.h"
#include "Weapons/DFWeapon.h"
#include "Engine/World.h"
#include "TimerManager.h"

ADFPickupSpot::ADFPickupSpot()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
	SetActorEnableCollision(false);
}

void ADFPickupSpot::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority() || !ItemClass)
	{
		return;
	}
	SpawnItem();
	GetWorldTimerManager().SetTimer(CheckTimer, this, &ADFPickupSpot::Check, 1.f, true, 1.f);
}

void ADFPickupSpot::SpawnItem()
{
	const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
	if (GS && !GS->IsItemAllowed(ItemClass))
	{
		return;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADFWeapon* W = GetWorld()->SpawnActor<ADFWeapon>(ItemClass, GetActorLocation(), GetActorRotation(), SP);
	if (!W)
	{
		return;
	}
	W->MaxClipAmmo = MaxClipAmmo > 0 ? MaxClipAmmo : W->MaxClipAmmo;
	W->MaxClips = MaxClips > 0 ? MaxClips : W->MaxClips;
	W->GiveFullAmmo();
	W->bMapPickup = true;
	W->PlaceAsPickup(GetActorLocation(), GetActorRotation());
	Item = W;
}

void ADFPickupSpot::Check()
{
	// taken (or gone): back after RespawnTime
	const ADFWeapon* W = Item.Get();
	if ((W && W->IsDropped()) || GetWorldTimerManager().IsTimerActive(RespawnTimer))
	{
		return;
	}
	Item = nullptr;
	if (RespawnTime <= 0.f)
	{
		GetWorldTimerManager().ClearTimer(CheckTimer);
		return;
	}
	GetWorldTimerManager().SetTimer(RespawnTimer, this, &ADFPickupSpot::SpawnItem, RespawnTime, false);
}
