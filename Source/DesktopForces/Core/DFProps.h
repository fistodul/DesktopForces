#pragma once

#include "CoreMinimal.h"

class UStaticMeshComponent;

namespace DF {
	/** What a beam or a line stops at: everything solid, not zones (ADFZoneVolume). */
	DESKTOPFORCES_API struct FCollisionObjectQueryParams SolidObjects();
	/** HurtRadius: 1 - (dist - victim's radius) / radius for a radial event, 1 for anything else.
	 *  A TakeDamage override that doesn't use AActor's own must apply it. */
	DESKTOPFORCES_API float RadialDamageScale(const struct FDamageEvent& Event, const FVector& VictimAt, float VictimRadius);
	/** The same for a box (a vehicle's hull): the distance is to its surface, so a blast against
	 *  any side does all of it, as against a soldier. */
	DESKTOPFORCES_API float RadialDamageScale(const struct FDamageEvent& Event, const FTransform& Box, const FVector& Extent);
	/** MF GetDamageInstigator: who gets the kills of a device left behind (sentry gun, mines,
	 *  lasers) - whoever placed it, alive or dead by now, while still on the team it was placed
	 *  for. Keep the controller from the placing: a dead pawn has none. */
	DESKTOPFORCES_API class AController* PlacerCredit(class AController* Placer, uint8 PlacedTeam);
	/** Light for a mesh that moves, so has no lightmap (ApplyPropMesh does this itself). */
	DESKTOPFORCES_API void SetMovingLight(class UPrimitiveComponent* C);
	/**
	 * Put a converted game mesh (`mfport.py props` -> /MobileForcesClassic/Props/<Package>/<Name>,
	 * e.g. "RageGame/RageSafeMeshBlue") on a component: at Offset from the actor with the MF
	 * DrawScale and the mesh's own materials. Returns false and leaves the component alone
	 * (the placeholder shape stays) when this install has no converted props.
	 */
	DESKTOPFORCES_API bool ApplyPropMesh(UStaticMeshComponent* C, const TCHAR* PackageAndName, float DrawScale = 1.f, const FVector& Offset = FVector::ZeroVector);
	/** A stand-in shape in one colour (a team's, mostly): the unlit tint material and its colour.
	 *  Nothing on a dedicated server. */
	DESKTOPFORCES_API void Tint(UStaticMeshComponent* C, const FLinearColor& Color);
	/** A thin beam from Start to End in Color: a unit cube component (/Engine/BasicShapes/Cube)
	 *  stretched between them (MF's Blue/RedBeamRifle: trip bomb lasers, the sniper's charge). */
	DESKTOPFORCES_API void PlaceBeam(UStaticMeshComponent* Beam, const FVector& Start, const FVector& End, const FLinearColor& Color);
	/** A dynamic instance of Master (a material's package path) with its "Texture" parameter set
	 *  to a texture of whichever pack has it ("Textures/RageGame/BlueWall/BlueWall_A01"); null if
	 *  either is missing (no classic pack). */
	DESKTOPFORCES_API class UMaterialInstanceDynamic* MakeTexturedMID(const TCHAR* Master, const FString& Texture, UObject* Outer);
	/**
	 * MF's waving team flag, a skeletal mesh of the classic pack ("CTFFlagMesh": cloth on a
	 * staff, RageGame.RageBlueFlag; "VehicleFlagMesh": a pennant, Rage.VehicleFlag), attached to
	 * Parent and flapping. Its cloth (the material slot "Skin<N>") takes SetFlagEmblem. Null
	 * without the pack and on a dedicated server: the caller makes do with shapes.
	 */
	DESKTOPFORCES_API class USkeletalMeshComponent* MakeFlag(class USceneComponent* Parent, const TCHAR* Mesh, FName ClothSlot);
	/** The flag without the classic pack: a flat cloth of Width x Height cm, upright along the
	 *  parent's X, its tied edge at At. Takes SetFlagEmblem too. */
	DESKTOPFORCES_API UStaticMeshComponent* MakeFlatFlag(class USceneComponent* Parent, const FVector& At, float Width, float Height);
	/** Play one of the flag mesh's sequences (FlagUpFlap, FlagDownFlap, FlagUp, FlagDown); its length. */
	DESKTOPFORCES_API float PlayFlag(class USkeletalMeshComponent* Flag, const TCHAR* Sequence, bool bLoop);
	/** The team's emblem on a flag's cloth (materials made from M_MF_VehicleFlag). */
	DESKTOPFORCES_API void SetFlagEmblem(class UMeshComponent* Flag, uint8 Team);
} // namespace DF
