#include "Core/DFProps.h"
#include "Core/DFContent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/Paths.h"

bool DF::ApplyPropMesh(UStaticMeshComponent* C, const TCHAR* PackageAndName, float DrawScale, const FVector& Offset)
{
	if (!C)
	{
		return false;
	}
	// community props (Desktop Forces' own) first, then the Mobile Forces Classic pack
	const FString Name = FPaths::GetBaseFilename(PackageAndName);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/MF/Community/Props/%s.%s"), PackageAndName, *Name),
		nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Mesh)
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s/Props/%s.%s"), DF::ClassicRoot, PackageAndName, *Name),
			nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
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
