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
	// community props (Desktop Forces' own) first, then the classic and unverified packs
	UStaticMesh* Mesh = DF::LoadClassic<UStaticMesh>(FString(TEXT("Props/")) + PackageAndName);
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
