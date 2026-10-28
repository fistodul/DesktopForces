#include "World/DFDecals.h"
#include "Core/DFUserSettings.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace {
	const TCHAR* DecalTexture(DF::EDecal Kind, int32 Variant) {
		static const TCHAR* Holes[] = { TEXT("BulletHole0"), TEXT("BulletHole1"), TEXT("BulletHole2"), TEXT("BulletHole3") };
		static const TCHAR* Scorches[] = { TEXT("Scorch0"), TEXT("Scorch1") };
		return Kind == DF::EDecal::BulletHole ? Holes[Variant % 4] : Scorches[Variant % 2];
	}

	UMaterialInterface* DecalMaterial(DF::EDecal Kind, int32 Variant, bool bQuad) {
		const FString Name = FString::Printf(TEXT("MI_MF_%s%s"), DecalTexture(Kind, Variant), bQuad ? TEXT("_Quad") : TEXT(""));
		return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/MF/Materials/Decals/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	/** Live projected meshes, oldest first; bounded geometry and lifetime. */
	TArray<TWeakObjectPtr<ADFProjectedScorch>> GScorches;
	constexpr int32 MaxScorches = 24;
} // namespace

void DF::SpawnDecal(const UObject* WorldContext, EDecal Kind, const FVector& At, const FVector& Normal, float Size) {
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const int32 Quality = UDFUserSettings::Get().DecalQuality;
	if (!World || World->GetNetMode() == NM_DedicatedServer || Quality <= 0 || Normal.IsNearlyZero())
		return;
	// marks stay put: none on things that move (crates, vehicles), they'd hang in the air
	const FVector N = Normal.GetSafeNormal();
	FHitResult Surface;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFDecalSurface), false, Cast<AActor>(WorldContext));
	if (!World->LineTraceSingleByChannel(Surface, At + N * 4.f, At - N * 4.f, ECC_GameTraceChannel2, Params) || !Surface.GetComponent() || Surface.GetComponent()->Mobility == EComponentMobility::Movable)
		return;
	const int32 Variant = FMath::RandRange(0, Kind == EDecal::BulletHole ? 3 : 1);
	if (Kind == EDecal::Scorch && Quality >= 2) {
		UMaterialInterface* Material = DecalMaterial(Kind, Variant, true);
		if (Material) {
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ADFProjectedScorch* Decal = World->SpawnActor<ADFProjectedScorch>(At, FRotator::ZeroRotator, Spawn);
			if (Decal && Decal->Project(N, Size, Material)) {
				GScorches.RemoveAll([](const TWeakObjectPtr<ADFProjectedScorch>& W) { return !W.IsValid(); });
				GScorches.Add(Decal);
				if (GScorches.Num() > MaxScorches) {
					if (ADFProjectedScorch* Old = GScorches[0].Get())
						Old->Destroy();
					GScorches.RemoveAt(0);
				}
				return;
			}
			if (Decal)
				Decal->Destroy();
		}
		// Fall back to the simple mark if projection found no usable surface.
	}
	if (ADFDecalQuads* Q = ADFDecalQuads::Get(World)) {
		// flat on the surface, a hair off it; a random turn
		const FQuat Rot = FRotationMatrix::MakeFromZ(N).ToQuat() * FQuat(FVector::UpVector, FMath::FRand() * UE_TWO_PI);
		Q->Add(Variant, Kind, FTransform(Rot, At + N * 0.4f, FVector(Size / 100.f)));
	}
}

void DF::SpawnScorchBelow(const UObject* WorldContext, const FVector& Location, float Size, float Range) {
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
		return;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFScorch), false, Cast<AActor>(WorldContext)); // not the rocket itself
	// the Weapon channel: the surfaces bullets hit (the map's collision blocks it, players don't)
	if (World->LineTraceSingleByChannel(Hit, Location + FVector(0.f, 0.f, 50.f), Location - FVector(0.f, 0.f, Range), ECC_GameTraceChannel2, Params))
		SpawnDecal(WorldContext, EDecal::Scorch, Hit.ImpactPoint, Hit.ImpactNormal, Size);
}

ADFDecalQuads::ADFDecalQuads() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ADFDecalQuads* ADFDecalQuads::Get(UWorld* World) {
	if (TActorIterator<ADFDecalQuads> It(World); It)
		return *It;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ADFDecalQuads>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
}

void ADFDecalQuads::BeginPlay() {
	Super::BeginPlay();
	GetWorldTimerManager().SetTimer(ExpireTimer, this, &ADFDecalQuads::Expire, 2.f, true);
}

void ADFDecalQuads::Add(int32 Variant, DF::EDecal Kind, const FTransform& Where) {
	const FString Key = DecalTexture(Kind, Variant);
	FRing* Ring = Rings.Find(Key);
	if (!Ring) {
		UMaterialInterface* M = DecalMaterial(Kind, Variant, true);
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		if (!M || !Plane)
			return;
#if WITH_EDITOR
		// uncooked: the mesh builds in the background on first load, with no bounds yet (the
		// instances would be culled)
		FStaticMeshCompilingManager::Get().FinishCompilation({ Plane });
#endif
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetStaticMesh(Plane);
		C->SetMaterial(0, M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetCanEverAffectNavigation(false);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		Ring = &Rings.Add(Key, FRing{ C, {}, 0 });
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Ring->Born.Num() < MaxPerVariant) {
		Ring->Mesh->AddInstance(Where, true);
		Ring->Born.Add(Now);
	} else {
		Ring->Mesh->UpdateInstanceTransform(Ring->Next, Where, true, false);
		Ring->Born[Ring->Next] = Now;
		Ring->Next = (Ring->Next + 1) % MaxPerVariant;
	}
	// the bounds don't follow new instances by themselves: culled at the origin otherwise
	Ring->Mesh->UpdateBounds();
	Ring->Mesh->MarkRenderStateDirty();
}

void ADFDecalQuads::Expire() {
	// old marks shrink away (instances aren't removed: their slots get reused)
	const float Now = GetWorld()->GetTimeSeconds();
	for (auto& Pair : Rings) {
		FRing& Ring = Pair.Value;
		bool bChanged = false;
		for (int32 i = 0; i < Ring.Born.Num(); ++i) {
			if (Ring.Born[i] > 0.f && Now - Ring.Born[i] > Lifetime) {
				Ring.Born[i] = -1.f;
				FTransform T;
				Ring.Mesh->GetInstanceTransform(i, T, true);
				T.SetScale3D(FVector::ZeroVector);
				Ring.Mesh->UpdateInstanceTransform(i, T, true, false);
				bChanged = true;
			}
		}
		if (bChanged) {
			Ring.Mesh->UpdateBounds();
			Ring.Mesh->MarkRenderStateDirty();
		}
	}
}

ADFProjectedScorch::ADFProjectedScorch() {
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Scorch"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetCanEverAffectNavigation(false);
	RootComponent = Mesh;
	InitialLifeSpan = 30.f;
}

bool ADFProjectedScorch::Project(const FVector& Normal, float Size, UMaterialInterface* Material) {
	constexpr int32 Cells = 16;
	constexpr int32 Width = Cells + 1;
	const FVector N = Normal.GetSafeNormal();
	const FMatrix Basis = FRotationMatrix::MakeFromZ(N);
	const FQuat Rotation(N, FMath::FRand() * UE_TWO_PI);
	const FVector U = Rotation.RotateVector(Basis.GetUnitAxis(EAxis::X));
	const FVector V = Rotation.RotateVector(Basis.GetUnitAxis(EAxis::Y));
	const float Depth = Size * 0.5f;
	TArray<FVector> Vertices, Normals;
	TArray<FVector2D> UV;
	TArray<int32> Triangles;
	TArray<bool> Valid;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFScorchProjection), true, this);
	for (int32 Y = 0; Y <= Cells; ++Y)
		for (int32 X = 0; X <= Cells; ++X) {
			const FVector2D Tex(float(X) / Cells, float(Y) / Cells);
			const FVector Point = GetActorLocation() + Size * ((Tex.X - 0.5f) * U + (Tex.Y - 0.5f) * V);
			FHitResult Hit;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Point + N * Depth, Point - N * Depth, ECC_GameTraceChannel2, Params) && Hit.GetComponent() && Hit.GetComponent()->Mobility != EComponentMobility::Movable && FVector::DotProduct(Hit.ImpactNormal, N) > 0.1f;
			Valid.Add(bHit);
			Vertices.Add(bHit ? Hit.ImpactPoint + Hit.ImpactNormal * 0.4f - GetActorLocation() : FVector::ZeroVector);
			Normals.Add(bHit ? Hit.ImpactNormal : N);
			UV.Add(Tex);
		}
	auto Triangle = [&](int32 A, int32 B, int32 C) {
		// Do not bridge holes, cliffs or discontinuous surfaces.
		const float MaxEdge = FMath::Square(Size / Cells * 2.f);
		if (Valid[A] && Valid[B] && Valid[C] && FVector::DotProduct(Normals[A], Normals[B]) > 0.99f && FVector::DotProduct(Normals[A], Normals[C]) > 0.99f && FMath::Abs(FVector::DotProduct(Vertices[B] - Vertices[A], Normals[A])) < 1.f && FMath::Abs(FVector::DotProduct(Vertices[C] - Vertices[A], Normals[A])) < 1.f && FVector::DistSquared(Vertices[A], Vertices[B]) <= MaxEdge && FVector::DistSquared(Vertices[B], Vertices[C]) <= MaxEdge && FVector::DistSquared(Vertices[C], Vertices[A]) <= MaxEdge)
			Triangles.Append({ A, C, B });
	};
	for (int32 Y = 0; Y < Cells; ++Y)
		for (int32 X = 0; X < Cells; ++X) {
			const int32 A = Y * Width + X;
			Triangle(A, A + 1, A + Width);
			Triangle(A + 1, A + Width + 1, A + Width);
		}
	if (Triangles.IsEmpty())
		return false;
	Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV, {}, {}, false);
	Mesh->SetMaterial(0, Material);
	return true;
}
