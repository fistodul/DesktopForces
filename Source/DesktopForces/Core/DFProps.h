#pragma once

#include "CoreMinimal.h"

class UStaticMeshComponent;

namespace DF {
	/**
	 * Put a converted game mesh (`mfport.py props` -> /MobileForcesClassic/Props/<Package>/<Name>,
	 * e.g. "RageGame/RageSafeMeshBlue") on a component: at Offset from the actor with the MF
	 * DrawScale and the mesh's own materials. Returns false and leaves the component alone
	 * (the placeholder shape stays) when this install has no converted props.
	 */
	/** HurtRadius: 1 - (dist - victim's radius) / radius for a radial event, 1 for anything else.
	 *  A TakeDamage override that doesn't use AActor's own must apply it. */
	DESKTOPFORCES_API float RadialDamageScale(const struct FDamageEvent& Event, const FVector& VictimAt, float VictimRadius);
	/** Light for a mesh that moves, so has no lightmap (ApplyPropMesh does this itself). */
	DESKTOPFORCES_API void SetMovingLight(class UPrimitiveComponent* C);
	DESKTOPFORCES_API bool ApplyPropMesh(UStaticMeshComponent* C, const TCHAR* PackageAndName, float DrawScale = 1.f, const FVector& Offset = FVector::ZeroVector);
} // namespace DF
