#include "World/DFPitFinder.h"

#include "Algo/AnyOf.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "NavLinkHostInterface.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "Player/DFCharacter.h"
#include "World/DFMover.h"
#include "World/DFZoneVolume.h"

namespace DF {
	namespace {
		float PolyArea(const TArray<FVector>& V) {
			float A = 0.f;
			for (int32 i = 0; i < V.Num(); ++i)
				A += V[i].X * V[(i + 1) % V.Num()].Y - V[(i + 1) % V.Num()].X * V[i].Y;
			return FMath::Abs(A) * 0.5f;
		}
	} // namespace

	TArray<FPit> FindPits(UWorld* World) {
		TArray<FPit> Pits;
		const UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		const ARecastNavMesh* Mesh = NavSys ? Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance()) : nullptr;
		if (!Mesh)
			return Pits;
		// what a soldier can climb: a jump's height plus a step onto the ledge
		const ADFCharacter* Soldier = GetDefault<ADFCharacter>();
		const UCharacterMovementComponent* Move = Soldier->GetCharacterMovement();
		const float Gravity = FMath::Max(FMath::Abs(World->GetGravityZ()), 1.f);
		const float Reach = FMath::Square(Move->JumpZVelocity) / (2.f * Gravity) + Move->MaxStepHeight;
		const float Radius = Soldier->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
		const FVector LandExtent(Radius + 30.f, Radius + 30.f, 120.f);
		const AWorldSettings* WS = World->GetWorldSettings();
		const float Bottom = WS && WS->KillZ > -1.0e6f ? WS->KillZ : -1.0e6f;

		// navmesh polys, walk-connected into islands
		TArray<NavNodeRef> Refs;
		TArray<TArray<FVector>> Verts;
		TMap<NavNodeRef, int32> Index;
		TArray<int32> TileOf;
		TArray<FNavTileRef> Tiles;
		Mesh->GetAllNavMeshTiles(Tiles);
		for (int32 t = 0; t < Tiles.Num(); ++t) {
			TArray<FNavPoly> Polys;
			if (!Mesh->GetPolysInTile(Tiles[t], Polys))
				continue;
			for (const FNavPoly& P : Polys) {
				TileOf.Add(t);
				Index.Add(P.Ref, Refs.Num());
				Refs.Add(P.Ref);
				Mesh->GetPolyVerts(P.Ref, Verts.AddDefaulted_GetRef());
			}
		}
		const int32 N = Refs.Num();
		TArray<int32> Parent;
		Parent.SetNum(N);
		for (int32 i = 0; i < N; ++i)
			Parent[i] = i;
		auto Root = [&Parent](int32 i) {
			while (Parent[i] != i)
				i = Parent[i] = Parent[Parent[i]];
			return i;
		};
		for (int32 i = 0; i < N; ++i) {
			TArray<NavNodeRef> Next;
			Mesh->GetPolyNeighbors(Refs[i], Next);
			for (const NavNodeRef R : Next)
				if (const int32* j = Index.Find(R))
					Parent[Root(i)] = Root(*j);
		}
		TArray<int32> Island;
		Island.SetNum(N);
		TMap<int32, int32> IslandOf;
		for (int32 i = 0; i < N; ++i) {
			const int32 R = Root(i);
			const int32* Known = IslandOf.Find(R);
			Island[i] = Known ? *Known : IslandOf.Add(R, IslandOf.Num());
		}
		int32 Nodes = IslandOf.Num();
		const int32 Islands = Nodes;
		auto IslandAt = [&](const FVector& At, const FVector& Extent) {
			FNavLocation Loc;
			const int32* i = Mesh->ProjectPoint(At, Loc, Extent) ? Index.Find(Loc.NodeRef) : nullptr;
			return i ? Island[*i] : INDEX_NONE;
		};

		// the moves between islands, by the fall they take (min, max; 0 up a ledge or a link)
		TArray<TMap<int32, FVector2f>> Moves;
		Moves.SetNum(Nodes);
		auto AddMove = [&Moves](int32 From, int32 To, float Fall) {
			if (From == INDEX_NONE || To == INDEX_NONE || From == To)
				return;
			if (FVector2f* M = Moves[From].Find(To))
				*M = FVector2f(FMath::Min(M->X, Fall), FMath::Max(M->Y, Fall));
			else
				Moves[From].Add(To, FVector2f(Fall, Fall));
		};
		FCollisionQueryParams Query(SCENE_QUERY_STAT(DFFindPits), false);
		for (TActorIterator<APawn> It(World); It; ++It)
			Query.AddIgnoredActor(*It);
		// off every navmesh edge: over a jump's height, then down to whatever is below
		for (int32 i = 0; i < N; ++i) {
			const TArray<FVector>& V = Verts[i];
			FVector Centre = FVector::ZeroVector;
			for (const FVector& P : V)
				Centre += P / V.Num();
			for (int32 e = 0; e < V.Num(); ++e) {
				const FVector A = V[e], B = V[(e + 1) % V.Num()];
				const float Len = FVector::Dist2D(A, B);
				if (Len < 10.f)
					continue;
				FVector Out = FVector(B.Y - A.Y, A.X - B.X, 0.f).GetSafeNormal();
				if (FVector::DotProduct(Out, (A + B) * 0.5f - Centre) < 0.f)
					Out = -Out;
				const int32 Samples = FMath::CeilToInt(Len / 150.f);
				for (int32 s = 0; s < Samples; ++s) {
					const FVector P = FMath::Lerp(A, B, (s + 0.5f) / Samples);
					const FVector Top = P + FVector(0.f, 0.f, Reach);
					const FVector Over = Top + Out * (2.f * Radius + 20.f);
					if (World->LineTraceTestByChannel(Top, Over, ECC_Pawn, Query))
						continue; // a wall higher than a jump
					FHitResult Hit;
					if (!World->LineTraceSingleByChannel(Hit, Over, FVector(Over.X, Over.Y, Bottom), ECC_Pawn, Query))
						continue; // out of the world: KillZ
					FNavLocation Land;
					const int32* j = Mesh->ProjectPoint(Hit.ImpactPoint, Land, LandExtent) ? Index.Find(Land.NodeRef) : nullptr;
					if (j)
						AddMove(Island[i], Island[*j], FMath::Max(0.f, P.Z - Land.Location.Z));
				}
			}
		}
		// ladders and teleporters: their nav links
		for (TActorIterator<AActor> It(World); It; ++It) {
			const INavLinkHostInterface* Host = Cast<INavLinkHostInterface>(*It);
			TArray<FNavigationLink> Links;
			TArray<FNavigationSegmentLink> Segments;
			if (!Host || !Host->GetNavigationLinksArray(Links, Segments))
				continue;
			for (const FNavigationLink& L : Links) {
				const int32 Left = IslandAt(It->GetActorTransform().TransformPosition(L.Left), LandExtent);
				const int32 Right = IslandAt(It->GetActorTransform().TransformPosition(L.Right), LandExtent);
				AddMove(Left, Right, 0.f);
				if (L.Direction == ENavLinkDirection::BothWays)
					AddMove(Right, Left, 0.f);
			}
		}
		// lifts and doors, water and updrafts: a hub joining every island they touch, both ways
		TArray<FBox> Hubs;
		for (TActorIterator<ADFMover> It(World); It; ++It)
			Hubs.Add(It->GetComponentsBoundingBox(true).ExpandBy(200.f));
		TArray<FBox> Pain, Water;
		for (TActorIterator<ADFZoneVolume> It(World); It; ++It) {
			if (It->bWaterVolume || It->bGravityZone)
				Hubs.Add(It->GetComponentsBoundingBox(true).ExpandBy(100.f));
			if (It->bWaterVolume)
				Water.Add(It->GetComponentsBoundingBox(true));
			else if (It->DamagePerSec > 0.f)
				Pain.Add(It->GetComponentsBoundingBox(true));
		}
		Moves.SetNum(Nodes + Hubs.Num());
		for (int32 h = 0; h < Hubs.Num(); ++h) {
			const int32 Hub = Nodes++;
			for (int32 i = 0; i < N; ++i)
				if (Algo::AnyOf(Verts[i], [&](const FVector& P) { return Hubs[h].IsInside(P); })) {
					AddMove(Island[i], Hub, 0.f);
					AddMove(Hub, Island[i], 0.f);
				}
		}

		// where soldiers can get to from a start, and where a start can be got back to from
		TArray<int32> Starts;
		for (TActorIterator<APlayerStart> It(World); It; ++It)
			if (const int32 S = IslandAt(It->GetActorLocation(), FVector(100.f, 100.f, 250.f)); S != INDEX_NONE)
				Starts.AddUnique(S);
		if (Starts.IsEmpty())
			return Pits;
		TArray<TArray<int32>> Back;
		Back.SetNum(Nodes);
		for (int32 From = 0; From < Nodes; ++From)
			for (const TPair<int32, FVector2f>& M : Moves[From])
				Back[M.Key].Add(From);
		auto Spread = [&](auto Next) {
			TBitArray<> Seen(false, Nodes);
			TArray<int32> Open = Starts;
			for (const int32 S : Starts)
				Seen[S] = true;
			while (!Open.IsEmpty()) {
				const int32 At = Open.Pop();
				Next(At, [&](int32 To) {
					if (!Seen[To]) {
						Seen[To] = true;
						Open.Add(To);
					}
				});
			}
			return Seen;
		};
		const TBitArray<> Reached = Spread([&](int32 At, auto Visit) { for (const TPair<int32, FVector2f>& M : Moves[At]) Visit(M.Key); });
		const TBitArray<> Returns = Spread([&](int32 At, auto Visit) { for (const int32 From : Back[At]) Visit(From); });

		// the stuck islands, the ones joined by falls taken as one pit
		TArray<int32> PitOf;
		PitOf.Init(INDEX_NONE, Islands);
		for (int32 i = 0; i < Islands; ++i) {
			if (!Reached[i] || Returns[i] || PitOf[i] != INDEX_NONE)
				continue;
			const int32 Pit = Pits.AddDefaulted();
			TArray<int32> Open = { i };
			PitOf[i] = Pit;
			while (!Open.IsEmpty()) {
				const int32 At = Open.Pop();
				for (const TPair<int32, FVector2f>& M : Moves[At])
					if (M.Key < Islands && PitOf[M.Key] == INDEX_NONE && !Returns[M.Key]) {
						PitOf[M.Key] = Pit;
						Open.Add(M.Key);
					}
			}
		}
		for (FPit& P : Pits)
			P.MinFall = TNumericLimits<float>::Max();
		for (int32 From = 0; From < Nodes; ++From)
			if (Reached[From] && Returns[From])
				for (const TPair<int32, FVector2f>& M : Moves[From])
					if (M.Key < Islands && PitOf[M.Key] != INDEX_NONE) {
						FPit& P = Pits[PitOf[M.Key]];
						P.MinFall = FMath::Min(P.MinFall, M.Value.X);
						P.MaxFall = FMath::Max(P.MaxFall, M.Value.Y);
					}
		for (int32 i = 0; i < N; ++i)
			if (const int32 Pit = PitOf[Island[i]]; Pit != INDEX_NONE) {
				FPit& P = Pits[Pit];
				for (const FVector& V : Verts[i]) {
					P.Floor += V;
					P.bHurts |= Algo::AnyOf(Pain, [&V](const FBox& B) { return B.IsInside(V + FVector(0.f, 0.f, 50.f)); });
					P.bWater |= Algo::AnyOf(Water, [&V](const FBox& B) { return B.IsInside(V + FVector(0.f, 0.f, 50.f)); });
				}
				P.Area += PolyArea(Verts[i]) / 10000.f;
			}
		for (FPit& P : Pits)
			if (P.MinFall == TNumericLimits<float>::Max())
				P.MinFall = 0.f; // reached only from another stuck island
		// the boxes, none over floor soldiers walk on and get back from (its corners, in 10 m cells)
		auto Cell = [](const FVector& V) { return FIntPoint(FMath::FloorToInt(V.X / 1000.f), FMath::FloorToInt(V.Y / 1000.f)); };
		TMultiMap<FIntPoint, FVector> Playable;
		for (int32 i = 0; i < N; ++i)
			if (Reached[Island[i]] && Returns[Island[i]])
				for (const FVector& V : Verts[i])
					Playable.Add(Cell(V), V);
		TArray<TMap<int32, FBox>> PitTiles;
		PitTiles.SetNum(Pits.Num());
		for (int32 i = 0; i < N; ++i)
			if (const int32 Pit = PitOf[Island[i]]; Pit != INDEX_NONE)
				for (const FVector& V : Verts[i])
					PitTiles[Pit].FindOrAdd(TileOf[i], FBox(ForceInit)) += V;
		auto Clash = [&](const FBox& Box) {
			for (int32 x = Cell(Box.Min).X; x <= Cell(Box.Max).X; ++x)
				for (int32 y = Cell(Box.Min).Y; y <= Cell(Box.Max).Y; ++y)
					for (auto It = Playable.CreateConstKeyIterator(FIntPoint(x, y)); It; ++It)
						if (Box.IsInsideOrOn(It.Value()))
							return true;
			return false;
		};
		// out to where a soldier's middle can be, up past their head
		auto Zone = [](const FBox& Floor) { return FBox(Floor.Min - FVector(30.f, 30.f, 100.f), Floor.Max + FVector(30.f, 30.f, 300.f)); };
		auto Area = [](const FBox& B) { return (B.Max.X - B.Min.X) * (B.Max.Y - B.Min.Y); };
		for (int32 p = 0; p < Pits.Num(); ++p) {
			FPit& Pit = Pits[p];
			// a flat pit in one box if that reaches no other floor (a map's bottom under a ship)
			if (Pit.Floor.Max.Z - Pit.Floor.Min.Z <= 200.f && !Clash(Zone(Pit.Floor))) {
				Pit.Boxes.Add(Zone(Pit.Floor));
				continue;
			}
			for (const TPair<int32, FBox>& T : PitTiles[p])
				if (Clash(Zone(T.Value)))
					++Pit.Clashes;
				else
					Pit.Boxes.Add(Zone(T.Value));
			// then tiles joined in rows, then the rows in columns, where they make one rectangle
			// (5% slack for the walls the navmesh keeps off) that still reaches no other floor
			for (const int32 Axis : { 0, 1 }) {
				const int32 Other = 1 - Axis;
				Pit.Boxes.Sort([Axis](const FBox& A, const FBox& B) { return A.Min[Axis] < B.Min[Axis]; });
				for (int32 i = 0; i < Pit.Boxes.Num(); ++i)
					for (int32 j = i + 1; j < Pit.Boxes.Num() && Pit.Boxes[j].Min[Axis] <= Pit.Boxes[i].Max[Axis] + 60.f; ++j) {
						const FBox &A = Pit.Boxes[i], &B = Pit.Boxes[j];
						if (FMath::Abs(A.Min[Other] - B.Min[Other]) > 60.f || FMath::Abs(A.Max[Other] - B.Max[Other]) > 60.f || FMath::Abs(A.Min.Z - B.Min.Z) > 50.f || FMath::Abs(A.Max.Z - B.Max.Z) > 50.f)
							continue;
						const FBox Joined = A + B;
						if (Area(Joined) > 1.05f * (Area(A) + Area(B)) || Clash(Joined))
							continue;
						Pit.Boxes[i] = Joined;
						Pit.Boxes.RemoveAt(j);
						j = i; // the longer box may reach further ones
					}
			}
		}
		return Pits;
	}
} // namespace DF
