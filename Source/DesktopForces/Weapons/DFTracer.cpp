#include "Weapons/DFTracer.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace {
	constexpr float TracerSpeed = 6750.f; // 3000 uu/s
	constexpr float TracerMinLength = 288.f; // 128 uu: closer, no tracer
	constexpr float TracerStartOffset = 144.f; // 64 uu out from the gun
} // namespace

ADFTracer::ADFTracer() {
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	Streak = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Streak"));
	CrossStreak = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CrossStreak"));
	for (UStaticMeshComponent* Part : { Streak.Get(), CrossStreak.Get() }) {
		Part->SetupAttachment(RootComponent);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetRelativeScale3D(FVector(0.9f, 0.04f, 1.f)); // 90 cm x 4 cm, with soft edges
		if (Plane.Succeeded())
			Part->SetStaticMesh(Plane.Object);
	}
	CrossStreak->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
}

void ADFTracer::Fire(UWorld* World, const FVector& Start, const FVector& End) {
	if (!World || World->GetNetMode() == NM_DedicatedServer)
		return;
	const FVector Dir = (End - Start).GetSafeNormal();
	const FVector From = Start + Dir * TracerStartOffset;
	const float Len = FVector::Dist(From, End);
	if (FVector::Dist(Start, End) < TracerMinLength || Dir.IsNearlyZero())
		return;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ADFTracer* T = World->SpawnActor<ADFTracer>(From, Dir.Rotation(), SP)) {
		// a weak cache: a raw static outlived the material after a map change (garbage
		// collected) and crashed the next shot
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
			Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MF/Materials/M_MF_Tracer.M_MF_Tracer"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (UMaterialInterface* Material = Cached.Get()) {
			T->Streak->SetMaterial(0, Material);
			T->CrossStreak->SetMaterial(0, Material);
		}
		T->Velocity = Dir * TracerSpeed;
		T->SetLifeSpan(FMath::Min(2.f, Len / TracerSpeed)); // FMin(2, Len / 3000)
	}
}

void ADFTracer::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);
	SetActorLocation(GetActorLocation() + Velocity * DeltaSeconds);
}
