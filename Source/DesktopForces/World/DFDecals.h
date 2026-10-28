#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DFDecals.generated.h"

class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * Marks left on the world: bullet holes (MF RagePock, 10 s) and scorch marks after explosions
 * (MF had them switched off; a graphics upgrade here). Art: Tools/artgen (T_MF_BulletHole*,
 * T_MF_Scorch*), materials M_MF_Decal / M_MF_DecalQuad (importer EnsureDecalMaterials).
 * UDFUserSettings::DecalQuality: 2 full (surface-conforming scorch meshes), 1 simple
 * (flat instanced quads), 0 none. Bullet holes stay quads. Local only, static surfaces only.
 * Full works with the forward renderer and unlit/emissive world materials without DBuffer.
 */
namespace DF {
	enum class EDecal : uint8 { BulletHole,
		Scorch };
	/** Size: across, cm. Normal: the surface's, pointing out. */
	DESKTOPFORCES_API void SpawnDecal(const UObject* WorldContext, EDecal Kind, const FVector& At, const FVector& Normal, float Size);
	/** A scorch mark on whatever is below Location within Range (explosions in the air leave none). */
	DESKTOPFORCES_API void SpawnScorchBelow(const UObject* WorldContext, const FVector& Location, float Size, float Range = 300.f);
} // namespace DF

/** The quad marks of a world: one instanced mesh per texture, a ring of slots reused oldest first. */
UCLASS(NotPlaceable, Transient)
class DESKTOPFORCES_API ADFDecalQuads : public AActor {
	GENERATED_BODY()

public:
	ADFDecalQuads();
	static ADFDecalQuads* Get(UWorld* World);
	void Add(int32 Variant, DF::EDecal Kind, const FTransform& Where);

	/** Quads per texture before the oldest is reused; seconds a mark stays. */
	int32 MaxPerVariant = 32;
	float Lifetime = 30.f;

protected:
	virtual void BeginPlay() override;
	void Expire();

	struct FRing {
		TObjectPtr<UInstancedStaticMeshComponent> Mesh;
		TArray<float> Born;
		int32 Next = 0;
	};
	TMap<FString, FRing> Rings;
	FTimerHandle ExpireTimer;
};

/** A small, collision-free scorch mesh projected onto static surfaces once on impact. */
UCLASS(NotPlaceable, Transient)
class DESKTOPFORCES_API ADFProjectedScorch : public AActor {
	GENERATED_BODY()
public:
	ADFProjectedScorch();
	bool Project(const FVector& Normal, float Size, UMaterialInterface* Material);

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Mesh;
};
