#include "Vehicles/ArmedAPC/MFArmedAPC.h"
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

#define LOCTEXT_NAMESPACE "MFArmedAPC"

namespace MFArmedAPCLocal
{
	USoundBase* LoadMFSound(const FString& MFName)
	{
		FString Package, Name;
		return MFName.Split(TEXT("."), &Package, &Name) ? MF::LoadClassic<USoundBase>(FString::Printf(TEXT("Sounds/%s/%s"), *Package, *Name)) : nullptr;
	}
}

AMFVehicle_ArmedAPC::AMFVehicle_ArmedAPC()
{
	DisplayName = LOCTEXT("ArmedAPC", "Armed APC");
	SeatPositions = { FVector(112.5f, 0.f, 58.5f), FVector(0.f, 0.f, 216.f) };   // aSeatsPos(1) Z 96: the hatch
	TurretMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Turret"));
	TurretMesh->SetupAttachment(Body);
	TurretMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

AMFVehicle_ArmedAPCRocket::AMFVehicle_ArmedAPCRocket()
{
	DisplayName = LOCTEXT("ArmedAPCRocket", "Armed APC (rockets)");
	TurnRate = 109.9f;            // 20000 / s
	TurretMeshName = TEXT("RageWeapons/RocketLauncherPickupCarryMesh");
	FireInterval = 0.8f;
	MaxClipAmmo = 4;
	Spread = 0.01f;
	ProjectileClass = AMFProjectile_Rocket::StaticClass();
	MFFireSound = TEXT("WeaponSFX_RocketLauncher.Fire");
	MFReloadSound = TEXT("WeaponSFX_RocketLauncher.Reload");
}

void AMFVehicle_ArmedAPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFVehicle_ArmedAPC, TurretRot);
	DOREPLIFETIME(AMFVehicle_ArmedAPC, ClipAmmo);
}

void AMFVehicle_ArmedAPC::BeginPlay()
{
	Super::BeginPlay();
	ClipAmmo = MaxClipAmmo;
	if (!MF::ApplyPropMesh(TurretMesh, *TurretMeshName, TurretDrawScale))
	{
		TurretMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		TurretMesh->SetRelativeScale3D(FVector(1.2f, 0.15f, 0.15f));
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		FireSound = MFArmedAPCLocal::LoadMFSound(MFFireSound);
		ReloadSoundAsset = MFArmedAPCLocal::LoadMFSound(MFReloadSound);
	}
}

FVector AMFVehicle_ArmedAPC::GunPos() const
{
	return TurretPivot + FRotator(0.f, TurretRot.Yaw, 0.f).RotateVector(GunOffset);
}

void AMFVehicle_ArmedAPC::GetMuzzle(FVector& OutStart, FVector& OutDir) const
{
	const FTransform T = Body->GetComponentTransform();
	OutStart = T.TransformPosition(GunPos() + TurretRot.RotateVector(MuzzleOffset) - BoxCenter);
	OutDir = T.TransformVectorNoScale(TurretRot.Vector());
}

void AMFVehicle_ArmedAPC::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const AMFCharacter* Gunner = Passengers.IsValidIndex(GunnerSeat) ? Passengers[GunnerSeat].Get() : nullptr;
	if (HasAuthority())
	{
		// aim: the gunner's view in the vehicle's frame, pitch limited, turning at TurnRate
		if (Gunner && Gunner->GetController())
		{
			FRotator Want = Body->GetComponentTransform().InverseTransformRotation(Gunner->GetController()->GetControlRotation().Quaternion()).Rotator();
			Want.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Want.Pitch), PitchMin, PitchMax);
			const float Step = TurnRate * DeltaSeconds;
			const FRotator Delta = (Want - TurretRot).GetNormalized();
			TurretRot = FRotator(TurretRot.Pitch + FMath::Clamp(Delta.Pitch, -Step, Step), FRotator::NormalizeAxis(TurretRot.Yaw + FMath::Clamp(Delta.Yaw, -Step, Step)), 0.f);
		}
		if (FireDelay > 0.f)
		{
			FireDelay -= DeltaSeconds;
			if (FireDelay <= 0.f && ClipAmmo == 0)
			{
				ClipAmmo = MaxClipAmmo;
			}
		}
		else if (Gunner && bGunnerFiring)
		{
			FireTurret(const_cast<AMFCharacter*>(Gunner));
		}
	}
	TurretMesh->SetRelativeLocationAndRotation(GunPos() - BoxCenter, TurretRot);
}

void AMFVehicle_ArmedAPC::FireTurret(AMFCharacter* Gunner)
{
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
			P->Launch(Dir, 0.f, GetVelocity());
		}
	}
	else
	{
		// TraceFire: through the APC itself and its riders
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MFAPCTurret), false, this);
		Params.AddIgnoredActor(Gunner);
		if (Driver)
		{
			Params.AddIgnoredActor(Driver);
		}
		for (AMFCharacter* P : Passengers)
		{
			Params.AddIgnoredActor(P);
		}
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Dir * MaxRange, ECC_GameTraceChannel2, Params) && Hit.GetActor())
		{
			FPointDamageEvent Ev(Damage, Hit, Dir, UMFDamage_Bullet::StaticClass());
			Hit.GetActor()->TakeDamage(Damage, Ev, Gunner->GetController(), this);
		}
	}
	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, Start);
	}
	MakeNoise(1.f, Gunner);
	--ClipAmmo;
	if (ClipAmmo > 0)
	{
		FireDelay = FireInterval;
	}
	else
	{
		ReloadTurret();
	}
}

void AMFVehicle_ArmedAPC::ReloadTurret()
{
	if (!HasAuthority())
	{
		return;
	}
	ClipAmmo = 0;
	FireDelay = ReloadTime;
	if (ReloadSoundAsset)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ReloadSoundAsset, GetActorLocation());
	}
}

void AMFVehicle_ArmedAPC::PassengerEntered(AMFCharacter* C, int32 Seat)
{
	if (Seat != GunnerSeat || !HasAuthority())
	{
		return;
	}
	// TakeTurret: hands off their own weapon, onto the gun
	GunnerLastWeapon = C->GetCurrentWeapon();
	if (AMFWeapon_APCTurret* Gun = Cast<AMFWeapon_APCTurret>(C->GiveWeapon(AMFWeapon_APCTurret::StaticClass(), false)))
	{
		Gun->APC = this;
		GunnerWeapon = Gun;
		C->EquipWeapon(Gun);
	}
}

void AMFVehicle_ArmedAPC::PassengerLeft(AMFCharacter* C, int32 Seat)
{
	if (Seat != GunnerSeat || !HasAuthority())
	{
		return;
	}
	bGunnerFiring = false;
	if (AMFWeapon_APCTurret* Gun = GunnerWeapon.Get())
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

AMFWeapon_APCTurret::AMFWeapon_APCTurret()
{
	DisplayName = LOCTEXT("Turret", "APC Turret");
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
	Mesh1P->SetVisibility(false);              // the gun is the APC's turret, nothing in the hands
}

void AMFWeapon_APCTurret::StartFire(bool bAlt)
{
	bFireHeld = true;
	if (!HasAuthority())
	{
		ServerStartFire(bAlt);
	}
	else if (AMFVehicle_ArmedAPC* V = APC.Get())
	{
		V->bGunnerFiring = true;
	}
}

void AMFWeapon_APCTurret::StopFire(bool bAlt)
{
	bFireHeld = false;
	if (!HasAuthority())
	{
		ServerStopFire(bAlt);
	}
	else if (AMFVehicle_ArmedAPC* V = APC.Get())
	{
		V->bGunnerFiring = false;
	}
}

void AMFWeapon_APCTurret::StartReload()
{
	// ArmedAPCWeapon.Reload: a part-used belt can be swapped early
	if (AMFVehicle_ArmedAPC* V = APC.Get(); V && HasAuthority() && V->GetClipAmmo() > 0)
	{
		V->ReloadTurret();
	}
	else if (!HasAuthority())
	{
		Super::StartReload();
	}
}

bool AMFWeapon_APCTurret::DrawHUD(UCanvas* Canvas)
{
	const AMFCharacter* Me = GetOwnerCharacter();
	const AMFVehicle_ArmedAPC* V = Me ? Cast<AMFVehicle_ArmedAPC>(Me->GetAttachParentActor()) : nullptr;
	if (!V || !Canvas)
	{
		return false;
	}
	// crosshair where the gun points (PostRender), the belt below it
	FVector Start, Dir;
	V->GetMuzzle(Start, Dir);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MFAPCAim), false, V);
	Params.AddIgnoredActor(Me);
	const FVector End = GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Dir * V->GetMaxRange(), ECC_Visibility, Params) ? Hit.ImpactPoint : Start + Dir * V->GetMaxRange();
	const FVector S = Canvas->Project(End);
	if (S.Z <= 0.f)
	{
		return true;
	}
	const FLinearColor Col = UMFGameSettings::Get().GetTeamColor(Me->GetTeam());
	Canvas->K2_DrawLine(FVector2D(S.X - 14.f, S.Y), FVector2D(S.X + 14.f, S.Y), 2.f, Col);
	Canvas->K2_DrawLine(FVector2D(S.X, S.Y - 14.f), FVector2D(S.X, S.Y + 14.f), 2.f, Col);
	Canvas->SetDrawColor(Col.ToFColor(true));
	Canvas->DrawText(GEngine->GetMediumFont(), V->GetClipAmmo() > 0 ? FString::FromInt(V->GetClipAmmo()) : FString(TEXT("RELOADING")), S.X - 16.f, S.Y + 20.f);
	return true;
}

#undef LOCTEXT_NAMESPACE
