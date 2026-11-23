#include "Core/DFProps.h"
#include "CollisionQueryParams.h"
#include "Engine/DamageEvents.h"
#include "Core/DFUserSettings.h"
#include "Core/DFGameSettings.h"
#include "Core/DFContent.h"
#include "Game/DFPlayerState.h"
#include "GameFramework/Controller.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Misc/Paths.h"

FCollisionObjectQueryParams DF::SolidObjects() {
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	return Objects;
}

float DF::RadialDamageScale(const FDamageEvent& Event, const FVector& VictimAt, float VictimRadius) {
	if (!Event.IsOfType(FRadialDamageEvent::ClassID))
		return 1.f;
	const FRadialDamageEvent& Radial = static_cast<const FRadialDamageEvent&>(Event);
	const float Dist = FVector::Dist(VictimAt, Radial.Origin);
	return FMath::Clamp(1.f - FMath::Max(0.f, Dist - VictimRadius) / FMath::Max(Radial.Params.OuterRadius, 1.f), 0.f, 1.f);
}

float DF::RadialDamageScale(const FDamageEvent& Event, const FTransform& Box, const FVector& Extent) {
	if (!Event.IsOfType(FRadialDamageEvent::ClassID))
		return 1.f;
	const FRadialDamageEvent& Radial = static_cast<const FRadialDamageEvent&>(Event);
	const FVector Local = Box.InverseTransformPositionNoScale(Radial.Origin);
	const float Dist = (Local.GetAbs() - Extent).ComponentMax(FVector::ZeroVector).Size();
	return FMath::Clamp(1.f - Dist / FMath::Max(Radial.Params.OuterRadius, 1.f), 0.f, 1.f);
}

void DF::SetMovingLight(UPrimitiveComponent* C) {
	// nothing baked reaches a thing that moves: UE1 lit actors from where they stood, this is
	// a flat stand-in the same on every map. One add in the material, no lightmap, no bake
	C->SetCustomPrimitiveDataFloat(0, UDFGameSettings::Get().MovingActorLight * UDFUserSettings::Get().Brightness);
}

void DF::Tint(UStaticMeshComponent* C, const FLinearColor& Color) {
	if (!C || C->GetNetMode() == NM_DedicatedServer)
		return;
	if (UMaterialInterface* M = UDFGameSettings::Get().GetTeamTintMaterial(); M && C->GetMaterial(0) != M)
		C->SetMaterial(0, M);
	C->SetCustomPrimitiveDataVector4(0, FVector4(Color.R, Color.G, Color.B, 1.f));
}

void DF::PlaceBeam(UStaticMeshComponent* Beam, const FVector& Start, const FVector& End, const FLinearColor& Color) {
	const FVector Delta = End - Start;
	if (!Beam || Delta.SizeSquared() < 1.f)
		return;
	Tint(Beam, Color);
	Beam->SetWorldTransform(FTransform(Delta.Rotation(), Start + Delta * 0.5f, FVector(Delta.Size() / 100.f, 0.01f, 0.01f)));
	Beam->SetVisibility(true);
}

UMaterialInstanceDynamic* DF::MakeTexturedMID(const TCHAR* Master, const FString& Texture, UObject* Outer) {
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Master, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UTexture* Tex = DF::LoadClassic<UTexture>(Texture);
	if (!M || !Tex)
		return nullptr;
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(M, Outer);
	MID->SetTextureParameterValue(TEXT("Texture"), Tex);
	return MID;
}

bool DF::ApplyPropMesh(UStaticMeshComponent* C, const TCHAR* PackageAndName, float DrawScale, const FVector& Offset) {
	if (!C)
		return false;
	// community props (Desktop Forces' own) first, then the classic and unverified packs
	UStaticMesh* Mesh = DF::LoadClassic<UStaticMesh>(FString(TEXT("Props/")) + PackageAndName);
	if (!Mesh)
		return false;
	if (C->GetStaticMesh() != Mesh) {
		C->SetStaticMesh(Mesh);
		C->EmptyOverrideMaterials();
	}
	DF::SetMovingLight(C);
	if (C->GetAttachParent()) { // a root's "relative" is the world: that's where the map put it
		C->SetRelativeLocation(Offset);
		C->SetRelativeRotation(FRotator::ZeroRotator);
	}
	C->SetRelativeScale3D(FVector(DrawScale));
	return true;
}

USkeletalMeshComponent* DF::MakeFlag(USceneComponent* Parent, const TCHAR* Mesh, FName ClothSlot) {
	// Interchange's folders: <root>/Flags/<Mesh>/<Mesh>/SkeletalMeshes/<Mesh>[<Sequence>]
	USkeletalMesh* Asset = IsRunningDedicatedServer() ? nullptr : DF::LoadClassic<USkeletalMesh>(FString::Printf(TEXT("Flags/%s/%s/SkeletalMeshes/%s"), Mesh, Mesh, Mesh));
	if (!Asset)
		return nullptr;
	USkeletalMeshComponent* Flag = NewObject<USkeletalMeshComponent>(Parent->GetOwner());
	Flag->SetSkeletalMesh(Asset);
	Flag->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Flag->SetCastShadow(false);
	Flag->SetCanEverAffectNavigation(false);
	Flag->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Flag->SetupAttachment(Parent);
	Flag->RegisterComponent();
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MF/Materials/M_MF_VehicleFlag.M_MF_VehicleFlag")))
		Flag->CreateDynamicMaterialInstance(FMath::Max(Flag->GetMaterialIndex(ClothSlot), 0), M);
	PlayFlag(Flag, TEXT("FlagUpFlap"), true);
	return Flag;
}

UStaticMeshComponent* DF::MakeFlatFlag(USceneComponent* Parent, const FVector& At, float Width, float Height) {
	UStaticMeshComponent* Cloth = NewObject<UStaticMeshComponent>(Parent->GetOwner());
	Cloth->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
	Cloth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cloth->SetCastShadow(false);
	Cloth->SetCanEverAffectNavigation(false);
	Cloth->SetupAttachment(Parent);
	Cloth->RegisterComponent();
	Cloth->SetRelativeLocationAndRotation(At - FVector(Width / 2.f, 0.f, 0.f), FRotator(0.f, 0.f, 90.f));
	Cloth->SetRelativeScale3D(FVector(-Width / 100.f, Height / 100.f, 1.f)); // the emblem reads from the tied edge
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MF/Materials/M_MF_VehicleFlag.M_MF_VehicleFlag")))
		Cloth->CreateDynamicMaterialInstance(0, M);
	return Cloth;
}

float DF::PlayFlag(USkeletalMeshComponent* Flag, const TCHAR* Sequence, bool bLoop) {
	const USkeletalMesh* Mesh = Flag ? Flag->GetSkeletalMeshAsset() : nullptr;
	if (!Mesh)
		return 0.f;
	const FString Name = Mesh->GetName() + Sequence;
	UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s%s.%s"), *Mesh->GetPackage()->GetName(), Sequence, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Anim)
		return 0.f;
	Flag->PlayAnimation(Anim, bLoop);
	return Anim->GetPlayLength();
}

void DF::SetFlagEmblem(UMeshComponent* Flag, uint8 Team) {
	UTexture2D* Emblem = DF::LoadClassic<UTexture2D>(Team <= 1 ? FString::Printf(TEXT("Textures/Rage/Skins/VehicleFlagTex%d"), Team) : FString(TEXT("Textures/Rage/Skins/VehicleFlagNeutral")));
	for (int32 i = 0; Flag && i < Flag->GetNumMaterials(); ++i)
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Flag->GetMaterial(i)))
			M->SetTextureParameterValue(TEXT("Texture"), Emblem);
}

AController* DF::PlacerCredit(AController* Placer, uint8 PlacedTeam) {
	const ADFPlayerState* PS = Placer ? Placer->GetPlayerState<ADFPlayerState>() : nullptr;
	return Placer && (PlacedTeam == DF::NoTeam || (PS && PS->GetTeam() == PlacedTeam)) ? Placer : nullptr;
}
