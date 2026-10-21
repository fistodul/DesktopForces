#include "World/MFEffectActor.h"
#include "Core/MFContent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "Vehicles/MFVehicleFrag.h"

AMFEffectActor::AMFEffectActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(false);
	bAlwaysRelevant = true;            // like MF's RageSprites: everyone sees a blast
	InitialLifeSpan = 2.f;             // long enough to reach the clients
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMFEffectActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMFEffectActor, Effect, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMFEffectActor, Scale, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMFEffectActor, bVehicleExplosion, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMFEffectActor, Sound, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMFEffectActor, SoundRadiusUU, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMFEffectActor, SoundVolume, COND_InitialOnly);
}

void AMFEffectActor::Spawn(UWorld* World, FName InEffect, const FVector& At, float InScale, bool bInVehicleExplosion,
	const FString& InSound, float InSoundRadiusUU, float InSoundVolume)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.bDeferConstruction = true;
	if (AMFEffectActor* A = World->SpawnActor<AMFEffectActor>(At, FRotator::ZeroRotator, SP))
	{
		A->Effect = InEffect;
		A->Scale = InScale;
		A->bVehicleExplosion = bInVehicleExplosion;
		A->Sound = InSound;
		A->SoundRadiusUU = InSoundRadiusUU;
		A->SoundVolume = InSoundVolume;
		A->FinishSpawning(FTransform(At));
	}
}

void AMFEffectActor::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FVector At = GetActorLocation();
	if (!Sound.IsEmpty())
	{
		MF::PlaySoundRadius(this, MF::LoadClassic<USoundBase>(Sound), At, SoundVolume, SoundRadiusUU);
	}
	if (bVehicleExplosion)
	{
		AMFVehicleFrag::Explosion(GetWorld(), At, Scale);
	}
	else if (!Effect.IsNone())
	{
		MF::SpawnEffect(this, *Effect.ToString(), At, Scale);
	}
}
