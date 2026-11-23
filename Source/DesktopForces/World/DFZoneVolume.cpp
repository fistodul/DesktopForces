#include "World/DFZoneVolume.h"
#include "Weapons/DFProjectile.h"
#include "Core/DFContent.h"
#include "Player/DFCharacter.h"
#include "Components/BrushComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "Core/DFProps.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

ADFZoneVolume::ADFZoneVolume() {
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	FluidFriction = 2.4f; // UE default 0.3 is slippery; MF swimming stops quickly
}

void ADFZoneVolume::PostInitializeComponents() {
	Super::PostInitializeComponents();
	// a zone is air or water, it stops nothing: as a brush it blocked rockets in mid air. A type no
	// trace by object asks for, so beams go through as well
	UBrushComponent* Shape = GetBrushComponent();
	Shape->SetCollisionObjectType(ECC_Destructible);
	Shape->SetCollisionResponseToAllChannels(ECR_Overlap);
}

void ADFZoneVolume::BeginPlay() {
	Super::BeginPlay();
	SetActorTickEnabled(!ZoneVelocity.IsNearlyZero() || (bGravityZone && !FVector2D(ZoneGravity).IsNearlyZero()));
	if (HasAuthority() && !FMath::IsNearlyZero(DamagePerSec))
		GetWorldTimerManager().SetTimer(PainHandle, this, &ADFZoneVolume::PainTimer, 1.f, true);
}

float ADFZoneVolume::GetSurfaceZ() const {
	return GetComponentsBoundingBox(true).Max.Z;
}

ADFZoneVolume* ADFZoneVolume::FindWater(const UWorld* World, const FVector& Point) {
	for (TActorIterator<ADFZoneVolume> It(World); It; ++It)
		if (It->bWaterVolume && It->EncompassesPoint(Point))
			return *It;
	return nullptr;
}

void ADFZoneVolume::ActorEnteredVolume(AActor* Other) {
	Super::ActorEnteredVolume(Other);
	// RageWaterZone EntryActor RageWaterImpact: a splash
	if (bWaterVolume && Cast<APawn>(Other) && GetNetMode() != NM_DedicatedServer)
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/MiscSFX/LSplash")), Other->GetActorLocation(), 0.8f, 1200.f);
	// RageWaterImpact's rings (and the splash) where it went in, on the surface
	if (bWaterVolume && GetNetMode() != NM_DedicatedServer && (Cast<APawn>(Other) || Other->IsA<ADFProjectile>())) {
		const FVector At(Other->GetActorLocation().X, Other->GetActorLocation().Y, GetSurfaceZ());
		DF::SpawnEffect(this, TEXT("Splash"), At, Other->IsA<ADFProjectile>() ? 0.5f : 1.f);
		// RageWaterImpact.Timer: a ring (DrawScale 0.15) every 0.3 s, twice
		for (const float Delay : { 0.3f, 0.6f }) {
			FTimerHandle Ring;
			GetWorldTimerManager().SetTimer(Ring, FTimerDelegate::CreateWeakLambda(this, [this, At]() { ADFWaterRing::Spawn(GetWorld(), At, 0.15f); }), Delay, false);
		}
	}
}

ADFWaterRing::ADFWaterRing() {
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	RootComponent = Mesh;
}

void ADFWaterRing::Spawn(UWorld* World, const FVector& At, float DrawScale) {
	if (!World || World->GetNetMode() == NM_DedicatedServer)
		return;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADFWaterRing* Ring = World->SpawnActor<ADFWaterRing>(At, FRotator(90.f, 0.f, 0.f), SP); // rot(16384, 0, 0)
	if (!Ring)
		return;
	for (int32 i = 0; i < 6; ++i) {
		const FString Name = i == 0 ? FString(TEXT("RageEffects/RageRingex")) : FString::Printf(TEXT("RageEffects/RageRingex_F%d"), i);
		if (!DF::ApplyPropMesh(Ring->Mesh, *Name, DrawScale))
			break;
		Ring->Frames.Add(Ring->Mesh->GetStaticMesh());
	}
	UMaterialInterface* Fade = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MF/Materials/M_MF_Fade.M_MF_Fade"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UTexture2D* Skin = DF::LoadClassic<UTexture2D>(TEXT("Textures/RageEffects/WaterRingTex/Splash_A01"));
	if (Ring->Frames.Num() < 6 || !Fade || !Skin) {
		Ring->Destroy(); // no classic pack, or a pipeline from before the ring's frames
		return;
	}
	Ring->Material = UMaterialInstanceDynamic::Create(Fade, Ring);
	Ring->Material->SetTextureParameterValue(TEXT("Texture"), Skin);
	for (int32 i = 0; i < Ring->Mesh->GetNumMaterials(); ++i)
		Ring->Mesh->SetMaterial(i, Ring->Material);
	Ring->Step();
	World->GetTimerManager().SetTimer(Ring->StepTimer, Ring, &ADFWaterRing::Step, 1.f / 30.f, true);
}

void ADFWaterRing::Step() {
	const float Life = 0.8f;
	if (Age >= Life) {
		Destroy();
		return;
	}
	// Explosion: 6 frames at 30 fps x 0.25 = 7.5 a second, the last held
	Mesh->SetStaticMesh(Frames[FMath::Min(FMath::FloorToInt(Age * 7.5f), Frames.Num() - 1)]);
	Material->SetScalarParameterValue(TEXT("Fade"), 1.f - Age / Life);
	Age += 1.f / 30.f;
}

float ADFZoneVolume::GetGravityZ() const {
	return bGravityZone ? ZoneGravity.Z : Super::GetGravityZ();
}

void ADFZoneVolume::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);
	// ZoneVelocity: drift with the current (UE1 adds it to the pawn's movement); sideways
	// gravity: UE's character movement only falls along Z, the rest is pushed here
	const FVector Push = ZoneVelocity * 2.f + (bGravityZone ? FVector(ZoneGravity.X, ZoneGravity.Y, 0.f) : FVector::ZeroVector);
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
		if (It->IsAlive() && It->GetCharacterMovement()->GetPhysicsVolume() == this)
			It->GetCharacterMovement()->AddImpulse(Push * DeltaSeconds, true);
}

void ADFZoneVolume::PainTimer() {
	// Pawn.PainTimer: DamagePerSec x depth (feet 0.4, body +0.4, head +0.2); negative heals
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It) {
		ADFCharacter* C = *It;
		if (!C->IsAlive() || C->GetAttachParentActor())
			continue;
		const FVector Loc = C->GetActorLocation();
		const float H = C->GetSimpleCollisionHalfHeight();
		const bool bFeet = EncompassesPoint(Loc - FVector(0.f, 0.f, H * 0.9f));
		if (!bFeet)
			continue;
		const float Depth = 0.4f + (EncompassesPoint(Loc) ? 0.4f : 0.f) + (EncompassesPoint(Loc + FVector(0.f, 0.f, H * 0.8f)) ? 0.2f : 0.f);
		const int32 Amount = FMath::RoundToInt(DamagePerSec * Depth);
		if (Amount > 0) {
			FDamageEvent Ev(UDFDamage_Zone::StaticClass());
			C->TakeDamage(float(Amount), Ev, nullptr, this);
		} else if (Amount < 0 && C->GetHealth() < C->MaxHealth) {
			C->Heal(-Amount);
		}
	}
}
