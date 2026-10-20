#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/MFTypes.h"
#include "Mods/Tug/MFGameMode_Tug.h"
#include "MFGameMode_Zombie.generated.h"

class AMFCharacter;

/** On zombies: cars hardly hurt them ("Zombies fear no car": run-over damage / 10). */
UCLASS()
class DESKTOPFORCES_API UMFZombieHide : public UActorComponent, public IMFDamageFilter
{
	GENERATED_BODY()
public:
	virtual int32 FilterDamage(int32 Damage, const UMFDamageType* Type) override;
};

/**
 * Zombie Mode (MF-mods ZombieGame, by Animeman for Akvari). Humans (team 0) against zombies
 * (team 1): zombies are faster, tougher and jump higher - the more humans per zombie, the
 * stronger (ZombieBiasExp) - and fight with knives (ZombieWeapons). A human killed by a zombie
 * becomes one (infection); the round goes to the zombies when no human is left, to the humans
 * if any survive the clock. Lone zombies against five or more humans become a NEMESIS (much
 * stronger, regenerates; infection pauses and dead humans wait for the next round); lone humans
 * against five or more zombies become SURVIVORS. Three rounds win (GoalTeamScore); every round
 * starts with the original teams (Tug of War's round logic).
 *
 * ?game=/Script/DesktopForces.MFGameMode_Zombie (maps: ZM-*). Options: ?ZombieWeapons=0-4
 * ?Infect=0 (plain team deathmatch with zombie bodies, FragLimit 30).
 */
UCLASS(Config = Game)
class DESKTOPFORCES_API AMFGameMode_Zombie : public AMFGameMode_Tug
{
	GENERATED_BODY()

public:
	AMFGameMode_Zombie();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void HandleMatchHasStarted() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;
	virtual bool PreventDeath(AMFCharacter* Victim, AController* Killer, TSubclassOf<UDamageType> DamageType) override;
	virtual void Killed(AController* Killer, AController* Victim, AMFCharacter* VictimPawn, TSubclassOf<UDamageType> DamageType) override;
	virtual TSubclassOf<AMFWeapon> GetWeaponClassFor(const AMFCharacter* Character, TSubclassOf<AMFWeapon> WeaponClass) const override;
	virtual void LoadoutDenied(APlayerController* PC) const override;

	bool IsNemesis(const AMFCharacter* C) const { return C && Nemeses.Contains(C); }
	/** ZombieShot alt fire on an enemy: they join the injector's team (+2). False if it can't. */
	bool Inject(AMFCharacter* By, AMFCharacter* Victim);

	/** How much stronger zombies get per human (boost = (humans / zombies) ^ this; 0.16 - 0.2). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombie")
	float ZombieBiasExp = 0.18f;

	/** Zombies' weapon use: 0 knife only, 1 + items lying on the ground, 2 + ranged weapons,
	 *  3 + loadout crates, 4 + loadout zones (MF zombieWeapons). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombie")
	int32 ZombieWeapons = 1;

	/** A zombie heals from the humans it kills (up to MaxHealth / 11, less from further away). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombie")
	bool bZombieLifeSteal = true;

	/** Zombies spawn at neutral starts away from both bases (and humans), not at the red base. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombie")
	bool bSpawnAnywhere = true;

	/** Humans killed by zombies join them. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombie")
	bool bZombieInfect = true;

protected:
	virtual uint8 ChooseTeam(AController* Who, uint8 Preferred) const override;
	virtual bool IsStartAllowed(const AMFPlayerStart* Start, AController* Player) const override;
	virtual void OnTimeLimit() override;
	virtual void CheckEndConditions(AMFPlayerState* LastKiller) override;
	virtual void NextRound() override;

	void BecomeZombie(AMFCharacter* C);
	void BecomeHuman(AMFCharacter* C);
	/** Swap each carried item for this team's variant; zombies keep only melee items below ZombieWeapons 2. */
	void TransformItems(AMFCharacter* C);
	/** MovedTeam: Victim joins By's team; their old team empty -> By +2, round over. */
	void MoveToTeam(AMFPlayerState* Victim, AMFPlayerState* By);
	int32 AliveCount(uint8 Team) const;
	bool TeamScored(uint8 Team) const;
	void Regenerate();
	void Announce(const FString& Line);

	TSet<TWeakObjectPtr<const AMFCharacter>> Nemeses;
	TMap<TWeakObjectPtr<AMFCharacter>, float> RegenCarry;
	/** Infection pauses while a nemesis lives (bZombieInfect stays the setting). */
	bool bInfectActive = true;
	bool bRestartingRound = false;
	FTimerHandle RegenTimer;
	TSet<TWeakObjectPtr<const AMFPlayerStart>> ZombieStarts;

	static constexpr float MeleeDistance = 1350.f;   // MF MeleeDistance 600 uu
};
