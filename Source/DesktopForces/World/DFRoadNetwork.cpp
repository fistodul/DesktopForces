#include "World/DFRoadNetwork.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ADFRoadNetwork::ADFRoadNetwork() {
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
}

void ADFRoadNetwork::PostLoad() {
	Super::PostLoad();
	BuildAdjacency();
}

void ADFRoadNetwork::BuildAdjacency() {
	Next.Reset();
	Next.SetNum(Nodes.Num());
	for (const FIntPoint& L : Links)
		if (Nodes.IsValidIndex(L.X) && Nodes.IsValidIndex(L.Y))
			Next[L.X].AddUnique(L.Y);
}

ADFRoadNetwork* ADFRoadNetwork::Get(const UWorld* World) {
	TActorIterator<ADFRoadNetwork> It(World);
	return It ? *It : nullptr;
}

int32 ADFRoadNetwork::FindNearestNode(const FVector& Point, float MaxDist, const AActor* Ignore) const {
	// nearest first, then the first one in sight (UE1 FindPathToward started from a visible node)
	TArray<TPair<float, int32>> Near;
	for (int32 I = 0; I < Nodes.Num(); ++I) {
		const float D = FVector::DistSquared(Nodes[I], Point);
		if (D < MaxDist * MaxDist)
			Near.Emplace(D, I);
	}
	Near.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DFRoadNode), false, Ignore);
	for (int32 K = 0; K < FMath::Min(Near.Num(), 8); ++K)
		if (!GetWorld()->LineTraceTestByChannel(Point + FVector(0.f, 0.f, 60.f), Nodes[Near[K].Value] + FVector(0.f, 0.f, 60.f), ECC_WorldStatic, Params))
			return Near[K].Value;
	return Near.Num() ? Near[0].Value : INDEX_NONE;
}

bool ADFRoadNetwork::FindRoute(int32 Start, int32 Goal, TArray<int32>& OutRoute, TConstArrayView<FVector> Avoid) const {
	OutRoute.Reset();
	if (!Nodes.IsValidIndex(Start) || !Nodes.IsValidIndex(Goal))
		return false;
	if (Next.Num() != Nodes.Num())
		const_cast<ADFRoadNetwork*>(this)->BuildAdjacency(); // spawned at runtime / in the editor
	TArray<float> Extra;
	Extra.Init(0.f, Nodes.Num());
	for (int32 I = 0; I < Nodes.Num(); ++I) {
		for (const FVector& P : Avoid)
			if (I != Start && I != Goal && FVector::DistSquared(Nodes[I], P) < FMath::Square(400.f))
				Extra[I] = 10000.f;
	}
	TArray<float> Cost;
	TArray<int32> From;
	Cost.Init(FLT_MAX, Nodes.Num());
	From.Init(INDEX_NONE, Nodes.Num());
	TArray<TPair<float, int32>> Open; // (cost + distance left, node), a binary heap
	auto ByEstimate = [](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; };
	Cost[Start] = 0.f;
	Open.HeapPush(TPair<float, int32>(FVector::Dist(Nodes[Start], Nodes[Goal]), Start), ByEstimate);
	while (Open.Num()) {
		TPair<float, int32> Top;
		Open.HeapPop(Top, ByEstimate);
		const int32 N = Top.Value;
		if (N == Goal) {
			for (int32 At = Goal; At != INDEX_NONE; At = From[At])
				OutRoute.Insert(At, 0);
			return true;
		}
		for (const int32 M : Next[N]) {
			const float C = Cost[N] + FVector::Dist(Nodes[N], Nodes[M]) + Extra[M];
			if (C < Cost[M]) {
				Cost[M] = C;
				From[M] = N;
				Open.HeapPush(TPair<float, int32>(C + FVector::Dist(Nodes[M], Nodes[Goal]), M), ByEstimate);
			}
		}
	}
	return false;
}
