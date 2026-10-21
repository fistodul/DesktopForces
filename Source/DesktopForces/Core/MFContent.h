#pragma once

#include "CoreMinimal.h"

/**
 * Content converted from the player's own Mobile Forces install (Tools/mfport) lives in the
 * MobileForcesClassic plugin (Plugins/MobileForcesClassic). Paths given here are relative to
 * it, e.g. "Sounds/MiscSFX/Explode". The plugin may be missing (Desktop Forces built without
 * the original game's content): loads return null and callers cope (no sound, simple HUD,
 * placeholder meshes).
 */
namespace MF
{
	/** Mount point of the MobileForcesClassic plugin. */
	inline const TCHAR* ClassicRoot = TEXT("/MobileForcesClassic");

	/** "Sounds/MiscSFX/Explode" -> "/MobileForcesClassic/Sounds/MiscSFX/Explode.Explode" */
	DESKTOPFORCES_API FString ClassicPath(const FString& Rel);

	DESKTOPFORCES_API UObject* LoadClassicObject(UClass* Class, const FString& Rel);

	template <typename T>
	T* LoadClassic(const FString& Rel)
	{
		return Cast<T>(LoadClassicObject(T::StaticClass(), Rel));
	}

	/**
	 * MF PlaySound(Sound, Slot, Volume, , Radius): a one-shot at Location heard out to RadiusUU
	 * (UE1 units: 1600 = the default, 100 = right next to it), on this machine only (call it
	 * where the event is seen: clients, listen servers; a dedicated server skips it).
	 */
	DESKTOPFORCES_API class USoundAttenuation* GetAttenuation(float RadiusUU);
	DESKTOPFORCES_API void PlaySoundRadius(const UObject* WorldContext, class USoundBase* Sound, const FVector& Location, float Volume = 1.f, float RadiusUU = 1600.f, float Pitch = 1.f);
}
