#include "Core/DFProps.h"
#include "CollisionQueryParams.h"
#include "Engine/DamageEvents.h"
#include "Core/DFUserSettings.h"
#include "Core/DFGameSettings.h"
#include "Core/DFContent.h"
#include "Components/StaticMeshComponent.h"
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

void DF::SetMovingLight(UPrimitiveComponent* C) {
	// nothing baked reaches a thing that moves: UE1 lit actors from where they stood, this is
	// a flat stand-in the same on every map. One add in the material, no lightmap, no bake
	C->SetCustomPrimitiveDataFloat(0, UDFGameSettings::Get().MovingActorLight * UDFUserSettings::Get().Brightness);
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
