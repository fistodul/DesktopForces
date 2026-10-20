#include "Mods/Zombie/MFGameMode_Zombie.h"
#include "Mods/Zombie/MFZombieWeapons.h"
#include "DesktopForces.h"
#include "Game/MFGameState.h"
#include "Game/MFMessages.h"
#include "Game/MFPlayerStart.h"
#include "Game/MFPlayerState.h"
#include "Player/MFCharacter.h"
#include "Vehicles/MFVehicle.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MFZombie"

namespace MFZombieLocal
{
	constexpr uint8 Humans = 0;
	constexpr uint8 Zombies = 1;
}

int32 UMFZombieHide::FilterDamage(int32 Damage, const UMFDamageType* Type)
{
	return Type && Type->IsA<UMFDamage_RunOver>() ? Damage / 10 : Damage;   // ZombieGame.ReduceDamage 'RunDown'
}

AMFGameMode_Zombie::AMFGameMode_Zombie()
{
	GameName = TEXT("Zombie Mode");
	NumTeams = 2;
	GoalTeamScore = 3;          // FragLimit 3 (rounds)
	TimeLimit = 8;              // per round
	FriendlyFireScale = 0.01f;
	bScoreTeamKills = false;
	bKillTransform = false;     // ZombieGame: the infected die and come back as zombies
}

void AMFGameMode_Zombie::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
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

void AMFGameMode_Zombie::InitGameState()
{
	Super::InitGameState();
	if (AMFGameState* GS = GetGameState<AMFGameState>())
	{
		GS->TeamNameOverrides = { TEXT("Human"), TEXT("Zombie") };     // teamcolor(0), teamcolor(1)
		GS->TeamLoadoutAccess = { 4, ZombieWeapons };                  // zombies: TryLoadout* by zombieWeapons
	}
}

void AMFGameMode_Zombie::HandleMatchHasStarted()
{
	// ZombieGame.PostBeginPlay: zombies spawn at neutral starts far from both bases
	ZombieStarts.Reset();
	TArray<const AMFPlayerStart*> Bases;
	for (TActorIterator<AMFPlayerStart> It(GetWorld()); It; ++It)
	{
		if (!It->IsNeutral())
		{
			Bases.Add(*It);
		}
	}
	for (TActorIterator<AMFPlayerStart> It(GetWorld()); It; ++It)
	{
		const bool bFar = !Bases.ContainsByPredicate([&](const AMFPlayerStart* B)
		{
			return FVector::Dist(B->GetActorLocation(), It->GetActorLocation()) < MeleeDistance * 2.f;
		});
		if (It->IsNeutral() && bFar)
		{
			ZombieStarts.Add(*It);
		}
	}
	GetWorldTimerManager().SetTimer(RegenTimer, this, &AMFGameMode_Zombie::Regenerate, 0.25f, true);
	Super::HandleMatchHasStarted();
}

uint8 AMFGameMode_Zombie::ChooseTeam(AController* Who, uint8 Preferred) const
{
	if (Who && Who->IsPlayerController())
	{
		return PickTeam(Preferred);
	}
	// ZombieGame.AddBot: keep about three humans per zombie
	return TeamSize(MFZombieLocal::Humans) < 3 * FMath::Max(TeamSize(MFZombieLocal::Zombies), 1) ? MFZombieLocal::Humans : MFZombieLocal::Zombies;
}

bool AMFGameMode_Zombie::IsStartAllowed(const AMFPlayerStart* Start, AController* Player) const
{
	const AMFPlayerState* PS = Player ? Player->GetPlayerState<AMFPlayerState>() : nullptr;
	if (!bSpawnAnywhere || !PS || !Start->bEnabled || Start->bCaptainStart)
	{
		return Super::IsStartAllowed(Start, Player);
	}
	const uint8 Team = PS->GetTeam();
	if (Team == MFZombieLocal::Zombies ? (!ZombieStarts.Contains(Start) && ZombieStarts.Num() > 0) : Start->IsNeutral())
	{
		return false;   // humans: any team base; zombies: the neutral starts away from them
	}
	// IsForTeam: no enemy within 1.6 melee distances
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetTeam() != Team && FVector::Dist(It->GetActorLocation(), Start->GetActorLocation()) <= MeleeDistance * 1.6f)
		{
			return false;
		}
	}
	return true;
}

void AMFGameMode_Zombie::RestartPlayer(AController* NewPlayer)
{
	const AMFPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AMFPlayerState>() : nullptr;
	// a nemesis is loose: dead humans wait for the next round
	if (PS && PS->GetTeam() == MFZombieLocal::Humans && bZombieInfect && !bInfectActive && !bRestartingRound)
	{
		return;
	}
	Super::RestartPlayer(NewPlayer);
	if (AMFCharacter* C = NewPlayer ? Cast<AMFCharacter>(NewPlayer->GetPawn()) : nullptr)
	{
		if (C->GetTeam() == MFZombieLocal::Zombies)
		{
			BecomeZombie(C);
		}
		else
		{
			BecomeHuman(C);
		}
	}
}

void AMFGameMode_Zombie::SetPlayerDefaults(APawn* PlayerPawn)
{
	AMFCharacter* C = Cast<AMFCharacter>(PlayerPawn);
	if (!C || C->GetTeam() != MFZombieLocal::Zombies)
	{
		Super::SetPlayerDefaults(PlayerPawn);
		return;
	}
	// AddDefaultInventory: "MFZombieLocal::Zombies get jack shite" - melee items, no loadout at the start
	AGameModeBase::SetPlayerDefaults(PlayerPawn);
	C->GiveWeapon(AMFWeapon_ZombieKnife::StaticClass(), true);
	C->GiveWeapon(AMFWeapon_ZombieArmour::StaticClass());
	C->GiveWeapon(AMFWeapon_ZombieShot::StaticClass());
}

TSubclassOf<AMFWeapon> AMFGameMode_Zombie::GetWeaponClassFor(const AMFCharacter* Character, TSubclassOf<AMFWeapon> W) const
{
	if (!W)
	{
		return W;
	}
	const bool bZombie = Character && Character->GetTeam() == MFZombieLocal::Zombies;
	// TransformToZombieItems / TransformToHumanItems
	if (W->IsChildOf<AMFWeapon_Knife>())
	{
		return bZombie ? AMFWeapon_ZombieKnife::StaticClass() : AMFWeapon_Knife::StaticClass();
	}
	if (W->IsChildOf<AMFWeapon_Armour>() && (W == AMFWeapon_Armour::StaticClass() || W == AMFWeapon_ZombieArmour::StaticClass()))
	{
		return bZombie ? AMFWeapon_ZombieArmour::StaticClass() : AMFWeapon_Armour::StaticClass();
	}
	if (W == AMFWeapon_AdrenalineShot::StaticClass())
	{
		return AMFWeapon_ZombieShot::StaticClass();
	}
	if (W == AMFWeapon_Shotgun::StaticClass())
	{
		return AMFWeapon_ZombieShotgun::StaticClass();
	}
	return W;
}

void AMFGameMode_Zombie::TransformItems(AMFCharacter* C)
{
	const bool bMeleeOnly = C->GetTeam() == MFZombieLocal::Zombies && ZombieWeapons < 2;
	TArray<AMFWeapon*> Carried(C->GetInventory());
	for (AMFWeapon* W : Carried)
	{
		const TSubclassOf<AMFWeapon> Want = GetWeaponClassFor(C, W->GetClass());
		const bool bMelee = Want == AMFWeapon_ZombieKnife::StaticClass() || Want == AMFWeapon_ZombieArmour::StaticClass()
			|| Want == AMFWeapon_ZombieShot::StaticClass();
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

void AMFGameMode_Zombie::BecomeZombie(AMFCharacter* C)
{
	const float Ratio = float(TeamSize(MFZombieLocal::Humans)) / FMath::Max(TeamSize(MFZombieLocal::Zombies), 1);
	const bool bNemesis = Ratio >= 5.f;
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
	const AMFCharacter* Def = GetDefault<AMFCharacter>(C->GetClass());
	FMFBodyScale S;
	S.Speed = FMath::Min(1.55f * Boost, 2.2f) - 0.2f;   // GroundSpeed = BaseGroundSpeed * x - 80 uu (of 400)
	S.Jump = FMath::Min(1.45f * Boost, 3.f);
	S.Accel = 1.15f;
	S.Step = 1.15f;
	S.FallDamage = FMath::Min(1.5f * Boost, 2.55f);
	S.FallDeath = 1.5f * Boost;
	C->SetBodyScale(S);
	C->MaxHealth = FMath::RoundToInt(Def->MaxHealth * 3.33f * Boost);
	UE_LOG(LogMF, Log, TEXT("Zombie: %s is a zombie (boost %.2f, health %d, speed x%.2f%s)"), C->GetPlayerState() ? *C->GetPlayerState()->GetPlayerName() : TEXT("?"), Boost, C->MaxHealth, S.Speed, bNemesis ? TEXT(", NEMESIS") : TEXT(""));
	C->MaxCarry = Def->MaxCarry - 2;
	C->Heal(C->MaxHealth);
	if (!C->FindComponentByClass<UMFZombieHide>())
	{
		UMFZombieHide* Hide = NewObject<UMFZombieHide>(C);
		Hide->RegisterComponent();
	}
	TransformItems(C);
	// "Feel the hunger in your hands"
	if (AMFWeapon* Knife = C->FindWeaponOfClass(AMFWeapon_ZombieKnife::StaticClass()))
	{
		C->EquipWeapon(Knife);
	}
	else
	{
		C->GiveWeapon(AMFWeapon_ZombieKnife::StaticClass(), true);
	}
}

void AMFGameMode_Zombie::BecomeHuman(AMFCharacter* C)
{
	const float Ratio = (TeamSize(MFZombieLocal::Zombies) + 0.5f) / FMath::Max(TeamSize(MFZombieLocal::Humans), 1);
	const bool bSurvivor = Ratio >= 5.f;
	const AMFCharacter* Def = GetDefault<AMFCharacter>(C->GetClass());
	C->MaxHealth = bSurvivor ? FMath::RoundToInt(Def->MaxHealth * FMath::Max(Ratio / 2.5f, 2.f)) : Def->MaxHealth;
	C->MaxCarry = Def->MaxCarry + 1;
	C->SetBodyScale(FMFBodyScale());
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
	if (UMFZombieHide* Hide = C->FindComponentByClass<UMFZombieHide>())
	{
		Hide->DestroyComponent();
	}
	TransformItems(C);
}

void AMFGameMode_Zombie::Regenerate()
{
	// nemesis / survivor: 1.5 health a second (ZombiePlayer.regenerationRate)
	for (auto It = Nemeses.CreateIterator(); It; ++It)
	{
		AMFCharacter* C = const_cast<AMFCharacter*>(It->Get());
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

int32 AMFGameMode_Zombie::AliveCount(uint8 Team) const
{
	int32 N = 0;
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		N += It->IsAlive() && It->GetTeam() == Team;
	}
	return N;
}

bool AMFGameMode_Zombie::TeamScored(uint8 Team) const
{
	if (const AGameStateBase* GS = GetGameState<AGameStateBase>())
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			const AMFPlayerState* M = Cast<AMFPlayerState>(PS);
			if (M && M->GetTeam() == Team && M->GetScore() > 0.f)
			{
				return true;
			}
		}
	}
	return false;
}

void AMFGameMode_Zombie::Announce(const FString& Line)
{
	BroadcastChat(nullptr, Line, false);
}

void AMFGameMode_Zombie::MoveToTeam(AMFPlayerState* Victim, AMFPlayerState* By)
{
	const uint8 OldTeam = Victim->GetTeam();
	Victim->SetTeam(By->GetTeam());
	UE_LOG(LogMF, Log, TEXT("Zombie: %s infected by %s (humans %d, zombies %d)"), *Victim->GetPlayerName(), *By->GetPlayerName(), TeamSize(MFZombieLocal::Humans), TeamSize(MFZombieLocal::Zombies));
	if (TeamSize(OldTeam) == 0)
	{
		By->SetScore(By->GetScore() + 2.f);
		RoundEnded(By->GetTeam());
	}
}

bool AMFGameMode_Zombie::PreventDeath(AMFCharacter* Victim, AController* Killer, TSubclassOf<UDamageType> DamageType)
{
	AMFPlayerState* KPS = Killer ? Killer->GetPlayerState<AMFPlayerState>() : nullptr;
	AMFPlayerState* VPS = Victim ? Victim->GetPlayerState<AMFPlayerState>() : nullptr;
	if (!bZombieInfect || !bInfectActive || !bKillTransform || bRoundOver || !KPS || !VPS
		|| KPS->GetTeam() != MFZombieLocal::Zombies || VPS->GetTeam() != MFZombieLocal::Humans)
	{
		return false;
	}
	// ZombiePlayer.Died with bKillTransform: turn on the spot instead of dying
	ScoreKill(KPS, VPS);
	BroadcastLocalized(this, UMFDeathMessage::StaticClass(), 0, KPS, VPS, DamageType.Get());
	MoveToTeam(VPS, KPS);
	if (!bRoundOver)
	{
		BecomeZombie(Victim);
	}
	return true;
}

void AMFGameMode_Zombie::Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType)
{
	Super::Killed(Killer, Victim, VictimPawn, DamageType);
	AMFPlayerState* KPS = Killer ? Killer->GetPlayerState<AMFPlayerState>() : nullptr;
	AMFPlayerState* VPS = Victim ? Victim->GetPlayerState<AMFPlayerState>() : (VictimPawn ? VictimPawn->GetPlayerState<AMFPlayerState>() : nullptr);
	const bool bWasNemesis = IsNemesis(VictimPawn);
	UE_LOG(LogMF, Verbose, TEXT("Zombie: %s (team %d) killed by %s with %s at %s"), VPS ? *VPS->GetPlayerName() : TEXT("?"), VPS ? VPS->GetTeam() : -1,
		KPS ? *KPS->GetPlayerName() : TEXT("-"), *GetNameSafe(DamageType.Get()), VictimPawn ? *VictimPawn->GetActorLocation().ToCompactString() : TEXT("?"));
	Nemeses.Remove(VictimPawn);
	if (VictimPawn && VictimPawn->GetTeam() == MFZombieLocal::Zombies && bWasNemesis)
	{
		bInfectActive = bZombieInfect;   // the nemesis is down: infection again
	}
	if (!bZombieInfect || bRoundOver || !VPS || VPS->GetTeam() != MFZombieLocal::Humans)
	{
		return;
	}
	// Don't let the last human cheat death...
	if (AliveCount(MFZombieLocal::Humans) == 0 && (TeamScored(MFZombieLocal::Zombies) || bWasNemesis || !bInfectActive))
	{
		RoundEnded(MFZombieLocal::Zombies);
		return;
	}
	AMFCharacter* KillerPawn = Killer ? Cast<AMFCharacter>(Killer->GetPawn()) : nullptr;
	if (!KPS || KPS->GetTeam() != MFZombieLocal::Zombies)
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
	if (bInfectActive && !(DamageType && DamageType->IsChildOf<UMFDamage_RunOver>()))
	{
		MoveToTeam(VPS, KPS);
	}
}

bool AMFGameMode_Zombie::Inject(AMFCharacter* By, AMFCharacter* Victim)
{
	AMFPlayerState* BPS = By ? By->GetPlayerState<AMFPlayerState>() : nullptr;
	AMFPlayerState* VPS = Victim ? Victim->GetPlayerState<AMFPlayerState>() : nullptr;
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
		Victim->Die(By->GetController(), UMFDamage_Suicide::StaticClass(), By);   // comes back on the new side
	}
	return true;
}

void AMFGameMode_Zombie::OnTimeLimit()
{
	if (!bZombieInfect)
	{
		AMFGameMode::OnTimeLimit();
		return;
	}
	if (!bRoundOver)
	{
		RoundEnded(TeamSize(MFZombieLocal::Humans) > 0 ? MFZombieLocal::Humans : MFZombieLocal::Zombies);   // humans who make it through win the round
	}
}

void AMFGameMode_Zombie::CheckEndConditions(AMFPlayerState* LastKiller)
{
	if (!bZombieInfect)
	{
		AMFGameMode::CheckEndConditions(LastKiller);
	}
}

void AMFGameMode_Zombie::NextRound()
{
	bInfectActive = bZombieInfect;
	Nemeses.Reset();
	RegenCarry.Reset();
	TGuardValue<bool> Restarting(bRestartingRound, true);
	Super::NextRound();
}

void AMFGameMode_Zombie::LoadoutDenied(APlayerController* PC) const
{
	if (PC)
	{
		PC->ClientMessage(LOCTEXT("NiceTry", "Nice try, zombie!").ToString());   // ZombiePlayer.TryLoadout
	}
}

#undef LOCTEXT_NAMESPACE
