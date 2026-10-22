// Per-asset data the importer leaves on a mesh.
#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "DFMeshScaleData.generated.h"

/** What a skeletal mesh's non-uniform MF MESHMAP SCALE leaves for its component (a skeleton
 *  can only be scaled evenly): the first-person weapons are squashed to half depth. */
UCLASS()
class DESKTOPFORCES_API UDFMeshScaleData : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "DF")
	FVector ComponentScale = FVector::OneVector;
};
