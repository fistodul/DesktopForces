#include "Core/DFContent.h"
#include "Components/AudioComponent.h"
#include "Core/DFUserSettings.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundClass.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"

UObject* DF::LoadClassicObject(UClass* Class, const FString& Rel) {
	// which root has it is worked out once: asking for a package that isn't there costs a
	// synchronous load attempt (FlushAsyncLoading, a hitch) every time - and classic assets were
	// first asked for under the community root on every footstep and every effect
	static TMap<FString, FString> Resolved; // Rel -> object path; empty: in no root
	if (const FString* Path = Resolved.Find(Rel))
		return Path->IsEmpty() ? nullptr : StaticLoadObject(Class, nullptr, **Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	TArray<FString, TInlineAllocator<UE_ARRAY_COUNT(ContentRoots)>> Packages;
	if (Rel.StartsWith(TEXT("/")))
		Packages.Add(Rel); // a package path
	else
		for (const TCHAR* Root : ContentRoots)
			Packages.Add(FString(Root) / Rel);
	const FString Name = FPaths::GetBaseFilename(Rel);
	for (const FString& Package : Packages) {
		if (!FPackageName::DoesPackageExist(Package))
			continue;
		const FString Path = Package + TEXT(".") + Name;
		if (UObject* O = StaticLoadObject(Class, nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)) {
			Resolved.Add(Rel, Path);
			return O;
		}
	}
	Resolved.Add(Rel, FString());
	return nullptr;
}

USoundAttenuation* DF::GetAttenuation(float RadiusUU) {
	// one shared attenuation per radius: full volume in the inner 6 %, linear to nothing at the
	// radius (rooted, weak pointers: nothing to tear down at exit after the object system is gone)
	static TMap<int32, TWeakObjectPtr<USoundAttenuation>> Cache;
	const int32 Key = FMath::Max(1, FMath::RoundToInt(RadiusUU));
	TWeakObjectPtr<USoundAttenuation>& A = Cache.FindOrAdd(Key);
	if (!A.IsValid()) {
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

namespace {
	USoundClass* SoundCategory(bool bVoice) {
		static TWeakObjectPtr<USoundClass> Effects, Voice;
		TWeakObjectPtr<USoundClass>& Category = bVoice ? Voice : Effects;
		if (!Category.IsValid()) {
			Category = NewObject<USoundClass>(GetTransientPackage());
			Category->AddToRoot();
			if (GEngine && GEngine->GetAudioDeviceManager())
				GEngine->GetAudioDeviceManager()->RegisterSoundClass(Category.Get());
		}
		return Category.Get();
	}
} // namespace

void DF::UpdateSoundVolumes() {
	const UDFUserSettings& Settings = UDFUserSettings::Get();
	SoundCategory(false)->Properties.Volume = Settings.EffectsVolume;
	SoundCategory(true)->Properties.Volume = Settings.AnnouncerVolume;
}

USoundBase* DF::EffectsSound(USoundBase* Sound) {
	if (Sound)
		Sound->SoundClassObject = SoundCategory(false);
	return Sound;
}

void DF::PlaySoundAtLocation(const UObject* WorldContext, USoundBase* Sound, const FVector& Location, float Volume, float Pitch, float StartTime, USoundAttenuation* Attenuation) {
	UGameplayStatics::PlaySoundAtLocation(WorldContext, EffectsSound(Sound), Location, Volume, Pitch, StartTime, Attenuation);
}

void DF::PlayUISound(const UObject* WorldContext, USoundBase* Sound) {
	if (Sound) {
		Sound->SoundClassObject = SoundCategory(false);
		UGameplayStatics::PlaySound2D(WorldContext, Sound);
	}
}

UAudioComponent* DF::PlayAnnouncer(const UObject* WorldContext, USoundBase* Sound) {
	if (!Sound)
		return nullptr;
	Sound->SoundClassObject = SoundCategory(true);
	return UGameplayStatics::SpawnSound2D(WorldContext, Sound);
}

void DF::PlaySoundRadius(const UObject* WorldContext, USoundBase* Sound, const FVector& Location, float Volume, float RadiusUU, float Pitch) {
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!Sound || !World || World->GetNetMode() == NM_DedicatedServer)
		return;
	Sound->SoundClassObject = SoundCategory(false);
	UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, FRotator::ZeroRotator, Volume, Pitch, 0.f, GetAttenuation(RadiusUU));
}

TArray<FString> DF::FindMaps() {
	FARFilter Filter;
	Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
	TArray<FString> Roots;
	for (const FString& Root : { FString(TEXT("/Game")), FString(TEXT("/Game/MF")), FString(ClassicRoot), FString(UnverifiedRoot) }) {
		const FString Maps = Root / TEXT("Maps");
		if (FPackageName::IsValidPath(Maps)) // a pack not installed isn't mounted
		{
			Roots.Add(Maps);
			Filter.PackagePaths.Add(FName(Maps));
		}
	}
	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
#if WITH_EDITOR
	Registry.ScanPathsSynchronous(Roots, false); // the editor scans in the background
#endif
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
	TArray<FString> Out;
	for (const FAssetData& A : Assets) {
		// a packaged game's registry lists every pack it was cooked with; a pack whose pak
		// was removed is left out here (a lookup in the mounted pak index, no disk scan)
		const FString Package = A.PackageName.ToString();
		if (FPackageName::DoesPackageExist(Package))
			Out.Add(Package);
	}
	return Out;
}

USoundBase* DF::LoadSound(const FString& MFName) {
	FString Package, Name;
	return MFName.Split(TEXT("."), &Package, &Name) ? LoadClassic<USoundBase>(FString::Printf(TEXT("Sounds/%s/%s"), *Package, *Name)) : nullptr;
}

UAudioComponent* DF::PlayLoop(USceneComponent* Component, const FString& MFName, float RadiusUU) {
	if (!Component || Component->GetNetMode() == NM_DedicatedServer)
		return nullptr;
	USoundBase* Sound = LoadSound(MFName);
	if (Sound)
		Sound->SoundClassObject = SoundCategory(false);
	if (USoundWave* Wave = Cast<USoundWave>(Sound))
		Wave->bLooping = true; // an ambient sound loops (the same wave as a one-shot is never wanted)
	return Sound ? UGameplayStatics::SpawnSoundAttached(Sound, Component, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
					   true, 1.f, 1.f, 0.f, GetAttenuation(RadiusUU))
				 : nullptr;
}

namespace {
	UParticleSystem* LoadEffect(const UObject* WorldContext, const TCHAR* Name) {
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!World || World->GetNetMode() == NM_DedicatedServer)
			return nullptr;
		return DF::LoadClassic<UParticleSystem>(FString(TEXT("Effects/P_")) + Name);
	}
} // namespace

UParticleSystemComponent* DF::SpawnEffect(const UObject* WorldContext, const TCHAR* Name, const FVector& Location, float Scale, const FRotator& Rotation) {
	UParticleSystem* PS = LoadEffect(WorldContext, Name);
	return PS ? UGameplayStatics::SpawnEmitterAtLocation(WorldContext, PS, Location, Rotation, FVector(Scale), true, EPSCPoolMethod::AutoRelease) : nullptr;
}

UParticleSystemComponent* DF::SpawnEffectAttached(const TCHAR* Name, USceneComponent* AttachTo, float Scale, bool bFollowRotation) {
	UParticleSystem* PS = AttachTo ? LoadEffect(AttachTo, Name) : nullptr;
	UParticleSystemComponent* C = PS ? UGameplayStatics::SpawnEmitterAttached(PS, AttachTo, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator, FVector(Scale),
										   EAttachLocation::KeepRelativeOffset, true, EPSCPoolMethod::AutoRelease)
									 : nullptr;
	if (C) {
		// MF moves the effect to its owner (SetLocation), it doesn't turn with a spinning chunk
		C->SetUsingAbsoluteRotation(!bFollowRotation);
		C->SetUsingAbsoluteScale(true);
		if (!bFollowRotation)
			C->SetWorldRotation(FRotator::ZeroRotator);
		C->SetWorldScale3D(FVector(Scale));
	}
	return C;
}

namespace {
	/** Playing music and the volume it was asked for (before the music setting). */
	TMap<TWeakObjectPtr<UAudioComponent>, float> GMusic;
} // namespace

UAudioComponent* DF::PlayMusic(const UObject* WorldContext, USoundBase* Music, float Volume) {
	UAudioComponent* C = Music ? UGameplayStatics::SpawnSound2D(WorldContext, Music, Volume * UDFUserSettings::Get().MusicVolume) : nullptr;
	if (C) {
		for (auto It = GMusic.CreateIterator(); It; ++It)
			if (!It->Key.IsValid())
				It.RemoveCurrent();
		GMusic.Add(C, Volume);
	}
	return C;
}

void DF::UpdateMusicVolume() {
	const float MusicVolume = UDFUserSettings::Get().MusicVolume;
	for (const auto& Pair : GMusic)
		if (UAudioComponent* C = Pair.Key.Get())
			C->SetVolumeMultiplier(Pair.Value * MusicVolume);
}

void DF::StopEffect(UParticleSystemComponent* Effect) {
	if (Effect) {
		Effect->DeactivateSystem();
		Effect->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
}
