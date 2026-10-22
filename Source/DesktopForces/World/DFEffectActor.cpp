#include "World/DFEffectActor.h"
#include "World/DFDecals.h"
#include "Core/DFContent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "Vehicles/VehicleFrag.h"

ADFEffectActor::ADFEffectActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(false);
	bAlwaysRelevant = true;            // like MF's RageSprites: everyone sees a blast
	InitialLifeSpan = 2.f;             // long enough to reach the clients
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ADFEffectActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ADFEffectActor, Effect, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ADFEffectActor, Scale, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ADFEffectActor, bVehicleExplosion, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ADFEffectActor, Sound, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ADFEffectActor, SoundRadiusUU, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ADFEffectActor, SoundVolume, COND_InitialOnly);
}

void ADFEffectActor::Spawn(UWorld* World, FName InEffect, const FVector& At, float InScale, bool bInVehicleExplosion,
	const FString& InSound, float InSoundRadiusUU, float InSoundVolume)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.bDeferConstruction = true;
	if (ADFEffectActor* A = World->SpawnActor<ADFEffectActor>(At, FRotator::ZeroRotator, SP))
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

void ADFEffectActor::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FVector At = GetActorLocation();
	if (!Sound.IsEmpty())
	{
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(Sound), At, SoundVolume, SoundRadiusUU);
	}
	if (bVehicleExplosion)
	{
		AVehicleFrag::Explosion(GetWorld(), At, Scale);
	}
	else if (!Effect.IsNone())
	{
		DF::SpawnEffect(this, *Effect.ToString(), At, Scale);
		DF::SpawnScorchBelow(this, At, 450.f * Scale);
	}
}
