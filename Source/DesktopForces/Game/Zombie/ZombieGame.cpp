#include "Game/Zombie/ZombieGame.h"
#include "Game/Zombie/ZombieWeapons.h"
#include "DesktopForces.h"
#include "Game/DFGameState.h"
#include "Game/DFMessages.h"
#include "Game/DFPlayerStart.h"
#include "Game/DFPlayerState.h"
#include "Player/DFCharacter.h"
#include "Vehicles/DFVehicle.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "DFZombie"

namespace ZombieLocal
{
	constexpr uint8 Humans = 0;
	constexpr uint8 Zombies = 1;
}

int32 UZombieHide::FilterDamage(int32 Damage, const UDFDamageType* Type)
{
	return Type && Type->IsA<UDFDamage_RunOver>() ? Damage / 10 : Damage;   // ZombieGame.ReduceDamage 'RunDown'
}

AZombieGame::AZombieGame()
{
	GameName = TEXT("Zombie Mode");
	MFGameClass = TEXT("ZombieGame");
	NumTeams = 2;
	GoalTeamScore = 3;          // FragLimit 3 (rounds)
	TimeLimit = 8;              // per round
	FriendlyFireScale = 0.01f;
	bScoreTeamKills = false;
	bKillTransform = false;     // ZombieGame: the infected die and come back as zombies
}

void AZombieGame::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	ZombieWeapons = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("ZombieWeapons"), ZombieWeapons), 0, 4);
	bZombieInfect = UGameplayStatics::GetIntOption(Options, TEXT("Infect"), bZombieInfect ? 1 : 0) != 0;
	if (!bZombieInfect)
	{
		// ZombieGame.PreBeginPlay: without infection it's a team deathmatch
		FragLimit = UGameplayStatics::GetIntOption(Options, TEXT("FragLimit"), 30);
		GoalTeamScore = UGameplayStatics::GetIntOption(Options, TEXT("GoalTeamScore"), 30);
		bScoreTeamKills = true;
	}
	bInfectActive = bZombieInfect;
}

void AZombieGame::InitGameState()
{
	Super::InitGameState();
	if (ADFGameState* GS = GetGameState<ADFGameState>())
	{
		GS->TeamNameOverrides = { TEXT("Human"), TEXT("Zombie") };     // teamcolor(0), teamcolor(1)
		GS->TeamLoadoutAccess = { 4, ZombieWeapons };                  // zombies: TryLoadout* by zombieWeapons
	}
}

void AZombieGame::HandleMatchHasStarted()
{
	// ZombieGame.PostBeginPlay: zombies spawn at neutral starts far from both bases
	ZombieStarts.Reset();
	TArray<const ADFPlayerStart*> Bases;
	for (TActorIterator<ADFPlayerStart> It(GetWorld()); It; ++It)
	{
		if (!It->IsNeutral())
		{
			Bases.Add(*It);
		}
	}
	for (TActorIterator<ADFPlayerStart> It(GetWorld()); It; ++It)
	{
		const bool bFar = !Bases.ContainsByPredicate([&](const ADFPlayerStart* B)
		{
			return FVector::Dist(B->GetActorLocation(), It->GetActorLocation()) < MeleeDistance * 2.f;
		});
		if (It->IsNeutral() && bFar)
		{
			ZombieStarts.Add(*It);
		}
	}
	GetWorldTimerManager().SetTimer(RegenTimer, this, &AZombieGame::Regenerate, 0.25f, true);
	Super::HandleMatchHasStarted();
}

uint8 AZombieGame::ChooseTeam(AController* Who, uint8 Preferred) const
{
	if (Who && Who->IsPlayerController())
	{
		return PickTeam(Preferred);
	}
	// ZombieGame.AddBot: keep about three humans per zombie
	return TeamSize(ZombieLocal::Humans) < 3 * FMath::Max(TeamSize(ZombieLocal::Zombies), 1) ? ZombieLocal::Humans : ZombieLocal::Zombies;
}

bool AZombieGame::IsStartAllowed(const ADFPlayerStart* Start, AController* Player) const
{
	const ADFPlayerState* PS = Player ? Player->GetPlayerState<ADFPlayerState>() : nullptr;
	if (!bSpawnAnywhere || !PS || !Start->bEnabled || Start->bCaptainStart)
	{
		return Super::IsStartAllowed(Start, Player);
	}
	const uint8 Team = PS->GetTeam();
	if (Team == ZombieLocal::Zombies ? (!ZombieStarts.Contains(Start) && ZombieStarts.Num() > 0) : Start->IsNeutral())
	{
		return false;   // humans: any team base; zombies: the neutral starts away from them
	}
	// IsForTeam: no enemy within 1.6 melee distances
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetTeam() != Team && FVector::Dist(It->GetActorLocation(), Start->GetActorLocation()) <= MeleeDistance * 1.6f)
		{
			return false;
		}
	}
	return true;
}

void AZombieGame::RestartPlayer(AController* NewPlayer)
{
	const ADFPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ADFPlayerState>() : nullptr;
	// a nemesis is loose: dead humans wait for the next round
	if (PS && PS->GetTeam() == ZombieLocal::Humans && bZombieInfect && !bInfectActive && !bRestartingRound)
	{
		return;
	}
	Super::RestartPlayer(NewPlayer);
	if (ADFCharacter* C = NewPlayer ? Cast<ADFCharacter>(NewPlayer->GetPawn()) : nullptr)
	{
		if (C->GetTeam() == ZombieLocal::Zombies)
		{
			BecomeZombie(C);
		}
		else
		{
			BecomeHuman(C);
		}
	}
}

void AZombieGame::SetPlayerDefaults(APawn* PlayerPawn)
{
	ADFCharacter* C = Cast<ADFCharacter>(PlayerPawn);
	if (!C || C->GetTeam() != ZombieLocal::Zombies)
	{
		Super::SetPlayerDefaults(PlayerPawn);
		return;
	}
	// AddDefaultInventory: "ZombieLocal::Zombies get jack shite" - melee items, no loadout at the start
	AGameModeBase::SetPlayerDefaults(PlayerPawn);
	C->GiveWeapon(AZombieKnife::StaticClass(), true);
	C->GiveWeapon(AZombieArmour::StaticClass());
	C->GiveWeapon(AZombieShot::StaticClass());
}

TSubclassOf<ADFWeapon> AZombieGame::GetWeaponClassFor(const ADFCharacter* Character, TSubclassOf<ADFWeapon> W) const
{
	if (!W)
	{
		return W;
	}
	const bool bZombie = Character && Character->GetTeam() == ZombieLocal::Zombies;
	// TransformToZombieItems / TransformToHumanItems
	if (W->IsChildOf<AKnife>())
	{
		return bZombie ? AZombieKnife::StaticClass() : AKnife::StaticClass();
	}
	if (W->IsChildOf<AArmour>() && (W == AArmour::StaticClass() || W == AZombieArmour::StaticClass()))
	{
		return bZombie ? AZombieArmour::StaticClass() : AArmour::StaticClass();
	}
	if (W == AAdrenalineShot::StaticClass())
	{
		return AZombieShot::StaticClass();
	}
	if (W == AShotgun::StaticClass())
	{
		return AZombieShotgun::StaticClass();
	}
	return W;
}

void AZombieGame::TransformItems(ADFCharacter* C)
{
	const bool bMeleeOnly = C->GetTeam() == ZombieLocal::Zombies && ZombieWeapons < 2;
	TArray<ADFWeapon*> Carried(C->GetInventory());
	for (ADFWeapon* W : Carried)
	{
		const TSubclassOf<ADFWeapon> Want = GetWeaponClassFor(C, W->GetClass());
		const bool bMelee = Want == AZombieKnife::StaticClass() || Want == AZombieArmour::StaticClass()
			|| Want == AZombieShot::StaticClass();
		if (bMeleeOnly && !bMelee)
		{
			C->RemoveWeapon(W);           // StripRanged
		}
		else if (Want != W->GetClass())
		{
			C->RemoveWeapon(W);
			C->GiveWeapon(Want);
		}
	}
}

void AZombieGame::BecomeZombie(ADFCharacter* C, bool bSpecial)
{
	const float Ratio = float(TeamSize(ZombieLocal::Humans)) / FMath::Max(TeamSize(ZombieLocal::Zombies), 1);
	const bool bNemesis = bSpecial || Ratio >= 5.f;
	float Exp = ZombieBiasExp, Cap = 1.253f;
	if (bNemesis)
	{
		Exp = FMath::Max(ZombieBiasExp, 0.24f);
		Cap = 10.f;
		Announce(FString::Printf(TEXT("%s is a NEMESIS!"), *C->GetPlayerState()->GetPlayerName()));
		bInfectActive = false;   // can't cheat death while a nemesis is alive
		Nemeses.Add(C);
	}
	else
	{
		Nemeses.Remove(C);
	}
	const float Boost = FMath::Clamp(FMath::Pow(FMath::Max(Ratio, 1.f), Exp), 1.f, Cap);
	const ADFCharacter* Def = GetDefault<ADFCharacter>(C->GetClass());
	FDFBodyScale S;
	S.Speed = FMath::Min(1.55f * Boost, 2.2f) - 0.2f;   // GroundSpeed = BaseGroundSpeed * x - 80 uu (of 400)
	S.Jump = FMath::Min(1.45f * Boost, 3.f);
	S.Accel = 1.15f;
	S.Step = 1.15f;
	S.FallDamage = FMath::Min(1.5f * Boost, 2.55f);
	S.FallDeath = 1.5f * Boost;
	C->SetBodyScale(S);
	C->MaxHealth = FMath::RoundToInt(Def->MaxHealth * 3.33f * Boost);
	UE_LOG(LogDF, Log, TEXT("Zombie: %s is a zombie (boost %.2f, health %d, speed x%.2f%s)"), C->GetPlayerState() ? *C->GetPlayerState()->GetPlayerName() : TEXT("?"), Boost, C->MaxHealth, S.Speed, bNemesis ? TEXT(", NEMESIS") : TEXT(""));
	C->MaxCarry = Def->MaxCarry - 2;
	C->Heal(C->MaxHealth);
	if (!C->FindComponentByClass<UZombieHide>())
	{
		UZombieHide* Hide = NewObject<UZombieHide>(C);
		Hide->RegisterComponent();
	}
	TransformItems(C);
	// "Feel the hunger in your hands"
	if (ADFWeapon* Knife = C->FindWeaponOfClass(AZombieKnife::StaticClass()))
	{
		C->EquipWeapon(Knife);
	}
	else
	{
		C->GiveWeapon(AZombieKnife::StaticClass(), true);
	}
}

void AZombieGame::BecomeHuman(ADFCharacter* C, bool bSpecial)
{
	const float Ratio = (TeamSize(ZombieLocal::Zombies) + 0.5f) / FMath::Max(TeamSize(ZombieLocal::Humans), 1);
	const bool bSurvivor = bSpecial || Ratio >= 5.f;
	const ADFCharacter* Def = GetDefault<ADFCharacter>(C->GetClass());
	C->MaxHealth = bSurvivor ? FMath::RoundToInt(Def->MaxHealth * FMath::Max(Ratio / 2.5f, 2.f)) : Def->MaxHealth;
	C->MaxCarry = Def->MaxCarry + 1;
	C->SetBodyScale(FDFBodyScale());
	C->Heal(C->MaxHealth);
	if (bSurvivor)
	{
		Announce(FString::Printf(TEXT("%s is a SURVIVOR!"), *C->GetPlayerState()->GetPlayerName()));
		Nemeses.Add(C);
	}
	else
	{
		Nemeses.Remove(C);
	}
	if (UZombieHide* Hide = C->FindComponentByClass<UZombieHide>())
	{
		Hide->DestroyComponent();
	}
	TransformItems(C);
}

void AZombieGame::Regenerate()
{
	// nemesis / survivor: 1.5 health a second (ZombiePlayer.regenerationRate)
	for (auto It = Nemeses.CreateIterator(); It; ++It)
	{
		ADFCharacter* C = const_cast<ADFCharacter*>(It->Get());
		if (!C)
		{
			It.RemoveCurrent();
			continue;
		}
		if (!C->IsAlive() || C->GetHealth() >= C->MaxHealth)
		{
			continue;
		}
		float& Carry = RegenCarry.FindOrAdd(C);
		Carry += 1.5f * 0.25f;
		if (Carry >= 1.f)
		{
			const int32 Amount = FMath::FloorToInt(Carry);
			Carry -= Amount;
			C->Heal(Amount);
		}
	}
}

int32 AZombieGame::AliveCount(uint8 Team) const
{
	int32 N = 0;
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
	{
		N += It->IsAlive() && It->GetTeam() == Team;
	}
	return N;
}

bool AZombieGame::TeamScored(uint8 Team) const
{
	if (const AGameStateBase* GS = GetGameState<AGameStateBase>())
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			const ADFPlayerState* M = Cast<ADFPlayerState>(PS);
			if (M && M->GetTeam() == Team && M->GetScore() > 0.f)
			{
				return true;
			}
		}
	}
	return false;
}

void AZombieGame::Announce(const FString& Line)
{
	BroadcastChat(nullptr, Line, false);
}

void AZombieGame::MoveToTeam(ADFPlayerState* Victim, ADFPlayerState* By)
{
	const uint8 OldTeam = Victim->GetTeam();
	Victim->SetTeam(By->GetTeam());
	UE_LOG(LogDF, Log, TEXT("Zombie: %s infected by %s (humans %d, zombies %d)"), *Victim->GetPlayerName(), *By->GetPlayerName(), TeamSize(ZombieLocal::Humans), TeamSize(ZombieLocal::Zombies));
	if (TeamSize(OldTeam) == 0)
	{
		By->SetScore(By->GetScore() + 2.f);
		RoundEnded(By->GetTeam());
	}
}

bool AZombieGame::PreventDeath(ADFCharacter* Victim, AController* Killer, TSubclassOf<UDamageType> DamageType)
{
	ADFPlayerState* KPS = Killer ? Killer->GetPlayerState<ADFPlayerState>() : nullptr;
	ADFPlayerState* VPS = Victim ? Victim->GetPlayerState<ADFPlayerState>() : nullptr;
	if (!bZombieInfect || !bInfectActive || !bKillTransform || bRoundOver || !KPS || !VPS
		|| KPS->GetTeam() != ZombieLocal::Zombies || VPS->GetTeam() != ZombieLocal::Humans)
	{
		return false;
	}
	// ZombiePlayer.Died with bKillTransform: turn on the spot instead of dying
	ScoreKill(KPS, VPS);
	BroadcastLocalized(this, UDFDeathMessage::StaticClass(), 0, KPS, VPS, DamageType.Get());
	MoveToTeam(VPS, KPS);
	if (!bRoundOver)
	{
		BecomeZombie(Victim);
	}
	return true;
}

void AZombieGame::Killed(AController* Killer, AController* Victim, ADFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType)
{
	Super::Killed(Killer, Victim, VictimPawn, DamageType);
	ADFPlayerState* KPS = Killer ? Killer->GetPlayerState<ADFPlayerState>() : nullptr;
	ADFPlayerState* VPS = Victim ? Victim->GetPlayerState<ADFPlayerState>() : (VictimPawn ? VictimPawn->GetPlayerState<ADFPlayerState>() : nullptr);
	const bool bWasNemesis = IsNemesis(VictimPawn);
	UE_LOG(LogDF, Verbose, TEXT("Zombie: %s (team %d) killed by %s with %s at %s"), VPS ? *VPS->GetPlayerName() : TEXT("?"), VPS ? VPS->GetTeam() : -1,
		KPS ? *KPS->GetPlayerName() : TEXT("-"), *GetNameSafe(DamageType.Get()), VictimPawn ? *VictimPawn->GetActorLocation().ToCompactString() : TEXT("?"));
	Nemeses.Remove(VictimPawn);
	if (VictimPawn && VictimPawn->GetTeam() == ZombieLocal::Zombies && bWasNemesis)
	{
		bInfectActive = bZombieInfect;   // the nemesis is down: infection again
	}
	if (!bZombieInfect || bRoundOver || !VPS || VPS->GetTeam() != ZombieLocal::Humans)
	{
		return;
	}
	// Don't let the last human cheat death...
	if (AliveCount(ZombieLocal::Humans) == 0 && (TeamScored(ZombieLocal::Zombies) || bWasNemesis || !bInfectActive))
	{
		RoundEnded(ZombieLocal::Zombies);
		return;
	}
	ADFCharacter* KillerPawn = Killer ? Cast<ADFCharacter>(Killer->GetPawn()) : nullptr;
	if (!KPS || KPS->GetTeam() != ZombieLocal::Zombies)
	{
		return;
	}
	// Let the zombie feast...
	if (bZombieLifeSteal && KillerPawn && VictimPawn)
	{
		const float Away = FVector::Dist(KillerPawn->GetActorLocation(), VictimPawn->GetActorLocation()) / MeleeDistance;
		const int32 Boost = FMath::Min(FMath::RoundToInt(KillerPawn->MaxHealth / 11.f * FMath::Max(1.f - Away, 0.f)), 100);
		KillerPawn->Heal(Boost);
	}
	// Move the infected to the zombies before the round ends (not when run over)
	if (bInfectActive && !(DamageType && DamageType->IsChildOf<UDFDamage_RunOver>()))
	{
		MoveToTeam(VPS, KPS);
	}
}

bool AZombieGame::Inject(ADFCharacter* By, ADFCharacter* Victim)
{
	ADFPlayerState* BPS = By ? By->GetPlayerState<ADFPlayerState>() : nullptr;
	ADFPlayerState* VPS = Victim ? Victim->GetPlayerState<ADFPlayerState>() : nullptr;
	if (!bZombieInfect || !bInfectActive || bRoundOver || !BPS || !VPS || Victim->GetHealth() > 100)
	{
		return false;
	}
	BPS->SetScore(BPS->GetScore() + 2.f);
	MoveToTeam(VPS, BPS);
	if (bRoundOver)
	{
		return true;
	}
	if (bKillTransform)
	{
		BecomeZombie(Victim);
	}
	else
	{
		Victim->Die(By->GetController(), UDFDamage_Suicide::StaticClass(), By);   // comes back on the new side
	}
	return true;
}

void AZombieGame::OnTimeLimit()
{
	if (!bZombieInfect)
	{
		ADFGameMode::OnTimeLimit();
		return;
	}
	if (!bRoundOver)
	{
		RoundEnded(TeamSize(ZombieLocal::Humans) > 0 ? ZombieLocal::Humans : ZombieLocal::Zombies);   // humans who make it through win the round
	}
}

void AZombieGame::CheckEndConditions(ADFPlayerState* LastKiller)
{
	if (!bZombieInfect)
	{
		ADFGameMode::CheckEndConditions(LastKiller);
	}
}

void AZombieGame::NextRound()
{
	bInfectActive = bZombieInfect;
	Nemeses.Reset();
	RegenCarry.Reset();
	TGuardValue<bool> Restarting(bRestartingRound, true);
	Super::NextRound();
}

void AZombieGame::LoadoutDenied(APlayerController* PC) const
{
	if (PC)
	{
		PC->ClientMessage(LOCTEXT("NiceTry", "Nice try, zombie!").ToString());   // ZombiePlayer.TryLoadout
	}
}

#undef LOCTEXT_NAMESPACE
