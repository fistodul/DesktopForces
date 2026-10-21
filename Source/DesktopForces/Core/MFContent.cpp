#include "Core/MFContent.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

FString MF::ClassicPath(const FString& Rel)
{
	return FString::Printf(TEXT("%s/%s.%s"), ClassicRoot, *Rel, *FPaths::GetBaseFilename(Rel));
}

UObject* MF::LoadClassicObject(UClass* Class, const FString& Rel)
{
	return StaticLoadObject(Class, nullptr, *ClassicPath(Rel), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

USoundAttenuation* MF::GetAttenuation(float RadiusUU)
{
	// one shared attenuation per radius: full volume in the inner 6 %, linear to nothing at the
	// radius (rooted, weak pointers: nothing to tear down at exit after the object system is gone)
	static TMap<int32, TWeakObjectPtr<USoundAttenuation>> Cache;
	const int32 Key = FMath::Max(1, FMath::RoundToInt(RadiusUU));
	TWeakObjectPtr<USoundAttenuation>& A = Cache.FindOrAdd(Key);
	if (!A.IsValid())
	{
		A = NewObject<USoundAttenuation>(GetTransientPackage());
		A->AddToRoot();
		const float Radius = Key * 2.25f;
		A->Attenuation.bAttenuate = true;
		A->Attenuation.bSpatialize = true;
		A->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
		A->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		A->Attenuation.AttenuationShapeExtents = FVector(Radius * 0.06f, 0.f, 0.f);
		A->Attenuation.FalloffDistance = Radius * 0.94f;
	}
	return A.Get();
}

void MF::PlaySoundRadius(const UObject* WorldContext, USoundBase* Sound, const FVector& Location, float Volume, float RadiusUU, float Pitch)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!Sound || !World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, FRotator::ZeroRotator, Volume, Pitch, 0.f, GetAttenuation(RadiusUU));
}
