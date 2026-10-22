#include "Vehicles/Heli/Heli.h"
#include "Core/DFContent.h"
#include "Core/DFProps.h"
#include "Player/DFCharacter.h"
#include "Vehicles/DFVehicleTurret.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "DFVehicles"

namespace
{
	// heli defaultproperties (x 2.25: uu -> cm)
	const FVector MainRotorPos(135.f, 4.5f, 292.5f);     // mainRotorPos (60, 2, 130)
	const FVector TailRotorPos(-731.25f, -22.5f, 105.75f); // tailRotorPos (-325, -10, 47)
	const FVector GlassPos[4] = { FVector(391.5f, -45.f, 90.f), FVector(391.5f, 67.5f, 94.5f), FVector(477.f, -51.75f, -22.5f), FVector(477.f, 67.5f, -22.5f) };
	const TCHAR* GlassMeshes[4] = { TEXT("heli/heliGlass_topLeft"), TEXT("heli/heliGlass_topRight"), TEXT("heli/heliGlass_bottomLeft"), TEXT("heli/heliGlass_bottomRight") };
	constexpr float IdlePitch = 58.f;               // idlePitch
	constexpr float PitchChange = 0.15f;            // soundPitchChange, a tick
	constexpr float HeightChange = 225.f;        // heightChangeInUnrealUnits 100
	constexpr float HeightDiffDivisor = 1125.f;  // destCurrentHeightDiffDivisor 500
}

AHeli::AHeli()
{
	DisplayName = LOCTEXT("Heli", "Heli");
	bBotsDrive = false;                          // BotDesireability: only with a player at the controls
	BodyMeshName = TEXT("heli/heliMesh");
	BoxCenter = FVector(75.f, 12.f, 0.f);         // the cabin of heliMesh (-801 .. 577 cm with the tail boom)
	BoxExtent = FVector(375.f, 140.f, 140.f);
	MassKg = 1500.f;                             // handling HELI Mass 1500
	TopSpeedKmh = 126.f;                         // air drag: about 30 m/s flat out
	AccelTime = 6.f;
	Armour = 0.68f;
	NitroAccel = 0.f;                            // Crouch lowers it instead
	PetrolCapPosition = FVector(-60.75f, -105.75f, 29.25f);   // PetrolCapPos
	PetrolCapRadius = 9.f;
	CameraDistance = 1462.5f;                  // NearCamera 650 uu
	EngineLoopSound = TEXT("Sounds/heli/heliEngineLoop");
	// aSeatsPos (DriverSeat 1 first), aDoorsPos
	SeatPositions = { FVector(360.f, -47.25f, 90.f), FVector(360.f, 74.25f, 90.f), FVector(168.75f, 123.75f, 56.25f), FVector(168.75f, -90.f, 56.25f) };
	DoorPositions = { FVector(416.25f, -157.5f, 0.f), FVector(416.25f, 202.5f, 0.f), FVector(202.5f, -180.f, 0.f), FVector(202.5f, 202.5f, 0.f) };

	auto Part = [this](const TCHAR* Name, const FVector& Pos)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Body);
		C->SetRelativeLocation(Pos - BoxCenter);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetCastShadow(false);
		return C;
	};
	MainRotor = Part(TEXT("MainRotor"), MainRotorPos);
	TailRotor = Part(TEXT("TailRotor"), TailRotorPos);
	for (int32 I = 0; I < 4; ++I)
	{
		UStaticMeshComponent* G = Part(*FString::Printf(TEXT("Glass%d"), I), GlassPos[I]);
		// heliGlass blocks shots (bBlockActors, CollisionRadius 48) until it shatters
		G->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		G->SetCollisionResponseToAllChannels(ECR_Ignore);
		G->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
		G->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
		G->BodyInstance.bAutoWeld = false;
		Glass.Add(G);
	}
}

void AHeli::BeginPlay()
{
	Super::BeginPlay();
	Body->SetAngularDamping(1.f);   // the Rage physics' own spin damping (2 turned too slowly)
	// ApplyPropMesh places the mesh: keep the parts where the constructor put them
	DF::ApplyPropMesh(MainRotor, TEXT("heli/heliMainRotorTemp"), 1.f, MainRotor->GetRelativeLocation());
	DF::ApplyPropMesh(TailRotor, TEXT("heli/heliTailRotorTemp"), 1.f, TailRotor->GetRelativeLocation());
	for (int32 I = 0; I < Glass.Num(); ++I)
	{
		DF::ApplyPropMesh(Glass[I], GlassMeshes[I], 1.f, Glass[I]->GetRelativeLocation());
	}
	GlassHealth.Init(300, Glass.Num());
	GlassTimers.SetNum(Glass.Num());
	HoverHeight = GetActorLocation().Z + 112.5f;
}

void AHeli::Tick(float DeltaSeconds)
{
	// the engine spins up with a pilot in and down without (soundPitch +-0.15 a tick); the rotors
	// turn (pitch - 15) x 100 (main) / x 200 (tail) UE1 units a tick, slowing down: pitch x 100 / 200
	const float Ticks = DeltaSeconds * 60.f;
	const bool bPilot = GetDriver() != nullptr && !IsBlownUp();
	EnginePitch = FMath::Clamp(EnginePitch + (bPilot ? 1.f : -1.f) * PitchChange * Ticks, 0.f, IdlePitch);
	const float Spin = bPilot ? FMath::Max(EnginePitch - 15.f, 0.f) : EnginePitch;
	MainRotorYaw = FMath::Fmod(MainRotorYaw + Spin * 100.f * Ticks * 360.f / 65536.f, 360.f);
	TailRotorPitch = FMath::Fmod(TailRotorPitch + Spin * 200.f * Ticks * 360.f / 65536.f, 360.f);
	MainRotor->SetRelativeRotation(FRotator(0.f, MainRotorYaw, 0.f));
	TailRotor->SetRelativeRotation(FRotator(TailRotorPitch, 0.f, 0.f));
	Super::Tick(DeltaSeconds);
}

void AHeli::SimulateDrive(float Dt)
{
	const float Ticks = Dt * 60.f;   // deltaMultiplier
	const FTransform T = Body->GetComponentTransform();
	const float Z = T.GetLocation().Z;
	if (!GetDriver() || EnginePitch < IdlePitch)
	{
		// no pilot / still spinning up: no lift; once it rests, it will lift 50 uu off where it is
		if (FMath::Abs(Body->GetPhysicsLinearVelocity().Z) < 20.f)
		{
			HoverHeight = Z + 112.5f;
		}
		return;
	}
	// Rage's angular velocity turns the other way round: every MF rotVel below is negated
	const FVector X = T.GetUnitAxis(EAxis::X), Y = T.GetUnitAxis(EAxis::Y), Up = T.GetUnitAxis(EAxis::Z);
	const FRotator YawOnly(0.f, T.Rotator().Yaw, 0.f);
	const FVector YawX = YawOnly.Vector(), YawY = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);
	const FVector Vel = Body->GetPhysicsLinearVelocity() / 100.f;   // m/s
	const float HSpeed = FVector(Vel.X, Vel.Y, 0.f).Size();
	const bool bAccel = Throttle > 0.1f, bBrake = Throttle < -0.1f, bRight = Steer > 0.1f, bLeft = Steer < -0.1f;
	FVector Travel = FVector::ZeroVector, Side = FVector::ZeroVector, RotVel = FVector::ZeroVector;
	const bool bUnderMax = HSpeed < 1700.f;   // horizontalSpeedMax
	if (bAccel)
	{
		Travel = bUnderMax ? YawX : FVector::ZeroVector;
		RotVel = X.Z > -0.3f ? -Y / 100.f : (X.Z < -0.6f ? Y / 100.f : FVector::ZeroVector);   // nose 0.3 .. 0.6 down
	}
	else if (bBrake)
	{
		Travel = bUnderMax ? -YawX : FVector::ZeroVector;
		RotVel = X.Z > 0.15f ? -Y / 100.f : (X.Z < 0.3f ? Y / 100.f : FVector::ZeroVector);
	}
	else
	{
		RotVel = X.Z > 0.f ? -Y / 20.f * FMath::Abs(X.Z) : Y / 20.f * FMath::Abs(X.Z) / 3.f;   // level out
	}
	if (bRight)
	{
		Side = bUnderMax ? YawY : FVector::ZeroVector;
		RotVel += Y.Z > -0.3f ? X / 100.f : (Y.Z < -0.6f ? -X / 100.f : FVector::ZeroVector);
	}
	else if (bLeft)
	{
		Side = bUnderMax ? -YawY : FVector::ZeroVector;
		RotVel += Y.Z < 0.3f ? -X / 100.f : (Y.Z > 0.6f ? X / 100.f : FVector::ZeroVector);
	}
	else
	{
		RotVel += Y.Z > 0.f ? X / 20.f * FMath::Abs(Y.Z) : -X / 20.f * FMath::Abs(Y.Z);
	}
	// hover height: Jump climbs, Crouch sinks; too far off and it starts from where it is
	if (bHandbrake)
	{
		HoverHeight = Z + HeightChange;
	}
	else if (bNitro)
	{
		HoverHeight = Z - HeightChange * 0.55f;
	}
	if (FMath::Abs(HoverHeight - Z) > HeightChange * 10.f)
	{
		HoverHeight = Z;
	}
	const FVector HoverDir = HoverHeight > Z ? FVector(0.f, 0.f, 1.f) : FVector::ZeroVector;
	const float HeightDiff = FMath::Min(FMath::Abs(HoverHeight - Z) / HeightDiffDivisor, 2.f);
	// Fire / AltFire: yaw left / right
	if (bDriverFire)
	{
		RotVel += Up / 40.f;
	}
	else if (bDriverAltFire)
	{
		RotVel -= Up / 40.f;
	}
	Body->SetPhysicsAngularVelocityInRadians(Body->GetPhysicsAngularVelocityInRadians() - RotVel * Ticks);
	// hover flight (forward flight needs 850 m/s, which it never reaches): keep the vertical
	// speed, slow the flat one 1 % a tick with no keys held, push along the keys and up
	// the lift is weighed against the Rage physics' 9.81 m/s^2: here vehicles fall at the
	// world's gravity (MF's 2.2x stronger pawn gravity), so it is scaled up to match
	const float Lift = -GetWorld()->GetGravityZ() / 981.f;
	const bool bNoKeys = !bAccel && !bBrake && !bRight && !bLeft;
	FVector NewVel = FVector(0.f, 0.f, Vel.Z) + FVector(Vel.X, Vel.Y, 0.f) * (1.f - (bNoKeys ? 0.01f * Ticks : 0.f))
		+ Ticks * ((Travel + Side) * 0.1f * (HSpeed < 850.f ? 1.f : 0.f) + HoverDir * HeightDiff * Lift);
	Body->SetPhysicsLinearVelocity(NewVel * 100.f);
}

float AHeli::BotDesirability(const ADFCharacter* Bot) const
{
	// heli.BotDesireability: only with a player flying it (playerPawnPilot)
	const ADFCharacter* Pilot = GetDriver();
	return Pilot && Pilot->IsPlayerControlled() ? SeatDesirability(Bot) : -1.f;
}

float AHeli::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// a shot into a pane cracks it (heliGlass.TakeDamage; not from people in a vehicle)
	if (HasAuthority() && DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const UPrimitiveComponent* Hit = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.GetComponent();
		const int32 Pane = Glass.IndexOfByKey(Hit);
		if (Pane != INDEX_NONE)
		{
			const APawn* By = EventInstigator ? EventInstigator->GetPawn() : nullptr;
			if (By && (Cast<ADFVehicle>(By) || Cast<ADFVehicle>(By->GetAttachParentActor())))
			{
				return 0.f;
			}
			const int32 Before = GlassHealth[Pane];
			GlassHealth[Pane] -= FMath::RoundToInt(Damage);
			if (GlassHealth[Pane] <= 0)
			{
				MulticastGlass(uint8(Pane), 2);
				GetWorldTimerManager().SetTimer(GlassTimers[Pane], FTimerDelegate::CreateWeakLambda(this, [this, Pane]()
				{
					GlassHealth[Pane] = 300;
					MulticastGlass(uint8(Pane), 3);
				}), 20.f, false);
			}
			else
			{
				MulticastGlass(uint8(Pane), Before >= 150 && GlassHealth[Pane] < 150 ? 1 : 0);
			}
			return 0.f;
		}
	}
	return Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
}

void AHeli::SetGlassIntact(int32 Pane, bool bIntact)
{
	if (Glass.IsValidIndex(Pane))
	{
		Glass[Pane]->SetVisibility(bIntact);
		Glass[Pane]->SetCollisionEnabled(bIntact ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}

void AHeli::MulticastGlass_Implementation(uint8 Pane, uint8 State)
{
	if (!Glass.IsValidIndex(Pane))
	{
		return;
	}
	const FVector At = Glass[Pane]->GetComponentLocation();
	if (State == 2)
	{
		SetGlassIntact(Pane, false);
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(TEXT("Sounds/levelSFX/GlassSmash")), At);
	}
	else if (State == 3)
	{
		SetGlassIntact(Pane, true);
	}
	else
	{
		// glass_hit_01 (700 uu, pitch 0.8), glass_cracked once below half health
		DF::PlaySoundRadius(this, DF::LoadClassic<USoundBase>(State == 1 ? TEXT("Sounds/heli/glass_cracked") : TEXT("Sounds/heli/glass_hit_01")),
			At, 1.f, 700.f, 0.8f);
	}
}

AHeliHmg::AHeliHmg()
{
	DisplayName = LOCTEXT("HeliHmg", "Heli (door guns)");
	BodyMeshName = TEXT("heli/heliMeshHmg");
	// hmgSeat 2 / 3: aSeatsPos (Z=60) standing at the guns
	SeatPositions[2] = FVector(169.875f, 128.25f, 135.f);
	SeatPositions[3] = FVector(169.875f, -94.5f, 135.f);
	auto Gun = [this](const TCHAR* Name, int32 Seat, const FVector& Pos, float Yaw)
	{
		UDFVehicleTurret* G = CreateDefaultSubobject<UDFVehicleTurret>(Name);
		// the jeep's vehWeap (MachineGun, 3000 a belt)
		G->GunnerSeat = Seat;
		G->TurretPivot = Pos;
		G->GunOffset = FVector::ZeroVector;
		G->MuzzleOffset = FVector(107.f, 0.f, 0.f);
		G->BaseYaw = Yaw;                    // vehicleWeapRot
		G->YawLimit = 90.f;                  // aSeatsRotLimit Yaw 16384
		G->PitchMin = -90.f;
		G->PitchMax = 90.f;
		G->TurnRate = 720.f;
		G->TurretMeshName = TEXT("jeep/jeepWeapMesh");
		G->FireInterval = 0.117f;
		G->MaxClipAmmo = 3000;
		G->ReloadTime = 4.f;
		G->Spread = 0.03f;
		G->Damage = 20.f;
		G->MaxRange = 22500.f;
		G->ClassicFireSound = TEXT("jeep.jeepWeapFire");
		G->ClassicReloadSound = TEXT("jeep.hmgReload");
		return G;
	};
	RightGun = Gun(TEXT("RightGun"), 2, FVector(169.875f, 128.25f, 101.25f), 90.f);    // (75.5, 57, 45) uu
	LeftGun = Gun(TEXT("LeftGun"), 3, FVector(169.875f, -94.5f, 101.25f), -90.f);     // (75.5, -42, 45) uu
}

#undef LOCTEXT_NAMESPACE
