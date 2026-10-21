#include "Core/MFContent.h"
#include "Components/AudioComponent.h"
#include "Core/MFUserSettings.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
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

namespace
{
	UParticleSystem* LoadEffect(const UObject* WorldContext, const TCHAR* Name)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return nullptr;
		}
		return MF::LoadClassic<UParticleSystem>(FString(TEXT("Effects/P_")) + Name);
	}
}

UParticleSystemComponent* MF::SpawnEffect(const UObject* WorldContext, const TCHAR* Name, const FVector& Location, float Scale, const FRotator& Rotation)
{
	UParticleSystem* PS = LoadEffect(WorldContext, Name);
	return PS ? UGameplayStatics::SpawnEmitterAtLocation(WorldContext, PS, Location, Rotation, FVector(Scale), true, EPSCPoolMethod::AutoRelease) : nullptr;
}

UParticleSystemComponent* MF::SpawnEffectAttached(const TCHAR* Name, USceneComponent* AttachTo, float Scale)
{
	UParticleSystem* PS = AttachTo ? LoadEffect(AttachTo, Name) : nullptr;
	UParticleSystemComponent* C = PS ? UGameplayStatics::SpawnEmitterAttached(PS, AttachTo, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator, FVector(Scale),
		EAttachLocation::KeepRelativeOffset, true, EPSCPoolMethod::AutoRelease) : nullptr;
	if (C)
	{
		// MF moves the effect to its owner (SetLocation), it doesn't turn with a spinning chunk
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetWorldRotation(FRotator::ZeroRotator);
		C->SetWorldScale3D(FVector(Scale));
	}
	return C;
}

namespace
{
	/** Playing music and the volume it was asked for (before the music setting). */
	TMap<TWeakObjectPtr<UAudioComponent>, float> GMusic;
}

UAudioComponent* MF::PlayMusic(const UObject* WorldContext, USoundBase* Music, float Volume)
{
	const UMFUserSettings* S = UMFUserSettings::Get();
	UAudioComponent* C = Music ? UGameplayStatics::SpawnSound2D(WorldContext, Music, Volume * (S ? S->MusicVolume : 1.f)) : nullptr;
	if (C)
	{
		for (auto It = GMusic.CreateIterator(); It; ++It)
		{
			if (!It->Key.IsValid())
			{
				It.RemoveCurrent();
			}
		}
		GMusic.Add(C, Volume);
	}
	return C;
}

void MF::UpdateMusicVolume()
{
	const UMFUserSettings* S = UMFUserSettings::Get();
	for (const auto& Pair : GMusic)
	{
		if (UAudioComponent* C = Pair.Key.Get())
		{
			C->SetVolumeMultiplier(Pair.Value * (S ? S->MusicVolume : 1.f));
		}
	}
}
