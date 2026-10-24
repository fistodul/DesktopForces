#include "World/MineGenerator.h"
#include "DesktopForces.h"
#include "Core/DFContent.h"
#include "Core/DFProps.h"
#include "Core/DFTypes.h"
#include "Player/DFCharacter.h"
#include "Vehicles/DFVehicle.h"
#include "World/DFMapActors.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace {
	constexpr float Radius = 144.f; // TripBombOnGround.CollisionRadius 64 uu
	constexpr float VehicleRadius = 216.f; // vehicles at 1.5 x
	constexpr float RearmTime = 5.f; // mineGenerator.checkInterval
} // namespace

AMineGenerator::AMineGenerator() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetVisibility(false);
	RootComponent = Mesh;
}

void AMineGenerator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMineGenerator, bArmed);
	DOREPLIFETIME_CONDITION(AMineGenerator, bSplash, COND_InitialOnly);
}

void AMineGenerator::SpawnAll(UWorld* World) {
	int32 N = 0;
	for (const TCHAR* Class : { TEXT("mineGenerator"), TEXT("splashMakerGen") }) {
		for (ADFLegacyActor* L : ADFLegacyActor::FindAll(World, Class)) {
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AMineGenerator* G = World->SpawnActorDeferred<AMineGenerator>(AMineGenerator::StaticClass(), L->GetActorTransform())) {
				G->bSplash = FCString::Stricmp(Class, TEXT("splashMakerGen")) == 0;
				G->FinishSpawning(L->GetActorTransform());
				++N;
			}
		}
	}
	if (N)
		UE_LOG(LogDF, Log, TEXT("Minefield: %d mines / splashes"), N);
}

void AMineGenerator::BeginPlay() {
	Super::BeginPlay();
	if (!bSplash)
		DF::ApplyPropMesh(Mesh, TEXT("RageWeapons/TripBombsThrowMesh")); // TripBombOnGround's mesh
	if (HasAuthority())
		Arm();
}

void AMineGenerator::Arm() {
	bArmed = true;
	OnRep_Armed();
	GetWorldTimerManager().SetTimer(CheckTimer, this, &AMineGenerator::Check, 0.1f, true, FMath::FRand() * 0.1f);
}

void AMineGenerator::OnRep_Armed() {
	Mesh->SetVisibility(bArmed && !bSplash);
}

void AMineGenerator::Check() {
	// generatedTripBombOnGround.Tick: soldiers in reach, vehicles a bit further
	TArray<FOverlapResult> Hits;
	GetWorld()->OverlapMultiByChannel(Hits, GetActorLocation(), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(VehicleRadius));
	for (const FOverlapResult& H : Hits) {
		const AActor* A = H.GetActor();
		const ADFCharacter* C = Cast<ADFCharacter>(A);
		const bool bVehicle = Cast<ADFVehicle>(A) != nullptr;
		if ((C && C->IsAlive() && FVector::Dist(C->GetActorLocation(), GetActorLocation()) < Radius + 40.f) || bVehicle) {
			SetOff();
			return;
		}
	}
}

void AMineGenerator::SetOff() {
	GetWorldTimerManager().ClearTimer(CheckTimer);
	bArmed = false;
	OnRep_Armed();
	if (!bSplash) {
		// TripBombOnGround.BlowUp: HurtRadius(120, 200 uu)
		TArray<AActor*> Ignore;
		UGameplayStatics::ApplyRadialDamageWithFalloff(this, 120.f, 0.f, GetActorLocation() + FVector(0.f, 0.f, 36.f), 0.f, 450.f, 1.f,
			UDFDamage_Explosion::StaticClass(), Ignore, this, nullptr, ECC_Visibility);
	}
	MulticastBlast(bSplash);
	GetWorldTimerManager().SetTimer(RearmTimer, this, &AMineGenerator::Arm, RearmTime, false);
}

void AMineGenerator::MulticastBlast_Implementation(bool bWasSplash) {
	if (GetNetMode() == NM_DedicatedServer)
		return;
	if (bWasSplash) {
		DF::SpawnEffect(this, TEXT("Splash"), GetActorLocation());
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/MiscSFX/LSplash")), GetActorLocation(), 1.f, 1600.f);
		return;
	}
	DF::SpawnEffect(this, TEXT("MineExplosion"), GetActorLocation());
	DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/MiscSFX/Explode")), GetActorLocation(), 1.f, 8000.f);
}
