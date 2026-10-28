#include "Weapons/PredatorMissile/PredatorMissile.h"
#include "World/DFEffectActor.h"
#include "Particles/ParticleSystemComponent.h"
#include "Core/DFContent.h"
#include "DesktopForces.h"
#include "Core/DFProps.h"
#include "Core/DFGameSettings.h"
#include "Game/DFGameState.h"
#include "Player/DFCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "DFPredator"

// --- missile -------------------------------------------------------------------------------

APredatorRocket::APredatorRocket() {
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	bAlwaysRelevant = true; // MF bAlwaysRelevant: everyone sees it coming
	InitialLifeSpan = 20.f;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(36.f); // CollisionRadius 16
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetOwnerNoSee(true); // the guider looks out of its nose (bOwnerNoSee)
	Mesh->CastShadow = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (Cone.Succeeded()) {
		Mesh->SetStaticMesh(Cone.Object);
		Mesh->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
		Mesh->SetRelativeScale3D(FVector(0.4f, 0.4f, 1.2f));
	}
}

void APredatorRocket::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APredatorRocket, bBoosting);
	DOREPLIFETIME(APredatorRocket, bExploded);
}

void APredatorRocket::InitializeFlight(APredatorMissile* Weapon, float CeilingHeight) {
	Controller = Weapon;
	// BaseSpeed = clamp(clamp(height, 600, 10000 uu) / TargetFlightTime, Speed 280, MaxSpeed 1600) uu/s
	BaseSpeed = FMath::Clamp(FMath::Clamp(CeilingHeight, 1350.f, 22500.f) / TargetFlightTime, 630.f, 3600.f);
	CurrentSpeed = BaseSpeed * InitialSpeedFactor;
	if (APawn* Guider = GetInstigator())
		Collision->IgnoreActorWhenMoving(Guider, true);
}

FRotator APredatorRocket::ClampDive(FRotator R) const {
	R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch), -MaxDiveAngle, -MinDiveAngle); // ClampDivePitch
	R.Roll = 0.f;
	return R;
}

AActor* APredatorRocket::FindBotTarget() const {
	const ADFCharacter* Me = Cast<ADFCharacter>(GetInstigator());
	AActor* Best = nullptr;
	float BestDist = 45000.f; // 20000 uu
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It) {
		if (*It == Me || !It->IsAlive() || (Me && Me->GetTeam() != DF::NoTeam && It->GetTeam() == Me->GetTeam()))
			continue;
		const float D = FVector::Dist(It->GetActorLocation(), GetActorLocation());
		if (D < BestDist) {
			BestDist = D;
			Best = *It;
		}
	}
	return Best;
}

void APredatorRocket::BeginPlay() {
	Super::BeginPlay();
	if (DF::ApplyPropMesh(Mesh, TEXT("RageWeapons/RocketMesh"), 2.5f)) // DrawScale 2.5
		Mesh->SetRelativeRotation(FRotator::ZeroRotator);

	// PredatorMissile: AmbientSound RocketFly, SoundRadius 255 (heard across the map)
	DF::PlayLoop(RootComponent, TEXT("WeaponSFX_RocketLauncher.RocketFly"), 6400.f);
	Trail = DF::SpawnEffectAttached(TEXT("RocketTrail"), RootComponent, 2.5f, true);
	if (Trail)
		Trail->SetOwnerNoSee(true); // the guider views the missile from its nose
}

void APredatorRocket::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || bExploded)
		return;
	FlightTime += DeltaSeconds;
	APawn* Guider = GetInstigator();
	const ADFCharacter* GuiderChar = Cast<ADFCharacter>(Guider);
	if (!GuiderChar || !GuiderChar->IsAlive()) {
		Explode(GetActorLocation()); // ServerTick: the guider died
		return;
	}
	AController* C = Guider->GetController();
	const APredatorMissile* W = Controller.Get();
	bBoosting = W && (W->IsFiring(false) || W->IsFiring(true));
	// speed: slow drop first, then cruise; boosting accelerates to 2.5x (SteerByPlayer)
	if (bBoosting)
		CurrentSpeed = FMath::Min(CurrentSpeed + 5625.f * DeltaSeconds, BaseSpeed * BoostMultiplier); // +2500 uu/s^2
	else if (FlightTime < InitialSlowDuration)
		CurrentSpeed = BaseSpeed * InitialSpeedFactor;
	else if (CurrentSpeed < BaseSpeed)
		CurrentSpeed = FMath::Min(CurrentSpeed + BaseSpeed * 0.8f * DeltaSeconds, BaseSpeed);
	else
		CurrentSpeed = FMath::Max(CurrentSpeed - 2700.f * DeltaSeconds, BaseSpeed); // -1200 uu/s^2
	// steering: toward the guider's view (players) or the nearest enemy (bots)
	FRotator Want = GetActorRotation();
	if (C && C->IsPlayerController())
		Want = C->GetControlRotation();
	else if (const AActor* T = FindBotTarget())
		Want = (T->GetActorLocation() - GetActorLocation()).Rotation();
	Want = ClampDive(Want);
	const float MaxStep = (bBoosting ? TurnRateBoost : TurnRateNormal) * DeltaSeconds;
	const FRotator Cur = GetActorRotation();
	const FRotator Delta = (Want - Cur).GetNormalized();
	const FRotator New(Cur.Pitch + FMath::Clamp(Delta.Pitch, -MaxStep, MaxStep), Cur.Yaw + FMath::Clamp(Delta.Yaw, -MaxStep, MaxStep), 0.f);
	SetActorRotation(New);
	if (C && C->IsPlayerController())
		C->SetControlRotation(New); // PP.ViewRotation = Rotation: the view sits on the missile
	FHitResult Hit;
	SetActorLocation(GetActorLocation() + New.Vector() * CurrentSpeed * DeltaSeconds, true, &Hit);
	if (Hit.bBlockingHit)
		Explode(Hit.ImpactPoint + Hit.ImpactNormal * 20.f);
}

void APredatorRocket::LifeSpanExpired() {
	if (HasAuthority() && !bExploded) {
		Explode(GetActorLocation());
		return;
	}
	Super::LifeSpanExpired();
}

void APredatorRocket::Explode(const FVector& At) {
	if (bExploded)
		return;
	bExploded = true;
	OnRep_Exploded();
	ForceNetUpdate();
	UE_LOG(LogDF, Log, TEXT("Predator: exploded at %s after %.1f s"), *At.ToCompactString(), FlightTime);
	TArray<AActor*> Ignore;
	UGameplayStatics::ApplyRadialDamageWithFalloff(this, Damage, 0.f, At, 0.f, BlastRadius, 1.f, UDFDamage_Explosion::StaticClass(),
		Ignore, this, GetInstigatorController(), ECC_Visibility);
	// AddExplosionServer(Size 6) + AddVehicleExplosionServer(Size 5)
	ADFEffectActor::Spawn(GetWorld(), TEXT("Explosion"), At, 2.4f, false, TEXT("Sounds/MiscSFX/Explode"), 8000.f, 1.f);
	ADFEffectActor::Spawn(GetWorld(), NAME_None, At, 2.f, true, TEXT("Sounds/MiscSFX/Explode"), 2000.f, 1.f);
	if (APredatorMissile* W = Controller.Get())
		W->MissileDone();
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetLifeSpan(0.3f); // let the explosion replicate
}

void APredatorRocket::Destroyed() {
	if (HasAuthority() && !bExploded) {
		if (APredatorMissile* W = Controller.Get())
			W->MissileDone();
	}
	Super::Destroyed();
}

// --- terminal ------------------------------------------------------------------------------

APredatorMissile::APredatorMissile() {
	DisplayName = LOCTEXT("Predator", "Predator Missile");
	ViewMesh = TEXT("RageWeapons/TripBombsPickupCarryMesh"); // PickupViewMesh
	ArmsMesh.Reset(); // no arms of its own: the carry model stays in hand
	HUDIcon = TEXT("Textures/PredatorMissile/Icons/PredatorIcon");
	HUDIconRect = FIntRect(0, 0, 128, 64); // WeaponIcon W=128 H=64
	bAddToLoadout = true;
	InventoryGroup = 11;
	CarrySize = 2;
	AIRating = 0.85f;
	MaxClipAmmo = 1;
	MaxClips = 1;
	bHideAmmoCount = false;
	FireTime = 0.2f;
	EquipTime = 0.5f;
	PutDownTime = 0.4f;
	RecoilPower = 0.f;
	ClassicSelectSound = TEXT("WeaponSFX_HeavyMachineGun.Select");
}

void APredatorMissile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APredatorMissile, ActiveMissile);
	DOREPLIFETIME(APredatorMissile, ReadyTime);
}

void APredatorMissile::PostInitializeComponents() {
	if (!DF::LoadClassic<UTexture2D>(TEXT("Textures/PredatorMissile/Icons/PredatorIcon"))) {
		HUDIcon = GetDefault<ADFWeapon>()->HUDIcon; // the weapon icon sheet: the trip bombs' box
		HUDIconRect = FIntRect(192, 0, 256, 64);
	}
	Super::PostInitializeComponents();
}

void APredatorMissile::OnGiven(ADFCharacter* NewOwner) {
	Super::OnGiven(NewOwner);
	if (HasAuthority() && ReadyTime <= 0.f)
		ReadyTime = GetWorld()->GetTimeSeconds() + WarmupDuration; // GiveTo: the terminal warms up
}

int32 APredatorMissile::WarmupSecondsLeft() const {
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const float Now = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	return Now < ReadyTime ? FMath::CeilToInt(ReadyTime - Now) : 0;
}

FString APredatorMissile::GetHUDStatus() const {
	const int32 Left = WarmupSecondsLeft();
	return Left > 0 ? FString::Printf(TEXT("Nuke ready in %ds"), Left) : FString();
}

bool APredatorMissile::FindLaunchLocation(FVector& OutLoc, FRotator& OutRot, float& OutCeiling) const {
	// FindLaunchLocation: the highest free air above the guider or 350 uu around
	const ADFCharacter* C = GetOwnerCharacter();
	if (!C)
		return false;
	const FVector Start = C->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFPredatorLaunch), false, C);
	FHitResult Hit;
	const float GroundZ = GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0, 0, 6750.f), ECC_Visibility, Params) ? Hit.ImpactPoint.Z : Start.Z - 90.f;
	const FVector Offsets[] = { FVector::ZeroVector, FVector(787.5f, 0, 0), FVector(-787.5f, 0, 0), FVector(0, 787.5f, 0), FVector(0, -787.5f, 0) };
	float Best = 0.f;
	FVector BestLoc = Start + FVector(0, 0, 2250.f);
	for (const FVector& O : Offsets) {
		const FVector P = Start + O;
		float Ceil, Height;
		if (GetWorld()->LineTraceSingleByChannel(Hit, P, P + FVector(0, 0, 18000.f), ECC_Visibility, Params)) {
			Ceil = Hit.ImpactPoint.Z - 216.f; // 96 uu under the roof / sky
			Height = Ceil - GroundZ;
		} else {
			Ceil = P.Z + 12375.f; // open sky: 5500 uu
			Height = 12375.f;
		}
		if (Height > Best) {
			Best = Height;
			BestLoc = FVector(P.X, P.Y, Ceil);
		}
	}
	if (Best < 900.f) // 400 uu
		return false;
	OutLoc = BestLoc;
	OutCeiling = Best;
	OutRot = (Start + FVector(0, 0, 112.5f) - BestLoc).Rotation();
	OutRot.Pitch = FMath::Clamp(FRotator::NormalizeAxis(OutRot.Pitch), -89.f, -5.f);
	return true;
}

bool APredatorMissile::TryFire(bool bAlt) {
	ADFCharacter* C = GetOwnerCharacter();
	if (!C || !C->IsAlive() || C->IsInLoadout() || ActiveMissile || !HasAmmoInClip())
		return false;
	APlayerController* PC = Cast<APlayerController>(C->GetController());
	if (const int32 Left = WarmupSecondsLeft(); Left > 0) {
		if (PC && HasAuthority())
			PC->ClientMessage(FString::Printf(TEXT("Nuke ready in %ds."), Left));
		return false;
	}
	if (!HasAuthority())
		return true; // the server launches; this client just waits for the view to switch
	FVector Loc;
	FRotator Rot;
	float Ceiling = 0.f;
	if (!FindLaunchLocation(Loc, Rot, Ceiling)) {
		if (PC)
			PC->ClientMessage(TEXT("Airspace blocked! Cannot establish missile trajectory."));
		return false;
	}
	FActorSpawnParameters SP;
	SP.Owner = C;
	SP.Instigator = C;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ActiveMissile = GetWorld()->SpawnActor<APredatorRocket>(APredatorRocket::StaticClass(), Loc, Rot, SP);
	if (!ActiveMissile)
		return false;
	ActiveMissile->InitializeFlight(this, Ceiling);
	UE_LOG(LogDF, Log, TEXT("Predator: launched from %s (free air %.0f cm)"), *Loc.ToCompactString(), Ceiling);
	if (USoundBase* Voice = DF::LoadClassic<USoundBase>(TEXT("Sounds/RagePlayerVoice/Nuke_the_whole_area")))
		DF::PlaySoundAtLocation(this, Voice, C->GetActorLocation());
	if (AController* Ctrl = C->GetController()) {
		Ctrl->SetIgnoreMoveInput(true); // FreezeGuider
		Ctrl->SetControlRotation(Rot);
		if (PC)
			PC->SetViewTargetWithBlend(ActiveMissile, 0.f);
	}
	SetState(EDFWeaponState::Firing, FireTime);
	return true;
}

void APredatorMissile::MissileDone() {
	if (!ActiveMissile)
		return;
	ActiveMissile = nullptr;
	ADFCharacter* C = GetOwnerCharacter();
	if (C) {
		if (AController* Ctrl = C->GetController()) {
			Ctrl->SetIgnoreMoveInput(false); // UnfreezeGuider
			if (APlayerController* PC = Cast<APlayerController>(Ctrl))
				PC->SetViewTargetWithBlend(C, 0.f);
		}
	}
	ClipAmmo = 0; // UseAmmo(1); empty: the terminal is gone
	if (C && HasAuthority()) {
		C->RemoveWeapon(this);
		C->SwitchToBestWeapon();
	}
}

void APredatorMissile::ForceInactive() {
	if (ActiveMissile && HasAuthority())
		ActiveMissile->Explode(ActiveMissile->GetActorLocation()); // put away mid-flight: it goes off
	Super::ForceInactive();
}

void APredatorMissile::OnPutDown() {
	if (ActiveMissile && HasAuthority())
		ActiveMissile->Explode(ActiveMissile->GetActorLocation());
	Super::OnPutDown();
}

void APredatorMissile::Destroyed() {
	if (ActiveMissile && HasAuthority()) {
		APredatorRocket* M = ActiveMissile;
		ActiveMissile = nullptr;
		M->Destroy();
	}
	Super::Destroyed();
}

bool APredatorMissile::DrawHUD(UCanvas* Canvas) {
	if (!ActiveMissile || !Canvas)
		return false;
	// guidance view: green frame, markers on soldiers (friends green, enemies red)
	const ADFCharacter* Me = GetOwnerCharacter();
	UFont* Font = GEngine->GetMediumFont();
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const FLinearColor Green(0.31f, 1.f, 0.47f), Red(1.f, 0.2f, 0.2f);
	Canvas->K2_DrawBox(FVector2D(W * 0.1f, H * 0.1f), FVector2D(W * 0.8f, H * 0.8f), 2.f, Green);
	Canvas->K2_DrawLine(FVector2D(W * 0.5f - 20.f, H * 0.5f), FVector2D(W * 0.5f + 20.f, H * 0.5f), 2.f, Green);
	Canvas->K2_DrawLine(FVector2D(W * 0.5f, H * 0.5f - 20.f), FVector2D(W * 0.5f, H * 0.5f + 20.f), 2.f, Green);
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It) {
		if (*It == Me || !It->IsAlive())
			continue;
		const FVector S = Canvas->Project(It->GetActorLocation());
		if (S.Z <= 0.f || S.X < 0.f || S.Y < 0.f || S.X > W || S.Y > H)
			continue;
		const bool bFriend = Me && Me->GetTeam() != DF::NoTeam && It->GetTeam() == Me->GetTeam();
		Canvas->K2_DrawBox(FVector2D(S.X - 12.f, S.Y - 12.f), FVector2D(24.f, 24.f), 2.f, bFriend ? Green : Red);
	}
	Canvas->SetDrawColor(FColor(80, 255, 120));
	Canvas->DrawText(Font, ActiveMissile->IsBoosting() ? TEXT("PREDATOR - BOOST") : TEXT("PREDATOR - hold Fire to boost"), W * 0.12f, H * 0.12f);
	return true;
}

#undef LOCTEXT_NAMESPACE

void APredatorRocket::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	DF::StopEffect(Trail);
	Super::EndPlay(EndPlayReason);
}

void APredatorRocket::OnRep_Exploded() {
	if (bExploded)
		DF::StopEffect(Trail);
}
