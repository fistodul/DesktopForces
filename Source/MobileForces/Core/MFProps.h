#pragma once

#include "CoreMinimal.h"

class UStaticMeshComponent;

namespace MF
{
	/**
	 * Put a converted game mesh (`mfport.py props` -> /Game/MFImported/Props/<Package>/<Name>,
	 * e.g. "RageGame/RageSafeMeshBlue") on a component: at Offset from the actor with the MF
	 * DrawScale and the mesh's own materials. Returns false and leaves the component alone
	 * (the placeholder shape stays) when this install has no converted props.
	 */
	MOBILEFORCES_API bool ApplyPropMesh(UStaticMeshComponent* C, const TCHAR* PackageAndName, float DrawScale = 1.f, const FVector& Offset = FVector::ZeroVector);
}
