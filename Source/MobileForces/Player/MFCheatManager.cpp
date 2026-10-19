#include "Player/MFCheatManager.h"
#include "MobileForces.h"
#include "World/MFLadder.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

void UMFCheatManager::MFNavTest()
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	APlayerController* PC = GetPlayerController();
	APawn* P = PC ? PC->GetPawn() : nullptr;
	if (!Nav || !P)
	{
		return;
	}
	for (TActorIterator<AMFLadderVolume> It(GetWorld()); It; ++It)
	{
		const FVector Bottom = It->GetActorTransform().TransformPosition(It->LinkBottom);
		const FVector Top = It->GetActorTransform().TransformPosition(It->LinkTop);
		FNavLocation B, T;
		const bool bB = Nav->ProjectPointToNavigation(Bottom, B, FVector(60, 60, 100));
		const bool bT = Nav->ProjectPointToNavigation(Top, T, FVector(60, 60, 100));
		const UNavigationPath* Up = Nav->FindPathToLocationSynchronously(GetWorld(), B.Location, T.Location, P);
		UE_LOG(LogMF, Display, TEXT("NavTest ladder %s: bottom %s (nav %d) top %s (nav %d) path valid %d partial %d len %.0f"), *It->GetName(),
			*Bottom.ToCompactString(), bB, *Top.ToCompactString(), bT, Up && Up->IsValid(), Up && Up->IsPartial(), Up ? Up->GetPathLength() : -1.f);
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->IsA<APlayerStart>() && !It->GetClass()->GetName().Contains(TEXT("HoldoutPoint")))
		{
			continue;
		}
		const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(), P->GetActorLocation(), It->GetActorLocation(), P);
		FNavLocation Proj;
		const bool bOnNav = Nav->ProjectPointToNavigation(It->GetActorLocation(), Proj);
		const FVector End = Path && Path->PathPoints.Num() ? Path->PathPoints.Last() : FVector::ZeroVector;
		UE_LOG(LogMF, Display, TEXT("NavTest %s at %s: onnav %d (proj dz %.0f) valid %d partial %d len %.0f end %s gap %.0f"), *It->GetName(),
			*It->GetActorLocation().ToCompactString(), bOnNav, bOnNav ? Proj.Location.Z - It->GetActorLocation().Z : 0.f,
			Path && Path->IsValid(), Path && Path->IsPartial(), Path ? Path->GetPathLength() : -1.f, *End.ToCompactString(),
			FVector::Dist(End, It->GetActorLocation()));
		if (Path && Path->IsPartial())
		{
			// What stops us: sweep a slim capsule from the path end towards the target
			FHitResult Hit;
			const FVector From = End + FVector(0, 0, 100.f);
			const FVector To = FVector(It->GetActorLocation().X, It->GetActorLocation().Y, From.Z);
			if (GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(20.f, 60.f), FCollisionQueryParams(NAME_None, false, P)))
			{
				const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Hit.GetComponent());
				UE_LOG(LogMF, Display, TEXT("NavTest   blocked by %s (mesh %s) at %s (normal %s, startpen %d)"), Hit.GetActor() ? *Hit.GetActor()->GetActorNameOrLabel() : TEXT("-"),
					SMC ? *GetNameSafe(SMC->GetStaticMesh()) : TEXT("-"), *Hit.ImpactPoint.ToCompactString(), *Hit.ImpactNormal.ToCompactString(), Hit.bStartPenetrating);
			}
			else
			{
				UE_LOG(LogMF, Display, TEXT("NavTest   nothing solid between path end and target (navmesh gap)"));
			}
		}
	}
}

void UMFCheatManager::MFCam(float X, float Y, float Z, float Yaw, float Pitch)
{
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(FVector(X, Y, Z), FRotator(Pitch, Yaw, 0.f), SP);
	Cam->GetCameraComponent()->SetFieldOfView(80.f);
	Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
	GetPlayerController()->SetViewTarget(Cam);
}

void UMFCheatManager::MFTopView(float X, float Y, float Width, float Z)
{
	if (Width <= 0.f)
	{
		GetPlayerController()->SetViewTarget(GetPlayerController()->GetPawn());
		return;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(FVector(X, Y, Z != 0.f ? Z : 20000.f), FRotator(-90.f, 0.f, 0.f), SP);
	UCameraComponent* CC = Cam->GetCameraComponent();
	CC->SetProjectionMode(ECameraProjectionMode::Orthographic);
	CC->SetOrthoWidth(Width);
	CC->SetConstraintAspectRatio(false);
	// Fixed planes: roofs above a low camera disappear (UE5 otherwise fits the planes to the
	// whole scene). Note: the cut isn't exactly at the camera height in 5.8, so use it to
	// remove roofs, not to slice storeys.
	CC->SetAutoCalculateOrthoPlanes(false);
	CC->SetUpdateOrthoPlanes(false);
	CC->SetOrthoNearClipPlane(0.f);
	CC->SetOrthoFarClipPlane(Z != 0.f ? 6000.f : 40000.f);
	GetPlayerController()->SetViewTarget(Cam);
}
