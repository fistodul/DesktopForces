#include "World/MFPickupSpot.h"
#include "Game/MFGameState.h"
#include "Weapons/MFWeapon.h"
#include "Engine/World.h"
#include "TimerManager.h"

AMFPickupSpot::AMFPickupSpot()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
	SetActorEnableCollision(false);
}

void AMFPickupSpot::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority() || !ItemClass)
	{
		return;
	}
	SpawnItem();
	GetWorldTimerManager().SetTimer(CheckTimer, this, &AMFPickupSpot::Check, 1.f, true, 1.f);
}

void AMFPickupSpot::SpawnItem()
{
	const AMFGameState* GS = GetWorld()->GetGameState<AMFGameState>();
	if (GS && !GS->IsItemAllowed(ItemClass))
	{
		return;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMFWeapon* W = GetWorld()->SpawnActor<AMFWeapon>(ItemClass, GetActorLocation(), GetActorRotation(), SP);
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

void AMFPickupSpot::Check()
{
	// taken (or gone): back after RespawnTime
	const AMFWeapon* W = Item.Get();
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
	GetWorldTimerManager().SetTimer(RespawnTimer, this, &AMFPickupSpot::SpawnItem, RespawnTime, false);
}
