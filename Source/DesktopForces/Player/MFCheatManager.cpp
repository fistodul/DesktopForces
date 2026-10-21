#include "Player/MFCheatManager.h"
#include "World/MFDecals.h"
#include "Materials/MaterialInterface.h"
#include "Engine/DamageEvents.h"
#include "TimerManager.h"
#include "DesktopForces.h"
#include "Core/MFTypes.h"
#include "Game/MFGameMode.h"
#include "Algo/AnyOf.h"
#include "Misc/StringOutputDevice.h"
#include "GameFramework/GameSession.h"
#include "Game/Zombie/MFGameMode_Zombie.h"
#include "Game/MFGameState.h"
#include "Game/MFPlayerState.h"
#include "Player/MFCharacter.h"
#include "Player/MFCharacterMovement.h"
#include "Player/MFPlayerController.h"
#include "Vehicles/MFVehicle.h"
#include "Weapons/MFWeapon.h"
#include "UObject/UObjectIterator.h"
#include "World/MFLadder.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
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

void UMFCheatManager::MFNavPath(float X, float Y, float Z)
{
	APawn* P = GetPlayerController()->GetPawn();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!P || !Nav)
	{
		return;
	}
	for (const float Ext : { 100.f, 1000.f, 10000.f })
	{
		FNavLocation Proj;
		const bool bOn = Nav->ProjectPointToNavigation(P->GetActorLocation(), Proj, FVector(Ext, Ext, Ext));
		UE_LOG(LogMF, Display, TEXT("MFNavPath: project within %.0f: %d %s"), Ext, bOn ? 1 : 0, *Proj.Location.ToCompactString());
	}
	UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(), P->GetActorLocation(), FVector(X, Y, Z), P);
	if (!Path || !Path->GetPath().IsValid())
	{
		UE_LOG(LogMF, Display, TEXT("MFNavPath: no path"));
		return;
	}
	UE_LOG(LogMF, Display, TEXT("MFNavPath: %d points, length %.0f, partial %d"), Path->PathPoints.Num(), Path->GetPathLength(), Path->IsPartial());
	const TArray<FNavPathPoint>& Points = Path->GetPath()->GetPathPoints();
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		const bool bLink = Points[i].CustomNavLinkId != FNavLinkId::Invalid;
		UE_LOG(LogMF, Display, TEXT("MFNavPath: %d %s%s"), i, *Points[i].Location.ToString(), bLink ? TEXT(" (link)") : TEXT(""));
	}
}

void UMFCheatManager::MFHurt(int32 N)
{
	if (APawn* P = GetPlayerController()->GetPawn())
	{
		P->TakeDamage(N, FDamageEvent(), nullptr, nullptr);
	}
}

void UMFCheatManager::MFHurtEnemies(int32 N)
{
	const AMFCharacter* Me = Cast<AMFCharacter>(GetPlayerController()->GetPawn());
	TArray<AMFCharacter*> Targets;
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Me && It->IsAlive() && (!Me || It->GetTeam() == MF::NoTeam || It->GetTeam() != Me->GetTeam()))
		{
			Targets.Add(*It);
		}
	}
	for (AMFCharacter* C : Targets)
	{
		FDamageEvent Ev(UMFDamage_Bullet::StaticClass());
		C->TakeDamage(float(N), Ev, GetPlayerController(), GetPlayerController()->GetPawn());
	}
}

void UMFCheatManager::MFFire(int32 Alt)
{
	if (AMFCharacter* C = Cast<AMFCharacter>(GetPlayerController()->GetPawn()))
	{
		const bool bAlt = Alt != 0;
		C->StartFire(bAlt);
		TWeakObjectPtr<AMFCharacter> Weak = C;
		FTimerHandle H;
		GetWorld()->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([Weak, bAlt]()
		{
			if (Weak.IsValid())
			{
				Weak->StopFire(bAlt);
			}
		}), 0.1f, false);
	}
}

void UMFCheatManager::MFWalk(float Yaw, float Seconds, float Forward, float Right, float Up)
{
	if (AMFPlayerController* PC = Cast<AMFPlayerController>(GetPlayerController()))
	{
		PC->SetControlRotation(FRotator(0.f, Yaw, 0.f));
		PC->ForceMove(FVector2D(Forward, Right), Seconds, Up);
	}
}

void UMFCheatManager::MFTeleport(float X, float Y, float Z)
{
	if (AMFVehicle* V = Cast<AMFVehicle>(GetPlayerController()->GetPawn()))
	{
		// a vehicle takes its trailer along (the hitch would pull them back together)
		const FVector Offset = FVector(X, Y, Z) - V->GetActorLocation();
		V->TeleportTo(FVector(X, Y, Z), V->GetActorRotation(), false, true);
		if (AMFVehicle* T = V->GetTrailer())
		{
			T->TeleportTo(T->GetActorLocation() + Offset, T->GetActorRotation(), false, true);
		}
		return;
	}
	if (APawn* P = GetPlayerController()->GetPawn())
	{
		if (!P->TeleportTo(FVector(X, Y, Z), P->GetActorRotation()))
		{
			UE_LOG(LogMF, Warning, TEXT("MFTeleport: (%.0f %.0f %.0f) blocked"), X, Y, Z);
		}
	}
}

void UMFCheatManager::MFTraceMat()
{
	FVector Loc;
	FRotator Rot;
	GetPlayerController()->GetPlayerViewPoint(Loc, Rot);
	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("MFTraceMat"), true, GetPlayerController()->GetPawn());
	Params.bReturnFaceIndex = true;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Loc, Loc + Rot.Vector() * 100000.f, ECC_Visibility, Params))
	{
		UE_LOG(LogMF, Display, TEXT("MFTraceMat: nothing"));
		return;
	}
	int32 Section = -1;
	UMaterialInterface* Mat = Hit.GetComponent() ? Hit.GetComponent()->GetMaterialFromCollisionFaceIndex(Hit.FaceIndex, Section) : nullptr;
	UE_LOG(LogMF, Display, TEXT("MFTraceMat: %s at %s dist %.0f face %d section %d material %s"),
		*GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToString(), Hit.Distance, Hit.FaceIndex, Section, *GetPathNameSafe(Mat));
	// the world's render meshes have no collision (separate collision meshes): list the
	// visible meshes whose bounds hold the hit point, with their materials
	auto LogErrors = [this](const UMaterialInterface* M)
	{
#if WITH_EDITOR   // compile errors are kept in editor builds only
		const UMaterial* Base = M ? M->GetMaterial() : nullptr;
		const FMaterialResource* Res = Base && GetWorld()->Scene ? Base->GetMaterialResource(GetWorld()->Scene->GetShaderPlatform()) : nullptr;
		if (Res)
		{
			for (const FString& E : Res->GetCompileErrors())
			{
				UE_LOG(LogMF, Display, TEXT("MFTraceMat:     compile error: %s"), *E);
			}
		}
#endif
	};
	LogErrors(Mat);
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld() && It->IsVisible() && It->Bounds.GetBox().ExpandBy(2.f).IsInside(Hit.ImpactPoint))
		{
			UE_LOG(LogMF, Display, TEXT("MFTraceMat:   %s (%s)"), *It->GetName(), *GetNameSafe(It->GetStaticMesh()));
			const FMeshMapBuildData* BuildData = It->LODData.Num() ? It->GetMeshMapBuildData(It->LODData[0]) : nullptr;
			const FLightMap* LM = BuildData ? BuildData->LightMap.GetReference() : nullptr;
			if (!LM)
			{
				UE_LOG(LogMF, Display, TEXT("MFTraceMat:     no lightmap (build data %s)"), BuildData ? TEXT("yes") : TEXT("no"));
			}
			else
			{
				const FLightMapInteraction I = LM->GetInteraction(GetWorld()->GetFeatureLevel());
				const FVector4f* Scale = I.GetType() == LMIT_Texture ? I.GetScaleArray() : nullptr;
				const FVector4f* Add = I.GetType() == LMIT_Texture ? I.GetAddArray() : nullptr;
				UE_LOG(LogMF, Display, TEXT("MFTraceMat:     lightmap type %d scale0 %s add0 %s scale1 %s add1 %s tex %s"), (int32)I.GetType(),
					Scale ? *Scale[0].ToString() : TEXT("-"), Add ? *Add[0].ToString() : TEXT("-"),
					Scale ? *Scale[1].ToString() : TEXT("-"), Add ? *Add[1].ToString() : TEXT("-"),
					I.GetType() == LMIT_Texture ? *GetNameSafe(I.GetTexture(true)) : TEXT("-"));
			}
			for (int32 i = 0; i < It->GetNumMaterials(); ++i)
			{
				UE_LOG(LogMF, Display, TEXT("MFTraceMat:     [%d] %s"), i, *GetPathNameSafe(It->GetMaterial(i)));
				LogErrors(It->GetMaterial(i));
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

void UMFCheatManager::MFEnterSeat(int32 Seat)
{
	AMFCharacter* C = Cast<AMFCharacter>(GetPlayerController()->GetPawn());
	AMFVehicle* Best = nullptr;
	for (TActorIterator<AMFVehicle> It(GetWorld()); It && C; ++It)
	{
		if (It->IsNear(C) && (!Best || FVector::Dist(It->GetActorLocation(), C->GetActorLocation()) < FVector::Dist(Best->GetActorLocation(), C->GetActorLocation())))
		{
			Best = *It;
		}
	}
	if (Best)
	{
		Best->EnterSeat(C, Seat);
	}
}

void UMFCheatManager::MFVehicles()
{
	for (TActorIterator<AMFVehicle> It(GetWorld()); It; ++It)
	{
		It->DebugDump();
	}
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

// --- Mobile Forces cheats ---------------------------------------------------------------
// MF allowed these only with cheats on and in single player (bGreatDane, NM_Standalone);
// UE's cheat manager has the same rule (standalone, or `EnableCheats` on a listen server).

namespace
{
	TSubclassOf<AMFWeapon> FindWeaponClass(const FString& Name)
	{
		// "Shotgun", "MFWeapon_Shotgun" or a full path
		if (UClass* C = LoadClass<AMFWeapon>(nullptr, *Name, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return C;
		}
		UClass* Best = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(AMFWeapon::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}
			const FString N = It->GetName();
			if (N.Equals(Name, ESearchCase::IgnoreCase) || N.Equals(TEXT("MFWeapon_") + Name, ESearchCase::IgnoreCase))
			{
				return *It;
			}
			if (!Best && N.Contains(Name))
			{
				Best = *It;
			}
		}
		return Best;
	}
}

void UMFCheatManager::SetTeam(int32 N)
{
	APlayerController* PC = GetPlayerController();
	AMFPlayerState* PS = PC ? PC->GetPlayerState<AMFPlayerState>() : nullptr;
	AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>();
	const AMFGameState* GS = GetWorld()->GetGameState<AMFGameState>();
	if (!PS || !GM || !GS || !GS->IsTeamGame() || N < 0 || N >= GS->NumTeams)
	{
		return;
	}
	// RagePlayer.SetTeam -> ChangeTeam: new team, fresh spawn on its side
	PS->SetTeam(uint8(N));
	if (APawn* P = PC->GetPawn())
	{
		P->Destroy();
	}
	GM->RestartPlayer(PC);
}

void UMFCheatManager::KillEnemy()
{
	const AMFCharacter* Me = Cast<AMFCharacter>(GetPlayerController()->GetPawn());
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Me && It->IsAlive() && (!Me || It->GetTeam() == MF::NoTeam || It->GetTeam() != Me->GetTeam()))
		{
			It->Die(GetPlayerController(), UMFDamage_Suicide::StaticClass(), nullptr);
		}
	}
}

void UMFCheatManager::KillPawns()
{
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && !It->IsPlayerControlled())
		{
			It->Die(nullptr, UMFDamage_Suicide::StaticClass(), nullptr);
		}
	}
}

void UMFCheatManager::KillAll(const FString& ClassName)
{
	UClass* Class = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
	if (!Class)
	{
		Class = FindFirstObject<UClass>(*(TEXT("MF") + ClassName), EFindFirstObjectOptions::NativeFirst);
	}
	if (!Class || !Class->IsChildOf(AActor::StaticClass()))
	{
		return;
	}
	for (TActorIterator<AActor> It(GetWorld(), Class); It; ++It)
	{
		if (AMFCharacter* C = Cast<AMFCharacter>(*It))
		{
			if (C->IsAlive() && C != GetPlayerController()->GetPawn())
			{
				C->Die(nullptr, UMFDamage_Suicide::StaticClass(), nullptr);
			}
		}
		else if (*It != GetPlayerController())
		{
			It->Destroy();
		}
	}
}

void UMFCheatManager::TeamGod()
{
	const AMFCharacter* Me = Cast<AMFCharacter>(GetPlayerController()->GetPawn());
	if (!Me)
	{
		return;
	}
	const bool bGod = Me->CanBeDamaged();   // toggle from the player's own state
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetTeam() == Me->GetTeam())
		{
			It->SetCanBeDamaged(!bGod);
		}
	}
	GetPlayerController()->ClientMessage(bGod ? TEXT("Team god mode on") : TEXT("Team god mode off"));
}

void UMFCheatManager::AllAmmo()
{
	if (AMFCharacter* C = Cast<AMFCharacter>(GetPlayerController()->GetPawn()))
	{
		C->RefillAmmo();
	}
}

void UMFCheatManager::SetTime(int32 Seconds)
{
	if (AMFGameState* GS = GetWorld()->GetGameState<AMFGameState>(); GS && GS->TimeLimit > 0)
	{
		GS->ElapsedTime = FMath::Max(0, GS->TimeLimit * 60 - Seconds);   // RagePlayer.settime
	}
}

void UMFCheatManager::SetWeapons(const FString& WeaponName)
{
	const TSubclassOf<AMFWeapon> Class = FindWeaponClass(WeaponName);
	if (!Class)
	{
		return;
	}
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsPlayerControlled() && It->IsAlive())
		{
			if (AMFWeapon* W = It->GiveWeapon(Class, false))
			{
				It->EquipWeapon(W);
			}
		}
	}
}

void UMFCheatManager::GiveWeapon(const FString& WeaponName)
{
	AMFCharacter* C = Cast<AMFCharacter>(GetPlayerController()->GetPawn());
	const TSubclassOf<AMFWeapon> Class = FindWeaponClass(WeaponName);
	if (C && Class)
	{
		if (AMFWeapon* W = C->GiveWeapon(Class, false))
		{
			C->EquipWeapon(W);
		}
	}
}

void UMFCheatManager::SetSpeed(float Multiplier)
{
	if (AMFCharacter* C = Cast<AMFCharacter>(GetPlayerController()->GetPawn()))
	{
		UCharacterMovementComponent* M = C->GetCharacterMovement();
		const UCharacterMovementComponent* Def = GetDefault<UMFCharacterMovement>();
		M->MaxWalkSpeed = Def->MaxWalkSpeed * Multiplier;
		M->MaxWalkSpeedCrouched = Def->MaxWalkSpeedCrouched * Multiplier;
	}
}

void UMFCheatManager::SetJumpZ(float Velocity)
{
	if (AMFCharacter* C = Cast<AMFCharacter>(GetPlayerController()->GetPawn()))
	{
		C->GetCharacterMovement()->JumpZVelocity = Velocity;
	}
}

void UMFCheatManager::ViewMe()
{
	ViewSelf();
}

void UMFCheatManager::MFAimAt(float X, float Y, float Z)
{
	APlayerController* PC = GetPlayerController();
	APawn* P = PC ? PC->GetPawn() : nullptr;
	if (!P)
	{
		return;
	}
	FVector From = P->GetActorLocation();
	if (const AMFCharacter* C = Cast<AMFCharacter>(P); C && C->GetFirstPersonCamera())
	{
		From = C->GetFirstPersonCamera()->GetComponentLocation();
	}
	PC->SetControlRotation((FVector(X, Y, Z) - From).Rotation());
	UE_LOG(LogMF, Display, TEXT("MFAimAt %s from %s"), *PC->GetControlRotation().ToCompactString(), *From.ToCompactString());
}

void UMFCheatManager::MFOrder(int32 Order)
{
	if (AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>())
	{
		GM->GiveOrder(GetPlayerController(), Order, nullptr);
	}
}

void UMFCheatManager::MFBlowUp()
{
	const APawn* Me = GetPlayerController()->GetPawn();
	AMFVehicle* Nearest = nullptr;
	for (TActorIterator<AMFVehicle> It(GetWorld()); It && Me; ++It)
	{
		if (!Nearest || FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(Nearest->GetActorLocation(), Me->GetActorLocation()))
		{
			Nearest = *It;
		}
	}
	if (Nearest)
	{
		UE_LOG(LogMF, Display, TEXT("MFBlowUp: %s at %s"), *Nearest->GetName(), *Nearest->GetActorLocation().ToCompactString());
		Nearest->TakeDamage(100000.f, FDamageEvent(UMFDamage_Explosion::StaticClass()), GetPlayerController(), nullptr);
	}
}

void UMFCheatManager::MFNavRelevant()
{
	TMap<FString, int32> Counts;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		TInlineComponentArray<UPrimitiveComponent*> Prims(*It);
		for (const UPrimitiveComponent* P : Prims)
		{
			const bool bSpawned = !It->HasAnyFlags(RF_WasLoaded);
			if ((P->Mobility == EComponentMobility::Movable || bSpawned) && P->CanEverAffectNavigation() && P->IsNavigationRelevant())
			{
				Counts.FindOrAdd(FString::Printf(TEXT("%s.%s (%s)"), *It->GetClass()->GetName(), *P->GetName(), *P->GetClass()->GetName()))++;
			}
		}
	}
	for (const auto& C : Counts)
	{
		UE_LOG(LogMF, Display, TEXT("Nav relevant movable: %s x%d"), *C.Key, C.Value);
	}
	if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		UE_LOG(LogMF, Display, TEXT("Navigation dirty: %d (%s, dirty areas queued %d, agents %d, default nav data %s, main %s)"), Nav->IsNavigationDirty() ? 1 : 0,
			*Nav->GetClass()->GetName(), Nav->HasDirtyAreasQueued() ? 1 : 0, Nav->GetSupportedAgents().Num(), *GetNameSafe(Nav->GetDefaultNavDataInstance()),
			*GetNameSafe(Nav->MainNavData));
		for (const ANavigationData* D : Nav->NavDataSet)
		{
			const ARecastNavMesh* R = Cast<ARecastNavMesh>(D);
			UE_LOG(LogMF, Display, TEXT("  nav data %s needs rebuild %d tiles %d generator %d runtime gen %d"), *GetNameSafe(D), D && D->NeedsRebuild() ? 1 : 0,
				R ? R->GetNavMeshTilesCount() : -1, D && D->GetGenerator() ? 1 : 0, D ? int32(D->GetRuntimeGenerationMode()) : -1);
		}
	}
}

void UMFCheatManager::MFDecal(int32 Kind, float Size)
{
	APlayerController* PC = GetPlayerController();
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFDecalCheat), false, PC->GetPawn());
	if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + View.Vector() * 10000.f, ECC_GameTraceChannel2, Params))   // the Weapon channel: what bullets hit
	{
		const MF::EDecal K = Kind == 0 ? MF::EDecal::BulletHole : MF::EDecal::Scorch;
		MF::SpawnDecal(PC, K, Hit.ImpactPoint, Hit.ImpactNormal, Size > 0.f ? Size : (Kind == 0 ? 12.f : 300.f));
		UE_LOG(LogMF, Display, TEXT("MFDecal %d at %s normal %s on %s"), Kind, *Hit.ImpactPoint.ToCompactString(), *Hit.ImpactNormal.ToCompactString(), *GetNameSafe(Hit.GetActor()));
	}
}

// --- running cheats on the server ------------------------------------------------------------

bool UMFCheatManager::ProcessConsoleExec(const TCHAR* Cmd, FOutputDevice& Ar, UObject* Executor)
{
	AMFPlayerController* PC = Cast<AMFPlayerController>(GetOuterAPlayerController());
	if (PC && !PC->HasAuthority())
	{
		// views and test helpers stay on this machine (and only where cheats are on: a free
		// camera sees through walls); everything else changes the game, so the server runs it
		static const TCHAR* Local[] = { TEXT("MFCam"), TEXT("MFTopView"), TEXT("MFNavTest"), TEXT("MFNavPath"), TEXT("MFNavRelevant"),
			TEXT("MFTraceMat"), TEXT("MFDecal"), TEXT("MFWalk"), TEXT("MFAimAt"), TEXT("MFFire"), TEXT("MFVehicles"), TEXT("ViewMe"),
			TEXT("ViewSelf"), TEXT("FreezeFrame"), TEXT("DebugCapsuleSweep") };
		const TCHAR* Str = Cmd;
		FString Name;
		if (FParse::Token(Str, Name, true))
		{
			const UFunction* F = FindFunction(FName(*Name));
			if (F && F->HasAnyFunctionFlags(FUNC_Exec))
			{
				if (!PC->CanCheat())
				{
					Ar.Log(TEXT("Cheats are off here: AdminLogin <password> first."));
					return true;
				}
				const bool bLocal = Algo::AnyOf(Local, [&Name](const TCHAR* L) { return Name.Equals(L, ESearchCase::IgnoreCase); });
				if (!bLocal)
				{
					PC->ServerAdmin(Cmd);
					return true;
				}
			}
		}
	}
	return Super::ProcessConsoleExec(Cmd, Ar, Executor);
}

// --- admin -----------------------------------------------------------------------------------

namespace
{
	/** "word rest of it" -> word, rest. */
	FString SplitFirst(const FString& In, FString& Rest)
	{
		FString First;
		if (!In.TrimStart().Split(TEXT(" "), &First, &Rest))
		{
			First = In.TrimStartAndEnd();
			Rest.Reset();
		}
		Rest.TrimStartAndEndInline();
		return First;
	}

	/** UE1 SetPropertyText / GetPropertyText through reflection. */
	bool SetPropertyText(UObject* Obj, const FString& Name, const FString& Value)
	{
		FProperty* P = Obj ? FindFProperty<FProperty>(Obj->GetClass(), FName(*Name)) : nullptr;
		return P && P->ImportText_InContainer(*Value, Obj, Obj, PPF_None) != nullptr;
	}

	bool GetPropertyText(UObject* Obj, const FString& Name, FString& Out)
	{
		FProperty* P = Obj ? FindFProperty<FProperty>(Obj->GetClass(), FName(*Name)) : nullptr;
		if (!P)
		{
			return false;
		}
		P->ExportText_InContainer(0, Out, Obj, Obj, Obj, PPF_None);
		return true;
	}

	/** A player's soldier, then their player state, then controller: the first that has it. */
	TArray<UObject*> PropertyTargets(AActor* A)
	{
		TArray<UObject*> Out{ A };
		if (const APawn* P = Cast<APawn>(A))
		{
			Out.Add(P->GetPlayerState());
			Out.Add(P->GetController());
		}
		return Out;
	}
}

AActor* UMFCheatManager::FindPlayer(const FString& Name, AController** OutController) const
{
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		AController* C = It->Get();
		if (C && C->PlayerState && C->PlayerState->GetPlayerName().Equals(Name, ESearchCase::IgnoreCase))
		{
			if (OutController)
			{
				*OutController = C;
			}
			// driving: the soldier, not the car
			const AMFVehicle* V = Cast<AMFVehicle>(C->GetPawn());
			return V && V->GetDriver() ? static_cast<AActor*>(V->GetDriver()) : (C->GetPawn() ? static_cast<AActor*>(C->GetPawn()) : C);
		}
	}
	return nullptr;
}

AActor* UMFCheatManager::LookedAt() const
{
	APlayerController* PC = GetPlayerController();
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFLookedAt), false, PC->GetPawn());
	GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + View.Vector() * 22500.f, ECC_GameTraceChannel2, Params);
	return Hit.GetActor();
}

void UMFCheatManager::Say(const FString& Line) const
{
	UE_LOG(LogMF, Display, TEXT("%s"), *Line);
	GetPlayerController()->ClientMessage(Line);
}

bool UMFCheatManager::IsAdmin() const
{
	const AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>();
	return GetWorld()->GetNetMode() == NM_Standalone || (GM && GM->IsAdmin(GetPlayerController()));
}

void UMFCheatManager::RunAs(const FString& PlayerAndCommand)
{
	FString Command;
	const FString Name = SplitFirst(PlayerAndCommand, Command);
	AController* C = nullptr;
	AActor* A = FindPlayer(Name, &C);
	if (!A || Command.IsEmpty())
	{
		Say(FString::Printf(TEXT("RunAs: no player %s"), *Name));
		return;
	}
	// MF: P.ConsoleCommand on their pawn (exec functions of the pawn, then the controller)
	FStringOutputDevice Out;
	APawn* P = Cast<APawn>(A);
	const bool bDone = (P && P->ProcessConsoleExec(*Command, Out, P)) || (C && C->ProcessConsoleExec(*Command, Out, P))
		|| (Cast<APlayerController>(C) && Cast<APlayerController>(C)->CheatManager && Cast<APlayerController>(C)->CheatManager->ProcessConsoleExec(*Command, Out, P));
	Say(bDone ? FString::Printf(TEXT("%s: %s"), *Name, *Command) : FString::Printf(TEXT("RunAs: %s can't %s"), *Name, *Command));
}

void UMFCheatManager::RunAsLooking(const FString& Command)
{
	if (const APawn* P = Cast<APawn>(LookedAt()); P && P->GetPlayerState())
	{
		RunAs(P->GetPlayerState()->GetPlayerName() + TEXT(" ") + Command);
	}
}

void UMFCheatManager::SetTo(const FString& PlayerPropertyValue)
{
	FString Rest, Value;
	const FString Name = SplitFirst(PlayerPropertyValue, Rest);
	const FString Prop = SplitFirst(Rest, Value);
	AActor* A = FindPlayer(Name);
	const bool bDone = A && Algo::AnyOf(PropertyTargets(A), [&](UObject* O) { return SetPropertyText(O, Prop, Value); });
	Say(bDone ? FString::Printf(TEXT("%s %s = %s"), *Name, *Prop, *Value) : FString::Printf(TEXT("SetTo: no %s on %s"), *Prop, *Name));
}

void UMFCheatManager::GetTo(const FString& PlayerAndProperty)
{
	FString Prop, Value;
	const FString Name = SplitFirst(PlayerAndProperty, Prop);
	AActor* A = FindPlayer(Name);
	const bool bDone = A && Algo::AnyOf(PropertyTargets(A), [&](UObject* O) { return GetPropertyText(O, Prop, Value); });
	Say(bDone ? FString::Printf(TEXT("%s %s = %s"), *Name, *Prop, *Value) : FString::Printf(TEXT("GetTo: no %s on %s"), *Prop, *Name));
}

void UMFCheatManager::SetToSelf(const FString& PropertyAndValue)
{
	if (const APlayerState* PS = GetPlayerController()->PlayerState)
	{
		SetTo(PS->GetPlayerName() + TEXT(" ") + PropertyAndValue);
	}
}

void UMFCheatManager::GetToSelf(const FString& Property)
{
	if (const APlayerState* PS = GetPlayerController()->PlayerState)
	{
		GetTo(PS->GetPlayerName() + TEXT(" ") + Property);
	}
}

void UMFCheatManager::SetToLooking(const FString& PropertyAndValue)
{
	FString Value;
	const FString Prop = SplitFirst(PropertyAndValue, Value);
	AActor* A = LookedAt();
	const bool bDone = A && Algo::AnyOf(PropertyTargets(A), [&](UObject* O) { return SetPropertyText(O, Prop, Value); });
	Say(bDone ? FString::Printf(TEXT("%s %s = %s"), *GetNameSafe(A), *Prop, *Value) : FString::Printf(TEXT("SetToLooking: no %s on %s"), *Prop, *GetNameSafe(A)));
}

void UMFCheatManager::GetToLooking(const FString& Property)
{
	FString Value;
	AActor* A = LookedAt();
	const bool bDone = A && Algo::AnyOf(PropertyTargets(A), [&](UObject* O) { return GetPropertyText(O, Property, Value); });
	Say(bDone ? FString::Printf(TEXT("%s %s = %s"), *GetNameSafe(A), *Property, *Value) : FString::Printf(TEXT("GetToLooking: no %s on %s"), *Property, *GetNameSafe(A)));
}

void UMFCheatManager::TeleportTo(const FString& PlayerName)
{
	APawn* Me = GetPlayerController()->GetPawn();
	const AActor* Them = FindPlayer(PlayerName);
	if (!Me || !Them)
	{
		return;
	}
	// ZombiePlayer.Teleport: a few random spots around them (VRand x (100, 100, 10) uu)
	for (int32 Try = 0; Try < 9; ++Try)
	{
		const FVector At = Them->GetActorLocation() + FMath::VRand() * FVector(225.f, 225.f, 22.5f);
		if (Me->TeleportTo(At, Me->GetActorRotation()))
		{
			return;
		}
	}
}

void UMFCheatManager::BecomeHuman()
{
	AMFGameMode_Zombie* GM = GetWorld()->GetAuthGameMode<AMFGameMode_Zombie>();
	if (AMFCharacter* C = Cast<AMFCharacter>(GetPlayerController()->GetPawn()); GM && C)
	{
		GM->BecomeHuman(C, false);
	}
}

void UMFCheatManager::BecomeSurvivor()
{
	AMFGameMode_Zombie* GM = GetWorld()->GetAuthGameMode<AMFGameMode_Zombie>();
	if (AMFCharacter* C = Cast<AMFCharacter>(GetPlayerController()->GetPawn()); GM && C)
	{
		GM->BecomeHuman(C, true);
	}
}

void UMFCheatManager::BecomeZombie()
{
	AMFGameMode_Zombie* GM = GetWorld()->GetAuthGameMode<AMFGameMode_Zombie>();
	if (AMFCharacter* C = Cast<AMFCharacter>(GetPlayerController()->GetPawn()); GM && C)
	{
		GM->BecomeZombie(C, false);
	}
}

void UMFCheatManager::BecomeNemesis()
{
	AMFGameMode_Zombie* GM = GetWorld()->GetAuthGameMode<AMFGameMode_Zombie>();
	if (AMFCharacter* C = Cast<AMFCharacter>(GetPlayerController()->GetPawn()); GM && C)
	{
		GM->BecomeZombie(C, true);
	}
}

void UMFCheatManager::Kick(const FString& PlayerName)
{
	AController* C = nullptr;
	FindPlayer(PlayerName, &C);
	APlayerController* Target = Cast<APlayerController>(C);
	AGameModeBase* GM = GetWorld()->GetAuthGameMode();
	if (!IsAdmin() || !Target || Target->IsLocalController() || !GM || !GM->GameSession)
	{
		Say(FString::Printf(TEXT("Kick: can't kick %s"), *PlayerName));
		return;
	}
	GM->GameSession->KickPlayer(Target, FText::FromString(TEXT("Kicked by an admin")));
}

void UMFCheatManager::SwitchMap(const FString& URL)
{
	if (!IsAdmin() || URL.IsEmpty())
	{
		return;
	}
	Say(FString::Printf(TEXT("Switching to %s"), *URL));
	GetWorld()->ServerTravel(URL);
}
