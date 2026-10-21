#include "Vehicles/MFVehicleTurret.h"
#include "Vehicles/MFVehicle.h"
#include "Core/MFContent.h"
#include "Core/MFGameSettings.h"
#include "Core/MFProps.h"
#include "Player/MFCharacter.h"
#include "Weapons/MFProjectile.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"

#define LOCTEXT_NAMESPACE "MFVehicleTurret"

namespace MFVehicleTurretLocal
{
	USoundBase* LoadMFSound(const FString& MFName)
	{
		FString Package, Name;
		return MFName.Split(TEXT("."), &Package, &Name) ? MF::LoadClassic<USoundBase>(FString::Printf(TEXT("Sounds/%s/%s"), *Package, *Name)) : nullptr;
	}
}

UMFVehicleTurret::UMFVehicleTurret()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UMFVehicleTurret::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMFVehicleTurret, TurretRot);
	DOREPLIFETIME(UMFVehicleTurret, ClipAmmo);
}

AMFVehicle* UMFVehicleTurret::GetVehicle() const
{
	return Cast<AMFVehicle>(GetOwner());
}

void UMFVehicleTurret::BeginPlay()
{
	Super::BeginPlay();
	ClipAmmo = MaxClipAmmo;
	TurretRot.Yaw = BaseYaw;
	AMFVehicle* V = GetVehicle();
	if (!V)
	{
		return;
	}
	TurretMesh = NewObject<UStaticMeshComponent>(V);
	TurretMesh->SetupAttachment(V->GetBody());
	TurretMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TurretMesh->RegisterComponent();
	if (!MF::ApplyPropMesh(TurretMesh, *TurretMeshName, TurretDrawScale))
	{
		TurretMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		TurretMesh->SetRelativeScale3D(FVector(1.2f, 0.15f, 0.15f));
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		FireSound = MFVehicleTurretLocal::LoadMFSound(MFFireSound);
		ReloadSound = MFVehicleTurretLocal::LoadMFSound(MFReloadSound);
	}
}

FVector UMFVehicleTurret::GunPos() const
{
	return TurretPivot + FRotator(0.f, TurretRot.Yaw, 0.f).RotateVector(GunOffset);
}

void UMFVehicleTurret::GetMuzzle(FVector& OutStart, FVector& OutDir) const
{
	const AMFVehicle* V = GetVehicle();
	const FTransform T = V->GetBody()->GetComponentTransform();
	OutStart = T.TransformPosition(GunPos() + TurretRot.RotateVector(MuzzleOffset) - V->GetBoxCenter());
	OutDir = T.TransformVectorNoScale(TurretRot.Vector());
}

void UMFVehicleTurret::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AMFVehicle* V = GetVehicle();
	if (!V)
	{
		return;
	}
	const TArray<TObjectPtr<AMFCharacter>>& Passengers = V->GetPassengers();
	AMFCharacter* Gunner = Passengers.IsValidIndex(GunnerSeat) ? Passengers[GunnerSeat].Get() : nullptr;
	if (GetOwnerRole() == ROLE_Authority)
	{
		// aim: the gunner's view in the vehicle's frame, pitch limited, turning at TurnRate
		if (Gunner && Gunner->GetController())
		{
			FRotator Want = V->GetBody()->GetComponentTransform().InverseTransformRotation(Gunner->GetController()->GetControlRotation().Quaternion()).Rotator();
			Want.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Want.Pitch), PitchMin, PitchMax);
			Want.Yaw = BaseYaw + FMath::Clamp(FRotator::NormalizeAxis(Want.Yaw - BaseYaw), -YawLimit, YawLimit);
			const float Step = TurnRate * DeltaTime;
			const FRotator Delta = (Want - TurretRot).GetNormalized();
			TurretRot = FRotator(TurretRot.Pitch + FMath::Clamp(Delta.Pitch, -Step, Step), FRotator::NormalizeAxis(TurretRot.Yaw + FMath::Clamp(Delta.Yaw, -Step, Step)), 0.f);
		}
		if (FireDelay > 0.f)
		{
			FireDelay -= DeltaTime;
			if (FireDelay <= 0.f && ClipAmmo == 0)
			{
				ClipAmmo = MaxClipAmmo;
			}
		}
		else if (Gunner && bGunnerFiring)
		{
			Fire(Gunner);
		}
	}
	if (TurretMesh)
	{
		TurretMesh->SetRelativeLocationAndRotation(GunPos() - V->GetBoxCenter(), TurretRot);
	}
}

void UMFVehicleTurret::Fire(AMFCharacter* Gunner)
{
	AMFVehicle* V = GetVehicle();
	FVector Start, Dir;
	GetMuzzle(Start, Dir);
	const FRotationMatrix Axes(Dir.Rotation());
	Dir = (Axes.GetUnitAxis(EAxis::X) + (FMath::FRand() - 0.5f) * Spread * Axes.GetUnitAxis(EAxis::Y)
		+ (FMath::FRand() - 0.5f) * Spread * Axes.GetUnitAxis(EAxis::Z)).GetSafeNormal();
	if (ProjectileClass)
	{
		FActorSpawnParameters SP;
		SP.Owner = Gunner;
		SP.Instigator = Gunner;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AMFProjectile* P = GetWorld()->SpawnActor<AMFProjectile>(ProjectileClass, Start, Dir.Rotation(), SP))
		{
			P->Launch(Dir, 0.f, V->GetVelocity());
		}
	}
	else
	{
		// TraceFire: through the vehicle itself and its riders
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MFVehicleTurret), false, V);
		Params.AddIgnoredActor(Gunner);
		Params.AddIgnoredActor(V->GetDriver());
		for (AMFCharacter* P : V->GetPassengers())
		{
			Params.AddIgnoredActor(P);
		}
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Dir * MaxRange, ECC_GameTraceChannel2, Params) && Hit.GetActor())
		{
			FPointDamageEvent Ev(Damage, Hit, Dir, UMFDamage_Bullet::StaticClass());
			Hit.GetActor()->TakeDamage(Damage, Ev, Gunner->GetController(), V);
		}
	}
	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, Start, 1.f, 1.f, 0.f, MF::GetAttenuation(1600.f));
	}
	V->MakeNoise(1.f, Gunner);
	--ClipAmmo;
	if (ClipAmmo > 0)
	{
		FireDelay = FireInterval;
	}
	else
	{
		Reload();
	}
}

void UMFVehicleTurret::Reload()
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}
	ClipAmmo = 0;
	FireDelay = ReloadTime;
	if (ReloadSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ReloadSound, GetOwner()->GetActorLocation(), 1.f, 1.f, 0.f, MF::GetAttenuation(1600.f));
	}
}

void UMFVehicleTurret::SeatEntered(AMFCharacter* C, int32 Seat)
{
	if (Seat != GunnerSeat || GetOwnerRole() != ROLE_Authority)
	{
		return;
	}
	// TakeTurret: hands off their own weapon, onto the gun
	GunnerLastWeapon = C->GetCurrentWeapon();
	if (AMFWeapon_VehicleTurret* Gun = Cast<AMFWeapon_VehicleTurret>(C->GiveWeapon(AMFWeapon_VehicleTurret::StaticClass(), false)))
	{
		Gun->Turret = this;
		Gun->HUDIconRect = HUDIconRect;
		GunnerWeapon = Gun;
		C->EquipWeapon(Gun);
	}
}

void UMFVehicleTurret::SeatLeft(AMFCharacter* C, int32 Seat)
{
	if (Seat != GunnerSeat || GetOwnerRole() != ROLE_Authority)
	{
		return;
	}
	bGunnerFiring = false;
	if (AMFWeapon_VehicleTurret* Gun = GunnerWeapon.Get())
	{
		C->RemoveWeapon(Gun);
	}
	GunnerWeapon = nullptr;
	if (AMFWeapon* Last = GunnerLastWeapon.Get(); Last && C->GetInventory().Contains(Last))
	{
		C->EquipWeapon(Last);
	}
	else
	{
		C->SwitchToBestWeapon();
	}
	GunnerLastWeapon = nullptr;
}

// --- the gunner's "weapon" -------------------------------------------------------------------

AMFWeapon_VehicleTurret::AMFWeapon_VehicleTurret()
{
	DisplayName = LOCTEXT("Turret", "Mounted gun");
	InventoryGroup = 12;
	CarrySize = 0;
	bLoadoutSelectable = false;
	bUndroppable = true;
	bAddToLoadout = false;
	MaxClipAmmo = 1;
	MaxClips = 1;
	EquipTime = 0.f;
	PutDownTime = 0.f;
	bHideAmmoCount = true;
	AIRating = -1.f;
	HUDIconRect = FIntRect(64, 64, 256, 128);   // the HMG's box on the weapon icon sheet
	Mesh1P->SetVisibility(false);              // the gun is on the vehicle, nothing in the hands
}

void AMFWeapon_VehicleTurret::StartFire(bool bAlt)
{
	bFireHeld = true;
	if (!HasAuthority())
	{
		ServerStartFire(bAlt);
	}
	else if (UMFVehicleTurret* T = Turret.Get())
	{
		T->bGunnerFiring = true;
	}
}

void AMFWeapon_VehicleTurret::StopFire(bool bAlt)
{
	bFireHeld = false;
	if (!HasAuthority())
	{
		ServerStopFire(bAlt);
	}
	else if (UMFVehicleTurret* T = Turret.Get())
	{
		T->bGunnerFiring = false;
	}
}

void AMFWeapon_VehicleTurret::StartReload()
{
	// ArmedAPCWeapon.Reload: a part-used belt can be swapped early
	if (UMFVehicleTurret* T = Turret.Get(); T && HasAuthority() && T->GetClipAmmo() > 0)
	{
		T->Reload();
	}
	else if (!HasAuthority())
	{
		Super::StartReload();
	}
}

bool AMFWeapon_VehicleTurret::DrawHUD(UCanvas* Canvas)
{
	const AMFCharacter* Me = GetOwnerCharacter();
	const AMFVehicle* V = Me ? Cast<AMFVehicle>(Me->GetAttachParentActor()) : nullptr;
	const UMFVehicleTurret* T = nullptr;
	if (V)
	{
		TInlineComponentArray<UMFVehicleTurret*> Turrets(V);
		for (const UMFVehicleTurret* Each : Turrets)
		{
			if (V->GetSeatOf(Me) == Each->GunnerSeat)
			{
				T = Each;
			}
		}
	}
	if (!T || !Canvas)
	{
		return false;
	}
	// crosshair where the gun points (PostRender), the belt below it
	FVector Start, Dir;
	T->GetMuzzle(Start, Dir);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFTurretAim), false, V);
	Params.AddIgnoredActor(Me);
	const FVector End = GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Dir * T->GetMaxRange(), ECC_Visibility, Params) ? Hit.ImpactPoint : Start + Dir * T->GetMaxRange();
	const FVector S = Canvas->Project(End);
	if (S.Z <= 0.f)
	{
		return true;
	}
	const FLinearColor Col = UMFGameSettings::Get().GetTeamColor(Me->GetTeam());
	Canvas->K2_DrawLine(FVector2D(S.X - 14.f, S.Y), FVector2D(S.X + 14.f, S.Y), 2.f, Col);
	Canvas->K2_DrawLine(FVector2D(S.X, S.Y - 14.f), FVector2D(S.X, S.Y + 14.f), 2.f, Col);
	Canvas->SetDrawColor(Col.ToFColor(true));
	Canvas->DrawText(GEngine->GetMediumFont(), T->GetClipAmmo() > 0 ? FString::FromInt(T->GetClipAmmo()) : FString(TEXT("RELOADING")), S.X - 16.f, S.Y + 20.f);
	return true;
}

#undef LOCTEXT_NAMESPACE
