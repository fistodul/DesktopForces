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
}
