#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MFImportCommandlet.generated.h"

/**
 * Imports the output of Tools/mfport into the project.
 *
 *   UnrealEditor-Cmd MobileForces.uproject -run=MFImport -work=<Tools/mfport/work/out>
 *       [-steps=textures,sounds,maps] [-maps=mf-warehouse,mf-polar]
 *
 * Tools/mfport/mfport.py import runs this for you.
 */
UCLASS()
class UMFImportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMFImportCommandlet();
	virtual int32 Main(const FString& Params) override;
};
