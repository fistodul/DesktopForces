#pragma once

#include "CoreMinimal.h"

/**
 * Content converted from the player's own Mobile Forces install (Tools/mfport) lives in the
 * MobileForcesClassic plugin (Plugins/MobileForcesClassic). Paths given here are relative to
 * it, e.g. "Sounds/MiscSFX/Explode". The plugin may be missing (Desktop Forces built without
 * the original game's content): loads return null and callers cope (no sound, simple HUD,
 * placeholder meshes).
 */
namespace DF
{
	/** Mount point of the MobileForcesClassic plugin. */
	inline const TCHAR* ClassicRoot = TEXT("/MobileForcesClassic");
	/** Community content not vouched for yet (the servers' weapon mods ...): its own plugin. */
	inline const TCHAR* UnverifiedRoot = TEXT("/MobileForcesUnverified");
	/** Where converted content may be, searched in this order: the game's own community
	 *  content, the classic plugin, the unverified plugin (packs.toml decides which). */
	inline const TCHAR* ContentRoots[] = { TEXT("/Game/MF/Community"), ClassicRoot, UnverifiedRoot };

	/** "Sounds/MiscSFX/Explode" -> "/MobileForcesClassic/Sounds/MiscSFX/Explode.Explode" */
	DESKTOPFORCES_API FString ClassicPath(const FString& Rel);

	/** Rel ("Sounds/AK74/AK74Fire") from the first content root that has it. */
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

	/**
	 * MF sprite effects (RageEffects.Explosion, VehicleExplosion, ExplosionFire, SmokePuffVeh,
	 * WallHit, Blood: Effects/P_<Name>, made by the importer from mflib/effects.py) at Location,
	 * Scale = the MF actor's DrawScale (RageEffect.AddExplosion: Size x 0.4). Like the sounds,
	 * only where it is seen: null on a dedicated server or without the classic content.
	 */
	/** Music (map songs, the menu theme): Volume x UDFUserSettings::MusicVolume, kept in step
	 *  when the setting changes (UpdateMusicVolume). */
	DESKTOPFORCES_API class UAudioComponent* PlayMusic(const UObject* WorldContext, class USoundBase* Music, float Volume);
	DESKTOPFORCES_API void UpdateMusicVolume();

	DESKTOPFORCES_API class UParticleSystemComponent* SpawnEffect(const UObject* WorldContext, const TCHAR* Name, const FVector& Location,
		float Scale = 1.f, const FRotator& Rotation = FRotator::ZeroRotator);
	/** The same, following a component (ExplosionFire on burning debris: SetLocation(Owner.Location)). */
	DESKTOPFORCES_API class UParticleSystemComponent* SpawnEffectAttached(const TCHAR* Name, class USceneComponent* AttachTo, float Scale = 1.f);
}
