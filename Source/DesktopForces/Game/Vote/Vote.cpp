// Votes: Gregaras' vote mutator (vote.u) on ADFGameMode. Code by Gregaras, MPL-2.0 with his
// agreement; the voteHUD readout is ADFHUD::DrawVote.
#include "Game/DFGameMode.h"
#include "Game/DFGameState.h"
#include "Game/DFPlayerState.h"
#include "AI/DFBotController.h"
#include "Core/DFContent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "GameMapsSettings.h"

FString ADFGameMode::VoteMapURL(const FString& Map, const FString& Mode) const
{
	FString Package;
	for (const FString& P : DF::FindMaps())
	{
		Package = FPackageName::GetShortName(P).Equals(Map, ESearchCase::IgnoreCase) ? P : Package;
	}
	if (Package.IsEmpty())
	{
		return FString();
	}
	// a short name (DefaultEngine.ini GameModeClassAliases: dm, ctf, koh ...) or this mode
	FString Game = GetClass()->GetPathName();
	if (!Mode.IsEmpty())
	{
		const FString Named = UGameMapsSettings::GetGameModeForName(Mode);
		if (!LoadClass<ADFGameMode>(nullptr, *Named, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return FString();
		}
		Game = Named;
	}
	return FString::Printf(TEXT("%s?game=%s"), *Package, *Game);
}

void ADFGameMode::CallVote(APlayerController* By, const FString& Args)
{
	ADFGameState* GS = GetGameState<ADFGameState>();
	const float Now = GetWorld()->GetTimeSeconds();
	if (!By || !GS)
	{
		return;
	}
	if (!VoteCommand.IsEmpty())
	{
		By->ClientMessage(TEXT("Vote already in progress"));
		return;
	}
	TArray<FString> A;
	Args.TrimStartAndEnd().ParseIntoArrayWS(A);
	const FString What = A.Num() ? A[0].ToLower() : FString();
	const FString Name = By->PlayerState ? By->PlayerState->GetPlayerName() : TEXT("Someone");
	FString Text;
	if (What == TEXT("travel") || What == TEXT("map"))
	{
		if (A.Num() < 2 || VoteMapURL(A[1], A.Num() > 2 ? A[2] : FString()).IsEmpty())
		{
			By->ClientMessage(TEXT("Usage: callvote travel <map> [dm|tdm|ctf|holdout|sc|det|cap|trailer|koh|tug|coop|zombie|arena|deball|br]"));
			return;
		}
		Text = FString::Printf(TEXT("%s called a vote to travel to %s%s"), *Name, *A[1], A.Num() > 2 ? *FString::Printf(TEXT(" (%s)"), *A[2]) : TEXT(""));
	}
	else if (What == TEXT("nextmap"))
	{
		Text = FString::Printf(TEXT("%s called a vote for the next map"), *Name);
	}
	else if (What == TEXT("restart"))
	{
		Text = FString::Printf(TEXT("%s called a vote to restart the map"), *Name);
	}
	else if (What == TEXT("addbots") && A.Num() > 1 && FCString::Atoi(*A[1]) > 0)
	{
		Text = FString::Printf(TEXT("%s called a vote to add %d bot(s)"), *Name, FMath::Clamp(FCString::Atoi(*A[1]), 1, 32));
	}
	else if (What == TEXT("kickbots"))
	{
		Text = FString::Printf(TEXT("%s called a vote to kick bots"), *Name);
	}
	else if ((What == TEXT("shuffle") || What == TEXT("balance")) && IsTeamGame())
	{
		Text = FString::Printf(TEXT("%s called a vote to %s the teams"), *Name, *What);
	}
	else
	{
		By->ClientMessage(TEXT("Callvotes: travel <map> [mode], nextmap, restart, addbots <n>, kickbots, shuffle, balance"));
		return;
	}
	Broadcast(this, Text, NAME_None);
	VoteCommand = Args.TrimStartAndEnd();
	VoteBallots.Reset();
	VoteBallots.Add(By, true);   // startCallvote: the caller agrees
	GS->VoteText = Text;
	GS->VoteEndTime = GS->GetServerWorldTimeSeconds() + VoteDuration;
	GetWorldTimerManager().SetTimer(VoteTimer, this, &ADFGameMode::CountVotes, 1.f, true);
	CountVotes();
}

void ADFGameMode::CastVote(APlayerController* By, bool bYes)
{
	if (!By)
	{
		return;
	}
	if (VoteCommand.IsEmpty())
	{
		By->ClientMessage(GetWorld()->GetTimeSeconds() < VoteRecentlyEndedUntil ? TEXT("Vote recently ended") : TEXT("No vote has been called"));
		return;
	}
	VoteBallots.Add(By, bYes);
	CountVotes();
}

void ADFGameMode::CountVotes()
{
	ADFGameState* GS = GetGameState<ADFGameState>();
	if (VoteCommand.IsEmpty() || !GS)
	{
		return;
	}
	int32 Yes = 0, No = 0;
	for (const auto& B : VoteBallots)
	{
		if (B.Key.IsValid())   // left the game: their vote goes
		{
			(B.Value ? Yes : No) += 1;
		}
	}
	GS->VoteYes = Yes;
	GS->VoteNo = No;
	const int32 Players = FMath::Max(1, GetNumPlayers());
	if (Yes > Players / 2)
	{
		EndVote(true);
	}
	else if (Yes + No >= Players)
	{
		EndVote(false);
	}
	else if (GS->GetServerWorldTimeSeconds() >= GS->VoteEndTime)
	{
		Broadcast(this, TEXT("Vote failed (timeout)"), NAME_None);
		EndVote(false);
	}
}

void ADFGameMode::EndVote(bool bPassed)
{
	const FString Command = VoteCommand;
	VoteCommand.Reset();
	VoteBallots.Reset();
	GetWorldTimerManager().ClearTimer(VoteTimer);
	VoteRecentlyEndedUntil = GetWorld()->GetTimeSeconds() + 3.f;
	if (ADFGameState* GS = GetGameState<ADFGameState>())
	{
		GS->VoteText.Reset();
	}
	if (bPassed)
	{
		Broadcast(this, TEXT("Vote passed"), NAME_None);
		RunVote(Command);
	}
	else
	{
		Broadcast(this, TEXT("Vote failed"), NAME_None);
	}
}

void ADFGameMode::RunVote(const FString& Command)
{
	TArray<FString> A;
	Command.ParseIntoArrayWS(A);
	const FString What = A.Num() ? A[0].ToLower() : FString();
	if (What == TEXT("travel") || What == TEXT("map"))
	{
		GetWorld()->ServerTravel(VoteMapURL(A[1], A.Num() > 2 ? A[2] : FString()));
	}
	else if (What == TEXT("nextmap") && !TravelToNextMap())
	{
		const TArray<FString> Maps = DF::FindMaps();
		const int32 Here = Maps.IndexOfByPredicate([this](const FString& P) { return FPackageName::GetShortName(P) == UWorld::RemovePIEPrefix(GetWorld()->GetMapName()); });
		if (Maps.Num())
		{
			GetWorld()->ServerTravel(FString::Printf(TEXT("%s?game=%s"), *Maps[(Here + 1) % Maps.Num()], *GetClass()->GetPathName()));
		}
	}
	else if (What == TEXT("restart"))
	{
		RestartGame();
	}
	else if (What == TEXT("addbots"))
	{
		AddBots(FMath::Clamp(FCString::Atoi(*A[1]), 1, 32));
	}
	else if (What == TEXT("kickbots"))
	{
		for (TActorIterator<ADFBotController> It(GetWorld()); It; ++It)
		{
			if (APawn* P = It->GetPawn())
			{
				P->Destroy();
			}
			It->Destroy();
		}
		NumBots = 0;
	}
	else if (What == TEXT("shuffle") || What == TEXT("balance"))
	{
		// shuffle: everyone dealt out again at random; balance: the fewest moves to even teams
		const int32 Teams = FMath::Max(2, NumTeams);
		TArray<TArray<AController*>> ByTeam;
		ByTeam.SetNum(Teams);
		TArray<AController*> All;
		for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
		{
			if (AController* C = It->Get(); C && C->GetPlayerState<ADFPlayerState>())
			{
				All.Add(C);
				ByTeam[FMath::Min<int32>(C->GetPlayerState<ADFPlayerState>()->GetTeam(), Teams - 1)].Add(C);
			}
		}
		TMap<AController*, uint8> Moves;
		if (What == TEXT("shuffle"))
		{
			for (int32 I = All.Num() - 1; I > 0; --I)
			{
				All.Swap(I, FMath::RandRange(0, I));
			}
			for (int32 I = 0; I < All.Num(); ++I)
			{
				Moves.Add(All[I], uint8(I % Teams));
			}
		}
		else
		{
			while (true)
			{
				int32 Big = 0, Small = 0;
				for (int32 T = 1; T < Teams; ++T)
				{
					Big = ByTeam[T].Num() > ByTeam[Big].Num() ? T : Big;
					Small = ByTeam[T].Num() < ByTeam[Small].Num() ? T : Small;
				}
				if (ByTeam[Big].Num() - ByTeam[Small].Num() <= 1)
				{
					break;
				}
				AController* C = ByTeam[Big].Pop();
				ByTeam[Small].Add(C);
				Moves.Add(C, uint8(Small));
			}
		}
		for (const auto& M : Moves)
		{
			ADFPlayerState* PS = M.Key->GetPlayerState<ADFPlayerState>();
			if (PS->GetTeam() != M.Value)
			{
				PS->SetTeam(M.Value);
				if (APawn* P = M.Key->GetPawn())
				{
					P->Destroy();
				}
				RestartPlayer(M.Key);
			}
		}
	}
}
