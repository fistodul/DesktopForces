#include "Player/DFCheatManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Weapons/DFProjectile.h"
#include "World/DFDecals.h"
#include "Materials/MaterialInterface.h"
#include "Engine/DamageEvents.h"
#include "TimerManager.h"
#include "DesktopForces.h"
#include "Core/DFTypes.h"
#include "Game/DFGameMode.h"
#include "Algo/AnyOf.h"
#include "Misc/StringOutputDevice.h"
#include "GameFramework/GameSession.h"
#include "Game/Zombie/ZombieGame.h"
#include "Game/DFGameState.h"
#include "Game/DFPlayerState.h"
#include "Loadout/DFLoadout.h"
#include "Engine/OverlapResult.h"
#include "Player/DFCharacter.h"
#include "Player/DFCharacterMovement.h"
#include "Player/DFPlayerController.h"
#include "Vehicles/DFVehicle.h"
#include "Weapons/DFWeapon.h"
#include "World/DFLadder.h"
#include "World/DFPitFinder.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Camera/CameraActor.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "SceneInterface.h"
#include "Engine/MapBuildDataRegistry.h"
#include "LightMap.h"
#include "StaticMeshComponentLODInfo.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "DrawDebugHelpers.h"

void UDFCheatManager::DFStartProbe() {
	UWorld* World = GetWorld();
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav)
		return;
	int32 Starts = 0, Off = 0;
	for (TActorIterator<APlayerStart> It(World); It; ++It) {
		++Starts;
		const FVector P = It->GetActorLocation();
		FNavLocation Spot;
		if (Nav->ProjectPointToNavigation(P, Spot, FVector(100.f, 100.f, 700.f)))
			continue;
		++Off;
		FString Near = Algo::AnyOf(Nav->GetNavigationBounds(), [&](const FNavigationBounds& B) { return B.AreaBox.IsInsideOrOn(P); }) ? TEXT("none in 8 m") : TEXT("none, outside the bounds");
		if (Nav->ProjectPointToNavigation(P, Spot, FVector(800.f)))
			Near = FString::Printf(TEXT("%.0f away, %+.0f z"), FVector::Dist2D(Spot.Location, P), Spot.Location.Z - P.Z);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DFStartProbe), false);
		// distance, actor, mesh, surface normal z and whether the hit part feeds the navmesh
		auto Trace = [&](const FVector& To) {
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, P, To, ECC_Pawn, Params))
				return FString(TEXT("-"));
			const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Hit.GetComponent());
			return FString::Printf(TEXT("%.0f %s (%s, nz %.2f, nav %d)"), Hit.Distance, *GetNameSafe(Hit.GetActor()), SMC ? *GetNameSafe(SMC->GetStaticMesh()) : TEXT("-"),
				Hit.ImpactNormal.Z, Hit.GetComponent() && Hit.GetComponent()->CanEverAffectNavigation());
		};
		// what a soldier standing here (capsule 38.25 x 87.75) would be inside of
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByChannel(Overlaps, P, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(38.25f, 87.75f), Params);
		FString Inside;
		for (const FOverlapResult& O : Overlaps)
			if (O.bBlockingHit)
				Inside += GetNameSafe(O.GetActor()) + TEXT(" ");
		float Crate = -1.f;
		for (TActorIterator<ALoadoutCrate> C(World); C; ++C)
			if (Crate < 0.f || FVector::Dist(C->GetActorLocation(), P) < Crate)
				Crate = FVector::Dist(C->GetActorLocation(), P);
		UE_LOG(LogDF, Display, TEXT("StartProbe %s at %s: nav %s | up %s | down %s | inside %s| crate %.0f"), *It->GetName(), *P.ToCompactString(), *Near,
			*Trace(P + FVector(0.f, 0.f, 400.f)), *Trace(P - FVector(0.f, 0.f, 1000.f)), Inside.IsEmpty() ? TEXT("- ") : *Inside, Crate);
		// everything solid within 2 m (what Recast saw around the spot)
		Overlaps.Reset();
		World->OverlapMultiByChannel(Overlaps, P, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeBox(FVector(200.f)), Params);
		FString Around;
		for (const FOverlapResult& O : Overlaps) {
			const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(O.GetComponent());
			if (O.bBlockingHit)
				Around += FString::Printf(TEXT("%s(%s) "), *GetNameSafe(O.GetActor()), SMC ? *GetNameSafe(SMC->GetStaticMesh()) : *GetNameSafe(O.GetComponent()));
		}
		UE_LOG(LogDF, Display, TEXT("StartProbe   within 2 m: %s"), *Around);
	}
	UE_LOG(LogDF, Display, TEXT("StartProbe: %d / %d player starts off the navmesh"), Off, Starts);
}

void UDFCheatManager::DFNavDraw(float X, float Y, float Z, float Radius) {
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const ARecastNavMesh* Recast = Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance()) : nullptr;
	if (!Recast)
		return;
	const FVector Center(X, Y, Z);
	int32 Drawn = 0;
	TArray<FNavPoly> Polys;
	TArray<FVector> Verts;
	Recast->GetPolysInBox(FBox::BuildAABB(Center, FVector(Radius)), Polys);
	for (const FNavPoly& Poly : Polys) {
		if (!Recast->GetPolyVerts(Poly.Ref, Verts))
			continue;
		++Drawn;
		for (int32 I = 0; I < Verts.Num(); ++I)
			DrawDebugLine(GetWorld(), Verts[I] + FVector(0, 0, 5), Verts[(I + 1) % Verts.Num()] + FVector(0, 0, 5), FColor::Green, true, -1.f, 0, 3.f);
	}
	UE_LOG(LogDF, Display, TEXT("DFNavDraw: %d polys within %.0f of %s"), Drawn, Radius, *Center.ToCompactString());
}

void UDFCheatManager::DFNavGrid(float X, float Y, float Z, float Half, float Step) {
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav || Step <= 0.f)
		return;
	UE_LOG(LogDF, Display, TEXT("DFNavGrid around %.0f %.0f %.0f, %.0f cm cells, +X right, +Y down"), X, Y, Z, Step);
	for (float DY = -Half; DY <= Half; DY += Step) {
		FString Row;
		for (float DX = -Half; DX <= Half; DX += Step) {
			FNavLocation L;
			const bool bOn = Nav->ProjectPointToNavigation(FVector(X + DX, Y + DY, Z), L, FVector(Step / 2.f, Step / 2.f, 300.f));
			const bool bCentre = FMath::Abs(DX) < Step / 2.f && FMath::Abs(DY) < Step / 2.f;
			Row += bCentre ? TEXT("S") : (bOn ? TEXT("#") : TEXT("."));
		}
		UE_LOG(LogDF, Display, TEXT("DFNavGrid %s"), *Row);
	}
}

void UDFCheatManager::DFNavTest() {
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	APlayerController* PC = GetPlayerController();
	APawn* P = PC ? PC->GetPawn() : nullptr;
	if (!Nav || !P)
		return;
	for (TActorIterator<ADFLadderVolume> It(GetWorld()); It; ++It) {
		const FVector Bottom = It->GetActorTransform().TransformPosition(It->LinkBottom);
		const FVector Top = It->GetActorTransform().TransformPosition(It->LinkTop);
		FNavLocation B, T;
		const bool bB = Nav->ProjectPointToNavigation(Bottom, B, FVector(60, 60, 100));
		const bool bT = Nav->ProjectPointToNavigation(Top, T, FVector(60, 60, 100));
		const UNavigationPath* Up = Nav->FindPathToLocationSynchronously(GetWorld(), B.Location, T.Location, P);
		UE_LOG(LogDF, Display, TEXT("NavTest ladder %s: bottom %s (nav %d) top %s (nav %d) path valid %d partial %d len %.0f"), *It->GetName(),
			*Bottom.ToCompactString(), bB, *Top.ToCompactString(), bT, Up && Up->IsValid(), Up && Up->IsPartial(), Up ? Up->GetPathLength() : -1.f);
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It) {
		if (!It->IsA<APlayerStart>() && !It->GetClass()->GetName().Contains(TEXT("HoldoutPoint")))
			continue;
		const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(), P->GetActorLocation(), It->GetActorLocation(), P);
		FNavLocation Proj;
		const bool bOnNav = Nav->ProjectPointToNavigation(It->GetActorLocation(), Proj);
		const FVector End = Path && Path->PathPoints.Num() ? Path->PathPoints.Last() : FVector::ZeroVector;
		UE_LOG(LogDF, Display, TEXT("NavTest %s at %s: onnav %d (proj dz %.0f) valid %d partial %d len %.0f end %s gap %.0f"), *It->GetName(),
			*It->GetActorLocation().ToCompactString(), bOnNav, bOnNav ? Proj.Location.Z - It->GetActorLocation().Z : 0.f,
			Path && Path->IsValid(), Path && Path->IsPartial(), Path ? Path->GetPathLength() : -1.f, *End.ToCompactString(),
			FVector::Dist(End, It->GetActorLocation()));
		if (Path && Path->IsPartial()) {
			// What stops us: sweep a slim capsule from the path end towards the target
			FHitResult Hit;
			const FVector From = End + FVector(0, 0, 100.f);
			const FVector To = FVector(It->GetActorLocation().X, It->GetActorLocation().Y, From.Z);
			if (GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(20.f, 60.f), FCollisionQueryParams(NAME_None, false, P))) {
				const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Hit.GetComponent());
				UE_LOG(LogDF, Display, TEXT("NavTest   blocked by %s (mesh %s) at %s (normal %s, startpen %d)"), Hit.GetActor() ? *Hit.GetActor()->GetActorNameOrLabel() : TEXT("-"),
					SMC ? *GetNameSafe(SMC->GetStaticMesh()) : TEXT("-"), *Hit.ImpactPoint.ToCompactString(), *Hit.ImpactNormal.ToCompactString(), Hit.bStartPenetrating);
			} else {
				UE_LOG(LogDF, Display, TEXT("NavTest   nothing solid between path end and target (navmesh gap)"));
			}
		}
	}
}

void UDFCheatManager::DFNavPath(float X, float Y, float Z) {
	APawn* P = GetPlayerController()->GetPawn();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!P || !Nav)
		return;
	for (const float Ext : { 100.f, 1000.f, 10000.f }) {
		FNavLocation Proj;
		const bool bOn = Nav->ProjectPointToNavigation(P->GetActorLocation(), Proj, FVector(Ext, Ext, Ext));
		UE_LOG(LogDF, Display, TEXT("DFNavPath: project within %.0f: %d %s"), Ext, bOn ? 1 : 0, *Proj.Location.ToCompactString());
	}
	UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(), P->GetActorLocation(), FVector(X, Y, Z), P);
	if (!Path || !Path->GetPath().IsValid()) {
		UE_LOG(LogDF, Display, TEXT("DFNavPath: no path"));
		return;
	}
	UE_LOG(LogDF, Display, TEXT("DFNavPath: %d points, length %.0f, partial %d"), Path->PathPoints.Num(), Path->GetPathLength(), Path->IsPartial());
	const TArray<FNavPathPoint>& Points = Path->GetPath()->GetPathPoints();
	for (int32 i = 0; i < Points.Num(); ++i) {
		const bool bLink = Points[i].CustomNavLinkId != FNavLinkId::Invalid;
		UE_LOG(LogDF, Display, TEXT("DFNavPath: %d %s%s"), i, *Points[i].Location.ToString(), bLink ? TEXT(" (link)") : TEXT(""));
	}
}

void UDFCheatManager::DFHurt(int32 N) {
	if (APawn* P = GetPlayerController()->GetPawn())
		P->TakeDamage(N, FDamageEvent(), nullptr, nullptr);
}

void UDFCheatManager::DFHurtEnemies(int32 N) {
	const ADFCharacter* Me = Cast<ADFCharacter>(GetPlayerController()->GetPawn());
	TArray<ADFCharacter*> Targets;
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
		if (*It != Me && It->IsAlive() && (!Me || It->GetTeam() == DF::NoTeam || It->GetTeam() != Me->GetTeam()))
			Targets.Add(*It);
	for (ADFCharacter* C : Targets) {
		FDamageEvent Ev(UDFDamage_Bullet::StaticClass());
		C->TakeDamage(float(N), Ev, GetPlayerController(), GetPlayerController()->GetPawn());
	}
}

void UDFCheatManager::DFTouch(FName Action, int32 bPressed) {
	if (ADFPlayerController* PC = Cast<ADFPlayerController>(GetPlayerController()))
		PC->TouchAction(Action, bPressed != 0);
}

void UDFCheatManager::DFFire(int32 Alt, float Seconds) {
	if (ADFCharacter* C = Cast<ADFCharacter>(GetPlayerController()->GetPawn())) {
		const bool bAlt = Alt != 0;
		C->StartFire(bAlt);
		TWeakObjectPtr<ADFCharacter> Weak = C;
		FTimerHandle H;
		GetWorld()->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([Weak, bAlt]() {
			if (Weak.IsValid())
				Weak->StopFire(bAlt);
		}),
			FMath::Max(Seconds, 0.05f), false);
	}
}

void UDFCheatManager::DFWalk(float Yaw, float Seconds, float Forward, float Right, float Up) {
	if (ADFPlayerController* PC = Cast<ADFPlayerController>(GetPlayerController())) {
		PC->SetControlRotation(FRotator(PC->GetControlRotation().Pitch, Yaw, 0.f));
		PC->ForceMove(FVector2D(Forward, Right), Seconds, Up);
	}
}

void UDFCheatManager::DFTeleport(float X, float Y, float Z) {
	if (ADFVehicle* V = Cast<ADFVehicle>(GetPlayerController()->GetPawn())) {
		// a vehicle takes its trailer along (the hitch would pull them back together)
		const FVector Offset = FVector(X, Y, Z) - V->GetActorLocation();
		V->TeleportTo(FVector(X, Y, Z), V->GetActorRotation(), false, true);
		if (ADFVehicle* T = V->GetTrailer())
			T->TeleportTo(T->GetActorLocation() + Offset, T->GetActorRotation(), false, true);
		return;
	}
	if (APawn* P = GetPlayerController()->GetPawn()) {
		if (!P->TeleportTo(FVector(X, Y, Z), P->GetActorRotation()))
			UE_LOG(LogDF, Warning, TEXT("DFTeleport: (%.0f %.0f %.0f) blocked"), X, Y, Z);
	}
}

void UDFCheatManager::DFTraceMat() {
	FVector Loc;
	FRotator Rot;
	GetPlayerController()->GetPlayerViewPoint(Loc, Rot);
	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("DFTraceMat"), true, GetPlayerController()->GetPawn());
	Params.bReturnFaceIndex = true;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Loc, Loc + Rot.Vector() * 100000.f, ECC_Visibility, Params)) {
		UE_LOG(LogDF, Display, TEXT("DFTraceMat: nothing"));
		return;
	}
	int32 Section = -1;
	UMaterialInterface* Mat = Hit.GetComponent() ? Hit.GetComponent()->GetMaterialFromCollisionFaceIndex(Hit.FaceIndex, Section) : nullptr;
	UE_LOG(LogDF, Display, TEXT("DFTraceMat: %s at %s dist %.0f face %d section %d material %s"),
		*GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToString(), Hit.Distance, Hit.FaceIndex, Section, *GetPathNameSafe(Mat));
	// the world's render meshes have no collision (separate collision meshes): list the
	// visible meshes whose bounds hold the hit point, with their materials
	auto LogErrors = [this](const UMaterialInterface* M) {
#if WITH_EDITOR // compile errors are kept in editor builds only
		const UMaterial* Base = M ? M->GetMaterial() : nullptr;
		const FMaterialResource* Res = Base && GetWorld()->Scene ? Base->GetMaterialResource(GetWorld()->Scene->GetShaderPlatform()) : nullptr;
		if (Res)
			for (const FString& E : Res->GetCompileErrors())
				UE_LOG(LogDF, Display, TEXT("DFTraceMat:     compile error: %s"), *E);
#endif
	};
	LogErrors(Mat);
	const UStaticMeshComponent* HitMesh = Cast<UStaticMeshComponent>(Hit.GetComponent());
	if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(HitMesh))
		UE_LOG(LogDF, Display, TEXT("DFTraceMat:   instanced: %d instances, bounds %s"), ISM->GetInstanceCount(), *ISM->Bounds.GetBox().ToString());
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It) {
		if (It->GetWorld() == GetWorld() && It->IsVisible() && (*It == HitMesh || It->Bounds.GetBox().ExpandBy(2.f).IsInside(Hit.ImpactPoint))) {
			UE_LOG(LogDF, Display, TEXT("DFTraceMat:   %s (%s)"), *It->GetName(), *GetNameSafe(It->GetStaticMesh()));
			const FMeshMapBuildData* BuildData = It->LODData.Num() ? It->GetMeshMapBuildData(It->LODData[0]) : nullptr;
			const FLightMap* LM = BuildData ? BuildData->LightMap.GetReference() : nullptr;
			if (!LM) {
				UE_LOG(LogDF, Display, TEXT("DFTraceMat:     no lightmap (build data %s)"), BuildData ? TEXT("yes") : TEXT("no"));
			} else {
				const FLightMapInteraction I = LM->GetInteraction(GetWorld()->GetFeatureLevel());
				const FVector4f* Scale = I.GetType() == LMIT_Texture ? I.GetScaleArray() : nullptr;
				const FVector4f* Add = I.GetType() == LMIT_Texture ? I.GetAddArray() : nullptr;
				UE_LOG(LogDF, Display, TEXT("DFTraceMat:     lightmap type %d scale0 %s add0 %s scale1 %s add1 %s tex %s"), (int32)I.GetType(),
					Scale ? *Scale[0].ToString() : TEXT("-"), Add ? *Add[0].ToString() : TEXT("-"),
					Scale ? *Scale[1].ToString() : TEXT("-"), Add ? *Add[1].ToString() : TEXT("-"),
					I.GetType() == LMIT_Texture ? *GetNameSafe(I.GetTexture(true)) : TEXT("-"));
			}
			for (int32 i = 0; i < It->GetNumMaterials(); ++i) {
				UE_LOG(LogDF, Display, TEXT("DFTraceMat:     [%d] %s"), i, *GetPathNameSafe(It->GetMaterial(i)));
				LogErrors(It->GetMaterial(i));
			}
		}
	}
}

void UDFCheatManager::DFCam(float X, float Y, float Z, float Yaw, float Pitch) {
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(FVector(X, Y, Z), FRotator(Pitch, Yaw, 0.f), SP);
	Cam->GetCameraComponent()->SetFieldOfView(80.f);
	Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
	GetPlayerController()->SetViewTarget(Cam);
}

void UDFCheatManager::DFOrbit(float Yaw, float Dist, int32 Other) {
	APawn* P = GetPlayerController()->GetPawn();
	if (Other && P) {
		APawn* Me = P;
		P = nullptr;
		for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
			if (*It != Me && It->IsAlive() && (!P || FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(P->GetActorLocation(), Me->GetActorLocation())))
				P = *It;
	}
	if (!P)
		return;
	const FRotator Facing(0.f, P->GetActorRotation().Yaw + Yaw, 0.f);
	const FVector At = P->GetActorLocation() + Facing.Vector() * Dist + FVector(0.f, 0.f, 20.f);
	DFCam(At.X, At.Y, At.Z, (P->GetActorLocation() - At).Rotation().Yaw, -5.f);
	if (AActor* Cam = GetPlayerController()->GetViewTarget())
		Cam->AttachToActor(P, FAttachmentTransformRules::KeepWorldTransform);
}

void UDFCheatManager::DFEnterSeat(int32 Seat) {
	ADFCharacter* C = Cast<ADFCharacter>(GetPlayerController()->GetPawn());
	ADFVehicle* Best = nullptr;
	for (TActorIterator<ADFVehicle> It(GetWorld()); It && C; ++It)
		if (It->IsNear(C) && (!Best || FVector::Dist(It->GetActorLocation(), C->GetActorLocation()) < FVector::Dist(Best->GetActorLocation(), C->GetActorLocation())))
			Best = *It;
	if (Best)
		Best->EnterSeat(C, Seat);
}

void UDFCheatManager::DFVehicles() {
	for (TActorIterator<ADFVehicle> It(GetWorld()); It; ++It)
		It->DebugDump();
}

void UDFCheatManager::DFTopView(float X, float Y, float Width, float Z) {
	if (Width <= 0.f) {
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

// --- Mobile Forces cheats ---------------------------------------------------------------
// MF allowed these only with cheats on and in single player (bGreatDane, NM_Standalone);
// UE's cheat manager has the same rule (standalone, or `EnableCheats` on a listen server).

namespace {
	TSubclassOf<ADFWeapon> FindWeaponClass(const FString& Name) {
		// "Shotgun", "Shotgun" or a full path
		if (UClass* C = LoadClass<ADFWeapon>(nullptr, *Name, nullptr, LOAD_NoWarn | LOAD_Quiet))
			return C;
		UClass* Best = nullptr;
		for (TObjectIterator<UClass> It; It; ++It) {
			if (!It->IsChildOf(ADFWeapon::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract))
				continue;
			const FString N = It->GetName();
			if (N.Equals(Name, ESearchCase::IgnoreCase))
				return *It;
			if (!Best && N.Contains(Name))
				Best = *It;
		}
		return Best;
	}
} // namespace

void UDFCheatManager::SetTeam(int32 N) {
	APlayerController* PC = GetPlayerController();
	ADFPlayerState* PS = PC ? PC->GetPlayerState<ADFPlayerState>() : nullptr;
	ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>();
	const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
	if (!PS || !GM || !GS || !GS->IsTeamGame() || N < 0 || N >= GS->NumTeams)
		return;
	// RagePlayer.SetTeam -> ChangeTeam: new team, fresh spawn on its side
	PS->SetTeam(uint8(N));
	if (APawn* P = PC->GetPawn())
		P->Destroy();
	GM->RestartPlayer(PC);
}

void UDFCheatManager::KillEnemy() {
	const ADFCharacter* Me = Cast<ADFCharacter>(GetPlayerController()->GetPawn());
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
		if (*It != Me && It->IsAlive() && (!Me || It->GetTeam() == DF::NoTeam || It->GetTeam() != Me->GetTeam()))
			It->Die(GetPlayerController(), UDFDamage_Suicide::StaticClass(), nullptr);
}

void UDFCheatManager::KillPawns() {
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
		if (It->IsAlive() && !It->IsPlayerControlled())
			It->Die(nullptr, UDFDamage_Suicide::StaticClass(), nullptr);
}

void UDFCheatManager::TeamGod() {
	const ADFCharacter* Me = Cast<ADFCharacter>(GetPlayerController()->GetPawn());
	if (!Me)
		return;
	const bool bGod = Me->CanBeDamaged(); // toggle from the player's own state
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
		if (It->GetTeam() == Me->GetTeam())
			It->SetCanBeDamaged(!bGod);
	GetPlayerController()->ClientMessage(bGod ? TEXT("Team god mode on") : TEXT("Team god mode off"));
}

void UDFCheatManager::AllAmmo() {
	if (ADFCharacter* C = Cast<ADFCharacter>(GetPlayerController()->GetPawn()))
		C->RefillAmmo();
}

void UDFCheatManager::SetTime(int32 Seconds) {
	if (ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>(); GS && GS->TimeLimit > 0)
		GS->ElapsedTime = FMath::Max(0, GS->TimeLimit * 60 - Seconds); // RagePlayer.settime
}

void UDFCheatManager::SetWeapons(const FString& WeaponName) {
	const TSubclassOf<ADFWeapon> Class = FindWeaponClass(WeaponName);
	if (!Class)
		return;
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It) {
		if (!It->IsPlayerControlled() && It->IsAlive()) {
			if (ADFWeapon* W = It->GiveWeapon(Class, false))
				It->EquipWeapon(W);
		}
	}
}

void UDFCheatManager::GiveWeapon(const FString& WeaponName) {
	ADFCharacter* C = Cast<ADFCharacter>(GetPlayerController()->GetPawn());
	const TSubclassOf<ADFWeapon> Class = FindWeaponClass(WeaponName);
	if (C && Class) {
		if (ADFWeapon* W = C->GiveWeapon(Class, false, true)) // a cheat ignores the carry limit
			C->EquipWeapon(W);
	}
}

void UDFCheatManager::SetSpeed(float Multiplier) {
	if (ADFCharacter* C = Cast<ADFCharacter>(GetPlayerController()->GetPawn())) {
		UCharacterMovementComponent* M = C->GetCharacterMovement();
		const UCharacterMovementComponent* Def = GetDefault<UDFCharacterMovement>();
		M->MaxWalkSpeed = Def->MaxWalkSpeed * Multiplier;
		M->MaxWalkSpeedCrouched = Def->MaxWalkSpeedCrouched * Multiplier;
	}
}

void UDFCheatManager::SetJumpZ(float Velocity) {
	if (ADFCharacter* C = Cast<ADFCharacter>(GetPlayerController()->GetPawn()))
		C->GetCharacterMovement()->JumpZVelocity = Velocity;
}

void UDFCheatManager::DFAimAt(float X, float Y, float Z) {
	APlayerController* PC = GetPlayerController();
	APawn* P = PC ? PC->GetPawn() : nullptr;
	if (!P)
		return;
	FVector From = P->GetActorLocation();
	if (const ADFCharacter* C = Cast<ADFCharacter>(P); C && C->GetFirstPersonCamera())
		From = C->GetFirstPersonCamera()->GetComponentLocation();
	PC->SetControlRotation((FVector(X, Y, Z) - From).Rotation());
	UE_LOG(LogDF, Display, TEXT("DFAimAt %s from %s"), *PC->GetControlRotation().ToCompactString(), *From.ToCompactString());
}

namespace {
	/** The nearest actor whose class (or a parent class) is called ClassName, not Me nor what rides in Me. */
	AActor* FindNearest(UWorld* World, const APawn* Me, const FString& ClassName) {
		AActor* Best = nullptr;
		for (TActorIterator<AActor> It(World); Me && It; ++It) {
			bool bMatch = false;
			for (const UClass* C = It->GetClass(); C && !bMatch; C = C->GetSuperClass())
				bMatch = C->GetName().Equals(ClassName, ESearchCase::IgnoreCase);
			if (!bMatch || *It == Me || It->GetAttachParentActor() == Me)
				continue;
			if (!Best || FVector::DistSquared(It->GetActorLocation(), Me->GetActorLocation()) < FVector::DistSquared(Best->GetActorLocation(), Me->GetActorLocation()))
				Best = *It;
		}
		return Best;
	}

	/** The soldier a player plays: the driver of the car they drive; none while dead. */
	ADFCharacter* SoldierOf(const APlayerState* PS) {
		APawn* P = PS ? PS->GetPawn() : nullptr;
		const ADFVehicle* V = Cast<ADFVehicle>(P);
		return V ? V->GetDriver() : Cast<ADFCharacter>(P);
	}
} // namespace

APlayerState* UDFCheatManager::FindPlayer(const FString& Name) const {
	// every player has a player state, playing, dead or behind a wheel: the names are there
	if (const AGameStateBase* GS = GetWorld()->GetGameState())
		for (APlayerState* PS : GS->PlayerArray)
			if (PS && PS->GetPlayerName().Equals(Name, ESearchCase::IgnoreCase))
				return PS;
	return nullptr;
}

FString UDFCheatManager::SplitName(const FString& Line, FString& Rest) const {
	// the longest player's name the line starts with (players' names may hold spaces: "red baron"), else
	// one word or several in quotes
	const FString In = Line.TrimStart();
	FString Name;
	if (const AGameStateBase* GS = GetWorld()->GetGameState())
		for (const APlayerState* PS : GS->PlayerArray) {
			const FString Player = PS ? PS->GetPlayerName() : FString();
			if (Player.Len() > Name.Len() && In.StartsWith(Player, ESearchCase::IgnoreCase) && (In.Len() == Player.Len() || FChar::IsWhitespace(In[Player.Len()])))
				Name = Player;
		}
	const TCHAR* Str = *In + Name.Len();
	if (Name.IsEmpty())
		FParse::Token(Str, Name, false);
	Rest = FString(Str).TrimStartAndEnd();
	return Name;
}

AActor* UDFCheatManager::FindPlayerOrNearest(const FString& Name, bool* bOutPlayer) const {
	const APlayerState* PS = FindPlayer(Name);
	if (bOutPlayer)
		*bOutPlayer = PS != nullptr;
	return PS ? PS->GetPawn() : FindNearest(GetWorld(), GetPlayerController()->GetPawn(), Name);
}

void UDFCheatManager::DFAimAtNearest(const FString& Line) {
	FString Rest;
	if (const AActor* Best = FindPlayerOrNearest(SplitName(Line, Rest))) {
		const FVector At = Best->GetActorLocation();
		DFAimAt(At.X, At.Y, At.Z);
	}
}

void UDFCheatManager::DFBring(const FString& NameAndDist) {
	FString Rest;
	AActor* Best = FindPlayerOrNearest(SplitName(NameAndDist, Rest));
	const float Dist = Rest.IsEmpty() ? 300.f : FCString::Atof(*Rest);
	const APawn* Me = GetPlayerController()->GetPawn();
	if (Me && Best && Best != Me)
		Best->SetActorLocation(Me->GetActorLocation() + FRotator(0.f, Me->GetActorRotation().Yaw, 0.f).Vector() * Dist, false, nullptr, ETeleportType::ResetPhysics);
}

void UDFCheatManager::DFWhere() {
	const APlayerController* PC = GetPlayerController();
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	const FVector At = PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : Eye;
	const FString Line = FString::Printf(TEXT("DFTeleport %.0f %.0f %.0f | DFCam %.0f %.0f %.0f %.0f %.0f"), At.X, At.Y, At.Z, Eye.X, Eye.Y, Eye.Z, View.Yaw, View.Pitch);
	FPlatformApplicationMisc::ClipboardCopy(*Line);
	Say(Line);
}

void UDFCheatManager::DFMuzzleProbe(const FString& Bone, float MaxAhead) {
	const APlayerController* PC = GetPlayerController();
	const ADFCharacter* C = PC ? Cast<ADFCharacter>(PC->GetPawn()) : nullptr;
	const ADFWeapon* W = C ? C->GetCurrentWeapon() : nullptr;
	USkeletalMeshComponent* Arms = W ? W->Arms.Get() : nullptr;
	const FSkeletalMeshRenderData* Render = Arms && Arms->GetSkeletalMeshAsset() ? Arms->GetSkeletalMeshAsset()->GetResourceForRendering() : nullptr;
	if (!Render || Render->LODRenderData.IsEmpty() || !Arms->GetSkinWeightBuffer(0)) {
		Say(TEXT("DFMuzzleProbe: no first-person model"));
		return;
	}
	const FSkeletalMeshLODRenderData& LOD = Render->LODRenderData[0];
	TArray<FMatrix44f> RefToLocal;
	Arms->CacheRefToLocalMatrices(RefToLocal);
	TArray<FVector3f> Positions;
	USkinnedMeshComponent::ComputeSkinnedPositions(Arms, Positions, RefToLocal, LOD, *Arms->GetSkinWeightBuffer(0));
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	const FVector Ahead = View.Vector();
	const FTransform& ToWorld = Arms->GetComponentTransform();
	const int32 Only = Bone.IsEmpty() ? INDEX_NONE : Arms->GetBoneIndex(FName(*Bone));
	// each vertex's strongest bone, through its section's bone map
	TArray<int32> VertexBone;
	VertexBone.Init(INDEX_NONE, Positions.Num());
	const FSkinWeightVertexBuffer& Weights = *Arms->GetSkinWeightBuffer(0);
	for (const FSkelMeshRenderSection& Section : LOD.RenderSections)
		for (uint32 V = Section.BaseVertexIndex; V < Section.BaseVertexIndex + Section.NumVertices && int32(V) < Positions.Num(); ++V) {
			uint16 Best = 0;
			for (uint32 i = 0; i < Weights.GetMaxBoneInfluences(); ++i)
				if (Weights.GetBoneWeight(V, i) > Best) {
					Best = Weights.GetBoneWeight(V, i);
					const uint32 Local = Weights.GetBoneIndex(V, i);
					VertexBone[V] = Section.BoneMap.IsValidIndex(Local) ? Section.BoneMap[Local] : INDEX_NONE;
				}
		}
	float Front = -UE_BIG_NUMBER;
	int32 Tip = INDEX_NONE;
	// on screen only: a model can carry parts never drawn far off (SC-20K: a flash mesh 1200 uu ahead)
	int32 SizeX = 0, SizeY = 0;
	PC->GetViewportSize(SizeX, SizeY);
	TArray<bool> Seen;
	Seen.Init(false, Positions.Num());
	for (int32 V = 0; V < Positions.Num(); ++V) {
		FVector2D Screen;
		const FVector At = ToWorld.TransformPosition(FVector(Positions[V]));
		Seen[V] = PC->ProjectWorldLocationToScreen(At, Screen, true) && Screen.X >= 0 && Screen.Y >= 0 && Screen.X < SizeX && Screen.Y < SizeY &&
			(MaxAhead <= 0.f || FVector::DotProduct(At - Eye, Ahead) <= MaxAhead);
	}
	for (int32 V = 0; V < Positions.Num(); ++V)
		if (Seen[V] && (Only == INDEX_NONE || VertexBone[V] == Only)) {
			const float D = FVector::DotProduct(ToWorld.TransformPosition(FVector(Positions[V])), Ahead);
			if (D > Front) {
				Front = D;
				Tip = V;
			}
		}
	if (Tip == INDEX_NONE || VertexBone[Tip] == INDEX_NONE) {
		Say(TEXT("DFMuzzleProbe: no vertex of that bone"));
		return;
	}
	FVector Sum = FVector::ZeroVector;
	int32 N = 0;
	for (int32 V = 0; V < Positions.Num(); ++V)
		if (Seen[V] && VertexBone[V] == VertexBone[Tip] && FVector::DotProduct(ToWorld.TransformPosition(FVector(Positions[V])), Ahead) > Front - 0.5f) {
			Sum += FVector(Positions[V]);
			++N;
		}
	FVector2D TipOnScreen;
	PC->ProjectWorldLocationToScreen(ToWorld.TransformPosition(Sum / N), TipOnScreen, true);
	const FName Name = Arms->GetBoneName(VertexBone[Tip]);
	const FVector Offset = Arms->GetSocketTransform(Name, RTS_Component).InverseTransformPosition(Sum / N);
	Say(FString::Printf(TEXT("DFMuzzleProbe %s: MuzzleBone = TEXT(\"%s\"); MuzzleOffset = FVector(%.2ff, %.2ff, %.2ff); (%d vertices at the tip, on screen at %.0f %.0f, %.1f cm ahead)"), *GetNameSafe(W->GetClass()), *Name.ToString(), Offset.X, Offset.Y, Offset.Z, N, TipOnScreen.X, TipOnScreen.Y,
		FVector::DotProduct(ToWorld.TransformPosition(Sum / N) - Eye, Ahead)));
}

void UDFCheatManager::DFOrder(int32 Order) {
	if (ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>())
		GM->GiveOrder(GetPlayerController(), Order, nullptr);
}

void UDFCheatManager::DFBlowUp() {
	const APawn* Me = GetPlayerController()->GetPawn();
	ADFVehicle* Nearest = nullptr;
	for (TActorIterator<ADFVehicle> It(GetWorld()); It && Me; ++It)
		if (!Nearest || FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(Nearest->GetActorLocation(), Me->GetActorLocation()))
			Nearest = *It;
	if (Nearest) {
		UE_LOG(LogDF, Display, TEXT("DFBlowUp: %s at %s"), *Nearest->GetName(), *Nearest->GetActorLocation().ToCompactString());
		Nearest->TakeDamage(100000.f, FDamageEvent(UDFDamage_Explosion::StaticClass()), GetPlayerController(), nullptr);
	}
}

void UDFCheatManager::DFNavRelevant() {
	TMap<FString, int32> Counts;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It) {
		TInlineComponentArray<UPrimitiveComponent*> Prims(*It);
		for (const UPrimitiveComponent* P : Prims) {
			const bool bSpawned = !It->HasAnyFlags(RF_WasLoaded);
			if ((P->Mobility == EComponentMobility::Movable || bSpawned) && P->CanEverAffectNavigation() && P->IsNavigationRelevant())
				Counts.FindOrAdd(FString::Printf(TEXT("%s.%s (%s)"), *It->GetClass()->GetName(), *P->GetName(), *P->GetClass()->GetName()))++;
		}
	}
	for (const auto& C : Counts)
		UE_LOG(LogDF, Display, TEXT("Nav relevant movable: %s x%d"), *C.Key, C.Value);
	if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld())) {
		UE_LOG(LogDF, Display, TEXT("Navigation dirty: %d (%s, dirty areas queued %d, agents %d, default nav data %s, main %s)"), Nav->IsNavigationDirty() ? 1 : 0,
			*Nav->GetClass()->GetName(), Nav->HasDirtyAreasQueued() ? 1 : 0, Nav->GetSupportedAgents().Num(), *GetNameSafe(Nav->GetDefaultNavDataInstance()),
			*GetNameSafe(Nav->MainNavData));
		for (const ANavigationData* D : Nav->NavDataSet) {
			const ARecastNavMesh* R = Cast<ARecastNavMesh>(D);
			UE_LOG(LogDF, Display, TEXT("  nav data %s needs rebuild %d tiles %d generator %d runtime gen %d"), *GetNameSafe(D), D && D->NeedsRebuild() ? 1 : 0,
				R ? R->GetNavMeshTilesCount() : -1, D && D->GetGenerator() ? 1 : 0, D ? int32(D->GetRuntimeGenerationMode()) : -1);
		}
	}
}

void UDFCheatManager::DFDecal(int32 Kind, float Size) {
	APlayerController* PC = GetPlayerController();
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFDecalCheat), false, PC->GetPawn());
	if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + View.Vector() * 10000.f, ECC_GameTraceChannel2, Params)) // the Weapon channel: what bullets hit
	{
		const DF::EDecal K = Kind == 0 ? DF::EDecal::BulletHole : Kind == 1 ? DF::EDecal::Scorch
																			: DF::EDecal::Blood;
		DF::SpawnDecal(PC, K, Hit.ImpactPoint, Hit.ImpactNormal, Size > 0.f ? Size : (Kind == 0 ? 12.f : Kind == 1 ? 300.f
																												   : 108.f));
		UE_LOG(LogDF, Display, TEXT("DFDecal %d at %s normal %s on %s"), Kind, *Hit.ImpactPoint.ToCompactString(), *Hit.ImpactNormal.ToCompactString(), *GetNameSafe(Hit.GetActor()));
	}
}

// --- running cheats on the server ------------------------------------------------------------

bool UDFCheatManager::ProcessConsoleExec(const TCHAR* Cmd, FOutputDevice& Ar, UObject* Executor) {
	ADFPlayerController* PC = Cast<ADFPlayerController>(GetOuterAPlayerController());
	if (PC && !PC->HasAuthority()) {
		// views and test helpers stay on this machine (and only where cheats are on: a free
		// camera sees through walls); everything else changes the game, so the server runs it
		static const TCHAR* Local[] = { TEXT("DFCam"), TEXT("DFTopView"), TEXT("DFNavTest"), TEXT("DFNavPath"), TEXT("DFNavRelevant"),
			TEXT("DFTraceMat"), TEXT("DFWhere"), TEXT("DFDecal"), TEXT("DFWalk"), TEXT("DFAimAt"), TEXT("DFAimAtNearest"), TEXT("DFFire"), TEXT("DFTouch"), TEXT("DFVehicles"),
			TEXT("ViewSelf"), TEXT("FreezeFrame"), TEXT("DebugCapsuleSweep") };
		const TCHAR* Str = Cmd;
		FString Name;
		if (FParse::Token(Str, Name, true)) {
			const UFunction* F = FindFunction(FName(*Name));
			if (F && F->HasAnyFunctionFlags(FUNC_Exec)) {
				if (!PC->CanCheat()) {
					Ar.Log(TEXT("Cheats are off here: AdminLogin <password> first."));
					return true;
				}
				const bool bLocal = Algo::AnyOf(Local, [&Name](const TCHAR* L) { return Name.Equals(L, ESearchCase::IgnoreCase); });
				if (!bLocal) {
					PC->ServerAdmin(Cmd);
					return true;
				}
			}
		}
	}
	// UObject dispatch remains available in Shipping, where UE disables its cheat wrapper.
	return UObject::ProcessConsoleExec(Cmd, Ar, Executor);
}

// --- admin -----------------------------------------------------------------------------------

namespace {
	/** "word rest of it" -> word, rest. */
	FString SplitFirst(const FString& In, FString& Rest) {
		FString First;
		if (!In.TrimStart().Split(TEXT(" "), &First, &Rest)) {
			First = In.TrimStartAndEnd();
			Rest.Reset();
		}
		Rest.TrimStartAndEndInline();
		return First;
	}

	/** UE1 SetPropertyText / GetPropertyText through reflection. */
	bool SetPropertyText(UObject* Obj, const FString& Name, const FString& Value) {
		FProperty* P = Obj ? FindFProperty<FProperty>(Obj->GetClass(), FName(*Name)) : nullptr;
		return P && P->ImportText_InContainer(*Value, Obj, Obj, PPF_None) != nullptr;
	}

	bool GetPropertyText(UObject* Obj, const FString& Name, FString& Out) {
		FProperty* P = Obj ? FindFProperty<FProperty>(Obj->GetClass(), FName(*Name)) : nullptr;
		if (!P)
			return false;
		P->ExportText_InContainer(0, Out, Obj, Obj, Obj, PPF_None);
		return true;
	}

	/** A player's soldier, then their player state, then controller: the first that has the property. */
	TArray<UObject*> PlayerTargets(APlayerState* PS) {
		return { SoldierOf(PS), PS, PS ? PS->GetOwningController() : nullptr };
	}

	/** What you look at, then (a soldier, a car) its player state and controller. */
	TArray<UObject*> LookedTargets(AActor* A) {
		const APawn* P = Cast<APawn>(A);
		return { A, P ? P->GetPlayerState() : nullptr, P ? P->GetController() : nullptr };
	}

	/** A player's name, anything else's actor name. */
	FString LookedName(const AActor* A) {
		const APawn* P = Cast<APawn>(A);
		return P && P->GetPlayerState() ? P->GetPlayerState()->GetPlayerName() : GetNameSafe(A);
	}
} // namespace

AActor* UDFCheatManager::LookedAt() const {
	APlayerController* PC = GetPlayerController();
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFLookedAt), false, PC->GetPawn());
	GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + View.Vector() * 22500.f, ECC_GameTraceChannel2, Params);
	return Hit.GetActor();
}

void UDFCheatManager::Say(const FString& Line) const {
	UE_LOG(LogDF, Display, TEXT("%s"), *Line);
	GetPlayerController()->ClientMessage(Line);
}

bool UDFCheatManager::IsAdmin() const {
	const ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>();
	return GetWorld()->GetNetMode() == NM_Standalone || (GM && GM->IsAdmin(GetPlayerController()));
}

void UDFCheatManager::RunOn(APawn* P, AController* C, const FString& Name, const FString& Command) {
	// admins only, not everybody on a ?Cheats=1 server: the command runs with the other
	// player's rights, and through an admin that would be Kick for anyone
	if (!IsAdmin()) {
		Say(TEXT("RunAs: admins only"));
		return;
	}
	if ((!P && !C) || Command.IsEmpty()) {
		Say(FString::Printf(TEXT("RunAs: no player %s"), *Name));
		return;
	}
	// MF: P.ConsoleCommand on their pawn (exec functions of the pawn, then the controller)
	FStringOutputDevice Out;
	const APlayerController* PC = Cast<APlayerController>(C);
	const bool bDone = (P && P->ProcessConsoleExec(*Command, Out, P)) || (C && C->ProcessConsoleExec(*Command, Out, P)) || (PC && PC->CheatManager && PC->CheatManager->ProcessConsoleExec(*Command, Out, P));
	Say(bDone ? FString::Printf(TEXT("%s: %s"), *Name, *Command) : FString::Printf(TEXT("RunAs: %s can't %s"), *Name, *Command));
}

void UDFCheatManager::SetOn(const TArray<UObject*>& Targets, const FString& Name, const FString& PropertyAndValue) {
	FString Value;
	const FString Prop = SplitFirst(PropertyAndValue, Value);
	if (!Algo::AnyOf(Targets))
		Say(FString::Printf(TEXT("No player %s"), *Name));
	else if (Algo::AnyOf(Targets, [&](UObject* O) { return SetPropertyText(O, Prop, Value); }))
		Say(FString::Printf(TEXT("%s %s = %s"), *Name, *Prop, *Value));
	else
		Say(FString::Printf(TEXT("No %s on %s"), *Prop, *Name));
}

void UDFCheatManager::GetOn(const TArray<UObject*>& Targets, const FString& Name, const FString& Property) {
	FString Value;
	if (!Algo::AnyOf(Targets))
		Say(FString::Printf(TEXT("No player %s"), *Name));
	else if (Algo::AnyOf(Targets, [&](UObject* O) { return GetPropertyText(O, Property, Value); }))
		Say(FString::Printf(TEXT("%s %s = %s"), *Name, *Property, *Value));
	else
		Say(FString::Printf(TEXT("No %s on %s"), *Property, *Name));
}

void UDFCheatManager::RunAs(const FString& PlayerAndCommand) {
	FString Command;
	const FString Name = SplitName(PlayerAndCommand, Command);
	APlayerState* PS = FindPlayer(Name);
	RunOn(SoldierOf(PS), PS ? PS->GetOwningController() : nullptr, Name, Command);
}

void UDFCheatManager::RunAsLooking(const FString& Command) {
	AActor* A = LookedAt();
	APawn* P = Cast<APawn>(A);
	RunOn(P, P ? P->GetController() : nullptr, LookedName(A), Command);
}

void UDFCheatManager::SetTo(const FString& PlayerPropertyValue) {
	FString Rest;
	const FString Name = SplitName(PlayerPropertyValue, Rest);
	SetOn(PlayerTargets(FindPlayer(Name)), Name, Rest);
}

void UDFCheatManager::GetTo(const FString& PlayerAndProperty) {
	FString Rest;
	const FString Name = SplitName(PlayerAndProperty, Rest);
	GetOn(PlayerTargets(FindPlayer(Name)), Name, Rest);
}

void UDFCheatManager::SetToSelf(const FString& PropertyAndValue) {
	APlayerState* PS = GetPlayerController()->PlayerState;
	SetOn(PlayerTargets(PS), PS ? PS->GetPlayerName() : FString(), PropertyAndValue);
}

void UDFCheatManager::GetToSelf(const FString& Property) {
	APlayerState* PS = GetPlayerController()->PlayerState;
	GetOn(PlayerTargets(PS), PS ? PS->GetPlayerName() : FString(), Property);
}

void UDFCheatManager::SetToLooking(const FString& PropertyAndValue) {
	AActor* A = LookedAt();
	SetOn(LookedTargets(A), LookedName(A), PropertyAndValue);
}

void UDFCheatManager::GetToLooking(const FString& Property) {
	AActor* A = LookedAt();
	GetOn(LookedTargets(A), LookedName(A), Property);
}

void UDFCheatManager::TeleportTo(const FString& NameAndDist) {
	FString Rest;
	const FString Name = SplitName(NameAndDist, Rest);
	const float Dist = FCString::Atof(*Rest);
	APawn* Me = GetPlayerController()->GetPawn();
	bool bPlayer = false;
	const AActor* Them = FindPlayerOrNearest(Name, &bPlayer);
	if (!Me || !Them || Them == Me) {
		Say(FString::Printf(TEXT("TeleportTo: no %s"), *Name));
		return;
	}
	if (bPlayer && Dist <= 0.f) {
		// ZombiePlayer.Teleport: a few random spots around them (VRand x (100, 100, 10) uu)
		for (int32 Try = 0; Try < 9; ++Try)
			if (Me->TeleportTo(Them->GetActorLocation() + FMath::VRand() * FVector(225.f, 225.f, 22.5f), Me->GetActorRotation()))
				return;
		return;
	}
	// in front of it, a little up (vehicles settle)
	const FVector At = Them->GetActorLocation() + Them->GetActorForwardVector() * (Dist > 0.f ? Dist : 600.f) + FVector(0.f, 0.f, 50.f);
	DFTeleport(At.X, At.Y, At.Z);
}

void UDFCheatManager::BecomeHuman() {
	AZombieGame* GM = GetWorld()->GetAuthGameMode<AZombieGame>();
	if (ADFCharacter* C = Cast<ADFCharacter>(GetPlayerController()->GetPawn()); GM && C)
		GM->BecomeHuman(C, false);
}

void UDFCheatManager::BecomeSurvivor() {
	AZombieGame* GM = GetWorld()->GetAuthGameMode<AZombieGame>();
	if (ADFCharacter* C = Cast<ADFCharacter>(GetPlayerController()->GetPawn()); GM && C)
		GM->BecomeHuman(C, true);
}

void UDFCheatManager::BecomeZombie() {
	AZombieGame* GM = GetWorld()->GetAuthGameMode<AZombieGame>();
	if (ADFCharacter* C = Cast<ADFCharacter>(GetPlayerController()->GetPawn()); GM && C)
		GM->BecomeZombie(C, false);
}

void UDFCheatManager::BecomeNemesis() {
	AZombieGame* GM = GetWorld()->GetAuthGameMode<AZombieGame>();
	if (ADFCharacter* C = Cast<ADFCharacter>(GetPlayerController()->GetPawn()); GM && C)
		GM->BecomeZombie(C, true);
}

void UDFCheatManager::Kick(const FString& Line) {
	FString Rest;
	const FString PlayerName = SplitName(Line, Rest);
	const APlayerState* PS = FindPlayer(PlayerName);
	APlayerController* Target = PS ? Cast<APlayerController>(PS->GetOwningController()) : nullptr;
	AGameModeBase* GM = GetWorld()->GetAuthGameMode();
	if (!IsAdmin() || !Target || Target->IsLocalController() || !GM || !GM->GameSession) {
		Say(FString::Printf(TEXT("Kick: can't kick %s"), *PlayerName));
		return;
	}
	GM->GameSession->KickPlayer(Target, FText::FromString(TEXT("Kicked by an admin")));
}

void UDFCheatManager::DFFindPits() {
	UWorld* World = GetWorld();
	const FString Map = UWorld::RemovePIEPrefix(World->GetMapName());
	const TArray<DF::FPit> Pits = DF::FindPits(World);
	FString Json = TEXT("[\n");
	for (int32 i = 0; i < Pits.Num(); ++i) {
		const DF::FPit& P = Pits[i];
		FString Boxes;
		for (const FBox& B : P.Boxes)
			Boxes += FString::Printf(TEXT("%s[%.0f, %.0f, %.0f, %.0f, %.0f, %.0f]"), Boxes.IsEmpty() ? TEXT("") : TEXT(", "), B.Min.X, B.Min.Y, B.Min.Z, B.Max.X, B.Max.Y, B.Max.Z);
		const FString Line = FString::Printf(TEXT("{\"min\": [%.0f, %.0f, %.0f], \"max\": [%.0f, %.0f, %.0f], \"min_fall\": %.0f, \"max_fall\": %.0f, \"area\": %.1f, \"hurts\": %s, \"water\": %s, \"clashes\": %d, \"boxes\": [%s]}"),
			P.Floor.Min.X, P.Floor.Min.Y, P.Floor.Min.Z, P.Floor.Max.X, P.Floor.Max.Y, P.Floor.Max.Z, P.MinFall, P.MaxFall, P.Area, P.bHurts ? TEXT("true") : TEXT("false"), P.bWater ? TEXT("true") : TEXT("false"), P.Clashes, *Boxes);
		UE_LOG(LogDF, Display, TEXT("DFFindPits %s: %s"), *Map, *Line.Left(300));
		Json += TEXT(" ") + Line + (i + 1 < Pits.Num() ? TEXT(",\n") : TEXT("\n"));
	}
	Json += TEXT("]\n");
	const FString File = FPaths::ProjectSavedDir() / TEXT("Pits") / (Map + TEXT(".json"));
	FFileHelper::SaveStringToFile(Json, *File);
	Say(FString::Printf(TEXT("%d pits -> %s"), Pits.Num(), *File));
}
