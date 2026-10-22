#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DFRoadNetwork.generated.h"

/**
 * The map's road graph for driving bots: MF's ReachSpecs flagged R_ROAD (RoadPoints, car
 * parks, vehicle generators), placed by the level builder. RageBot drove from path node to
 * path node along these; RoadPoint.MaxSpeed (0..255) capped the speed on the way.
 */
UCLASS(NotBlueprintable)
class DESKTOPFORCES_API ADFRoadNetwork : public AActor
{
	GENERATED_BODY()

public:
	ADFRoadNetwork();

	UPROPERTY(VisibleAnywhere, Category = "DF")
	TArray<FVector> Nodes;

	/** RoadPoint.MaxSpeed per node (255 = flat out). */
	UPROPERTY(VisibleAnywhere, Category = "DF")
	TArray<uint8> MaxSpeed;

	/** One-way links (a reach spec each way for a two-way road): X -> Y. */
	UPROPERTY(VisibleAnywhere, Category = "DF")
	TArray<FIntPoint> Links;

	/** The map's network, if it has roads. */
	static ADFRoadNetwork* Get(const UWorld* World);

	/** Road node nearest to Point that can be seen from it (within MaxDist), or INDEX_NONE. */
	int32 FindNearestNode(const FVector& Point, float MaxDist, const AActor* Ignore = nullptr) const;

	/** Shortest road route between two nodes (A*), as node indices from Start to Goal. Nodes
	 *  near any Avoid point (parked vehicles on generator spots, wrecks) cost a long detour. */
	bool FindRoute(int32 Start, int32 Goal, TArray<int32>& OutRoute, TConstArrayView<FVector> Avoid = {}) const;

protected:
	virtual void PostLoad() override;
	void BuildAdjacency();
	TArray<TArray<int32>> Next;
};
