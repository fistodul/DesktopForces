#include "World/MFDecals.h"
#include "Core/MFUserSettings.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
	const TCHAR* DecalTexture(MF::EDecal Kind, int32 Variant)
	{
		static const TCHAR* Holes[] = { TEXT("BulletHole0"), TEXT("BulletHole1"), TEXT("BulletHole2"), TEXT("BulletHole3") };
		static const TCHAR* Scorches[] = { TEXT("Scorch0"), TEXT("Scorch1") };
		return Kind == MF::EDecal::BulletHole ? Holes[Variant % 4] : Scorches[Variant % 2];
	}

	UMaterialInterface* DecalMaterial(MF::EDecal Kind, int32 Variant, bool bQuad)
	{
		const FString Name = FString::Printf(TEXT("MI_MF_%s%s"), DecalTexture(Kind, Variant), bQuad ? TEXT("_Quad") : TEXT(""));
		return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/MF/Materials/Decals/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	/** Live scorch decals, oldest first (a cap keeps the DBuffer cheap). */
	TArray<TWeakObjectPtr<UDecalComponent>> GScorches;
	constexpr int32 MaxScorches = 24;
}

void MF::SpawnDecal(const UObject* WorldContext, EDecal Kind, const FVector& At, const FVector& Normal, float Size)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UMFUserSettings* S = UMFUserSettings::Get();
	const int32 Quality = S ? S->DecalQuality : 2;
	if (!World || World->GetNetMode() == NM_DedicatedServer || Quality <= 0 || Normal.IsNearlyZero())
	{
		return;
	}
	// marks stay put: none on things that move (crates, vehicles), they'd hang in the air
	const FVector N = Normal.GetSafeNormal();
	FHitResult Surface;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFDecalSurface), false, Cast<AActor>(WorldContext));
	if (!World->LineTraceSingleByChannel(Surface, At + N * 4.f, At - N * 4.f, ECC_GameTraceChannel2, Params)
		|| !Surface.GetComponent() || Surface.GetComponent()->Mobility == EComponentMobility::Movable)
	{
		return;
	}
	const int32 Variant = FMath::RandRange(0, Kind == EDecal::BulletHole ? 3 : 1);
	// projected decals render into the DBuffer, which is off unless r.DBuffer is set (the
	// forward renderer draws them only there): flat quads otherwise
	static const IConsoleVariable* DBuffer = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DBuffer"));
	if (Kind == EDecal::Scorch && Quality >= 2 && DBuffer && DBuffer->GetInt() != 0)
	{
		UMaterialInterface* M = DecalMaterial(Kind, Variant, false);
		if (!M)
		{
			return;
		}
		// projects along its X axis, into the surface; a random turn so marks don't repeat
		FRotator Rot = (-N).Rotation();
		Rot.Roll = FMath::FRand() * 360.f;
		if (UDecalComponent* D = UGameplayStatics::SpawnDecalAtLocation(World, M, FVector(Size * 0.25f, Size * 0.5f, Size * 0.5f), At, Rot, 30.f))
		{
			D->SetFadeOut(25.f, 5.f, false);
			D->SetFadeScreenSize(0.002f);
			GScorches.RemoveAll([](const TWeakObjectPtr<UDecalComponent>& W) { return !W.IsValid(); });
			GScorches.Add(D);
			if (GScorches.Num() > MaxScorches)
			{
				if (UDecalComponent* Old = GScorches[0].Get())
				{
					Old->DestroyComponent();
				}
				GScorches.RemoveAt(0);
			}
		}
		return;
	}
	if (AMFDecalQuads* Q = AMFDecalQuads::Get(World))
	{
		// flat on the surface, a hair off it; a random turn
		const FQuat Rot = FRotationMatrix::MakeFromZ(N).ToQuat() * FQuat(FVector::UpVector, FMath::FRand() * UE_TWO_PI);
		Q->Add(Variant, Kind, FTransform(Rot, At + N * 0.4f, FVector(Size / 100.f)));
	}
}

void MF::SpawnScorchBelow(const UObject* WorldContext, const FVector& Location, float Size, float Range)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFScorch), false, Cast<AActor>(WorldContext));   // not the rocket itself
	// the Weapon channel: the surfaces bullets hit (the map's collision blocks it, players don't)
	if (World->LineTraceSingleByChannel(Hit, Location + FVector(0.f, 0.f, 50.f), Location - FVector(0.f, 0.f, Range), ECC_GameTraceChannel2, Params))
	{
		SpawnDecal(WorldContext, EDecal::Scorch, Hit.ImpactPoint, Hit.ImpactNormal, Size);
	}
}

AMFDecalQuads::AMFDecalQuads()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AMFDecalQuads* AMFDecalQuads::Get(UWorld* World)
{
	if (TActorIterator<AMFDecalQuads> It(World); It)
	{
		return *It;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AMFDecalQuads>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
}

void AMFDecalQuads::BeginPlay()
{
	Super::BeginPlay();
	GetWorldTimerManager().SetTimer(ExpireTimer, this, &AMFDecalQuads::Expire, 2.f, true);
}

void AMFDecalQuads::Add(int32 Variant, MF::EDecal Kind, const FTransform& Where)
{
	const FString Key = DecalTexture(Kind, Variant);
	FRing* Ring = Rings.Find(Key);
	if (!Ring)
	{
		UMaterialInterface* M = DecalMaterial(Kind, Variant, true);
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		if (!M || !Plane)
		{
			return;
		}
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
	if (Ring->Born.Num() < MaxPerVariant)
	{
		Ring->Mesh->AddInstance(Where, true);
		Ring->Born.Add(Now);
	}
	else
	{
		Ring->Mesh->UpdateInstanceTransform(Ring->Next, Where, true, false);
		Ring->Born[Ring->Next] = Now;
		Ring->Next = (Ring->Next + 1) % MaxPerVariant;
	}
	// the bounds don't follow new instances by themselves: culled at the origin otherwise
	Ring->Mesh->UpdateBounds();
	Ring->Mesh->MarkRenderStateDirty();
}

void AMFDecalQuads::Expire()
{
	// old marks shrink away (instances aren't removed: their slots get reused)
	const float Now = GetWorld()->GetTimeSeconds();
	for (auto& Pair : Rings)
	{
		FRing& Ring = Pair.Value;
		bool bChanged = false;
		for (int32 i = 0; i < Ring.Born.Num(); ++i)
		{
			if (Ring.Born[i] > 0.f && Now - Ring.Born[i] > Lifetime)
			{
				Ring.Born[i] = -1.f;
				FTransform T;
				Ring.Mesh->GetInstanceTransform(i, T, true);
				T.SetScale3D(FVector::ZeroVector);
				Ring.Mesh->UpdateInstanceTransform(i, T, true, false);
				bChanged = true;
			}
		}
		if (bChanged)
		{
			Ring.Mesh->UpdateBounds();
			Ring.Mesh->MarkRenderStateDirty();
		}
	}
}
