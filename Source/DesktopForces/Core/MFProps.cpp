#include "Core/MFProps.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/Paths.h"

bool MF::ApplyPropMesh(UStaticMeshComponent* C, const TCHAR* PackageAndName, float DrawScale, const FVector& Offset)
{
	if (!C)
	{
		return false;
	}
	const FString Name = FPaths::GetBaseFilename(PackageAndName);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/MobileForcesClassic/Props/%s.%s"), PackageAndName, *Name),
		nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Mesh)
	{
		return false;
	}
	if (C->GetStaticMesh() != Mesh)
	{
		C->SetStaticMesh(Mesh);
		C->EmptyOverrideMaterials();
	}
	C->SetRelativeLocation(Offset);
	C->SetRelativeRotation(FRotator::ZeroRotator);
	C->SetRelativeScale3D(FVector(DrawScale));
	return true;
}
