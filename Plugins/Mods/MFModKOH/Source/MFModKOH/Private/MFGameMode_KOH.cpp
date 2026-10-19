#include "MFGameMode_KOH.h"
#include "Core/MFGameSettings.h"
#include "Game/MFGameState.h"
#include "Game/MFPlayerStart.h"
#include "Game/MFPlayerState.h"
#include "Player/MFCharacter.h"
#include "World/MFMapActors.h"
#include "EngineUtils.h"

AMFGameMode_KOH::AMFGameMode_KOH()
{
	GameName = TEXT("King of the Hill");
	GoalTeamScore = 700;   // points, not minutes as in Holdout
	FragLimit = 0;
	TimeLimit = 0;
}

bool AMFGameMode_KOH::PickPoint(TArray<FCandidate>& Pool, TArray<FTransform>& Chosen, const FVector* Ref, int32 PreferredType, float MinDist) const
{
	// KOHGame.PickBestCandidate: nearest to Ref (or random), +preference for the type, spaced out
	int32 Best = INDEX_NONE;
	float BestScore = FLT_MAX;
	for (int32 i = 0; i < Pool.Num(); ++i)
	{
		const FVector Loc = Pool[i].Where.GetLocation();
		if (Chosen.ContainsByPredicate([&](const FTransform& C) { return FVector::Dist(C.GetLocation(), Loc) < MinDist; }))
		{
			continue;
		}
		float Score = Ref ? FVector::Dist(Loc, *Ref) : FMath::FRand() * 2250.f;
		if (Pool[i].Type == PreferredType)
		{
			Score -= 11250.f;   // MF: -5000 uu preference bonus
		}
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = i;
		}
	}
	if (Best == INDEX_NONE)
	{
		return false;
	}
	Chosen.Add(Pool[Best].Where);
	Pool.RemoveAt(Best);
	return true;
}

void AMFGameMode_KOH::SpawnPoints()
{
	TArray<FCandidate> Pool;
	auto AddAll = [&](FName Class, int32 Type)
	{
		for (AMFLegacyActor* L : AMFLegacyActor::FindAll(GetWorld(), Class))
		{
			Pool.Add({ L->GetActorTransform(), Type });
		}
	};
	AddAll(TEXT("RageDominationTimer"), 0);
	AddAll(TEXT("RageDetonationLock"), 1);
	AddAll(TEXT("RageDetPossibleKeyPos"), 2);

	// Team bases = average of each team's starts; neutral starts far from them join the pool
	TMap<uint8, FVector> BaseSum;
	TMap<uint8, int32> BaseCount;
	TArray<AMFPlayerStart*> Neutral;
	for (TActorIterator<AMFPlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->IsNeutral())
		{
			Neutral.Add(*It);
		}
		else
		{
			BaseSum.FindOrAdd(It->TeamNumber) += It->GetActorLocation();
			BaseCount.FindOrAdd(It->TeamNumber)++;
		}
	}
	TArray<FVector> Bases;
	for (const auto& Pair : BaseSum)
	{
		Bases.Add(Pair.Value / BaseCount[Pair.Key]);
	}
	for (AMFPlayerStart* S : Neutral)
	{
		const bool bNearBase = Bases.ContainsByPredicate([&](const FVector& B) { return FVector::Dist(B, S->GetActorLocation()) < MinDistance * 2.f; });
		if (!bNearBase)
		{
			Pool.Add({ S->GetActorTransform(), 2 });
		}
	}

	TArray<FTransform> Chosen;
	if (bCentralizedSpawning && Bases.Num() > 0)
	{
		FVector Centre = FVector::ZeroVector;
		for (const FVector& B : Bases)
		{
			Centre += B;
		}
		Centre /= Bases.Num();
		PickPoint(Pool, Chosen, &Centre, 0, MinDistance);
		if (PointsToSpawn >= 3)
		{
			for (const FVector& B : Bases)
			{
				if (Chosen.Num() < PointsToSpawn)
				{
					PickPoint(Pool, Chosen, &B, 1, MinDistance);
				}
			}
		}
	}
	while (Chosen.Num() < PointsToSpawn && PickPoint(Pool, Chosen, nullptr, 2, MinDistance)) {}
	while (Chosen.Num() < PointsToSpawn && PickPoint(Pool, Chosen, nullptr, 2, MinDistance * 0.5f)) {}

	for (int32 i = 0; i < Chosen.Num(); ++i)
	{
		SpawnPointAt(Chosen[i], FString::Printf(TEXT("Point %c"), TCHAR('A' + i % 26)));
	}
}

void AMFGameMode_KOH::UpdatePoints()
{
	AMFGameState* GS = GetGameState<AMFGameState>();
	if (!GS || !IsMatchInProgress())
	{
		return;
	}
	// KOHGame.Timer: +1 team score per second per point held
	for (AMFHoldoutPoint* P : Points)
	{
		if (P && P->TeamTimes.IsValidIndex(P->OwnerTeam))
		{
			P->TeamTimes[P->OwnerTeam] += 1;
			GS->TeamScores[P->OwnerTeam] += 1;
		}
	}
	for (int32 T = 0; T < GS->TeamScores.Num(); ++T)
	{
		if (GoalTeamScore > 0 && GS->TeamScores[T] >= GoalTeamScore)
		{
			OnTimeLimit();   // highest team score wins
			return;
		}
	}
}

void AMFGameMode_KOH::PointCaptured(AMFHoldoutPoint* Point, AMFCharacter* By)
{
	if (AMFPlayerState* PS = By->GetPlayerState<AMFPlayerState>())
	{
		PS->SetScore(PS->GetScore() + 2.f);   // KOHGame.ScoreTimer bonus
	}
	Super::PointCaptured(Point, By);
}
