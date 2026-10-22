#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DFImportCommandlet.generated.h"

/**
 * Imports the output of Tools/mfport into the project.
 *
 *   UnrealEditor-Cmd DesktopForces.uproject -run=DFImport -work=<Tools/mfport/work/out>
 *       [-steps=textures,sounds,maps] [-maps=mf-warehouse,mf-polar]
 *
 * Tools/mfport/mfport.py import runs this for you.
 */
UCLASS()
class UDFImportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDFImportCommandlet();
	virtual int32 Main(const FString& Params) override;
};
