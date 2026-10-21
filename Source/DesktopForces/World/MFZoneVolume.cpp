#include "World/MFZoneVolume.h"
#include "Core/MFContent.h"
#include "Player/MFCharacter.h"
#include "Components/BrushComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

AMFZoneVolume::AMFZoneVolume()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	FluidFriction = 2.4f;   // UE default 0.3 is slippery; MF swimming stops quickly
}

void AMFZoneVolume::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(!ZoneVelocity.IsNearlyZero() || (bGravityZone && !FVector2D(ZoneGravity).IsNearlyZero()));
	if (HasAuthority() && !FMath::IsNearlyZero(DamagePerSec))
	{
		GetWorldTimerManager().SetTimer(PainHandle, this, &AMFZoneVolume::PainTimer, 1.f, true);
	}
}

float AMFZoneVolume::GetSurfaceZ() const
{
	return GetComponentsBoundingBox(true).Max.Z;
}

AMFZoneVolume* AMFZoneVolume::FindWater(const UWorld* World, const FVector& Point)
{
	for (TActorIterator<AMFZoneVolume> It(World); It; ++It)
	{
		if (It->bWaterVolume && It->EncompassesPoint(Point))
		{
			return *It;
		}
	}
	return nullptr;
}

void AMFZoneVolume::ActorEnteredVolume(AActor* Other)
{
	Super::ActorEnteredVolume(Other);
	// RageWaterZone EntryActor RageWaterImpact: a splash
	if (bWaterVolume && Cast<APawn>(Other) && GetNetMode() != NM_DedicatedServer)
	{
		MF::PlaySoundRadius(this, MF::LoadClassic<USoundBase>(TEXT("Sounds/MiscSFX/LSplash")), Other->GetActorLocation(), 0.8f, 1200.f);
	}
}

float AMFZoneVolume::GetGravityZ() const
{
	return bGravityZone ? ZoneGravity.Z : Super::GetGravityZ();
}

void AMFZoneVolume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// ZoneVelocity: drift with the current (UE1 adds it to the pawn's movement); sideways
	// gravity: UE's character movement only falls along Z, the rest is pushed here
	const FVector Push = ZoneVelocity * 2.f + (bGravityZone ? FVector(ZoneGravity.X, ZoneGravity.Y, 0.f) : FVector::ZeroVector);
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetCharacterMovement()->GetPhysicsVolume() == this)
		{
			It->GetCharacterMovement()->AddImpulse(Push * DeltaSeconds, true);
		}
	}
}

void AMFZoneVolume::PainTimer()
{
	// Pawn.PainTimer: DamagePerSec x depth (feet 0.4, body +0.4, head +0.2); negative heals
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		AMFCharacter* C = *It;
		if (!C->IsAlive() || C->GetAttachParentActor())
		{
			continue;
		}
		const FVector Loc = C->GetActorLocation();
		const float H = C->GetSimpleCollisionHalfHeight();
		const bool bFeet = EncompassesPoint(Loc - FVector(0.f, 0.f, H * 0.9f));
		if (!bFeet)
		{
			continue;
		}
		const float Depth = 0.4f + (EncompassesPoint(Loc) ? 0.4f : 0.f) + (EncompassesPoint(Loc + FVector(0.f, 0.f, H * 0.8f)) ? 0.2f : 0.f);
		const int32 Amount = FMath::RoundToInt(DamagePerSec * Depth);
		if (Amount > 0)
		{
			FDamageEvent Ev(UMFDamage_Zone::StaticClass());
			C->TakeDamage(float(Amount), Ev, nullptr, this);
		}
		else if (Amount < 0 && C->GetHealth() < C->MaxHealth)
		{
			C->Heal(-Amount);
		}
	}
}
