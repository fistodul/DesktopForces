#include "Game/Holdout/Holdout.h"
#include "World/DFMapActors.h"
#include "EngineUtils.h"

AHoldout::AHoldout() {
	GameName = TEXT("Holdout");
	MFGameClass = TEXT("RageDomination");
	GoalTeamScore = 6; // minutes of hold time (RageDomination.GoalTeamScore)
	TimeLimit = 20;
	PointsToSpawn = 1;
	CaptureBonus = 0;
}

void AHoldout::SpawnPoints() {
	// MF bSingleTarget: Domination uses the map's first timer (KOH's pick otherwise)
	for (TActorIterator<AHoldoutPoint> It(GetWorld()); It; ++It) {
		It->TeamTimes.Init(0, NumTeams);
		Points.Add(*It);
	}
	if (Points.Num() > 0)
		return;
	const TArray<ADFLegacyActor*> Timers = ADFLegacyActor::FindAll(GetWorld(), TEXT("RageDominationTimer"));
	if (Timers.Num() > 0) {
		SpawnPointAt(Timers[0]->GetActorTransform(), TEXT("Holdout"));
		return;
	}
	Super::SpawnPoints();
}
