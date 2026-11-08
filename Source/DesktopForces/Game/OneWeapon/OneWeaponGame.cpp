#include "Game/OneWeapon/OneWeaponGame.h"
#include "Game/DFGameState.h"
#include "Game/DFPlayerState.h"
#include "Player/DFCharacter.h"
#include "Weapons/Armour/Armour.h"
#include "Weapons/DFWeapon.h"
#include "Weapons/Knife/Knife.h"
#include "Weapons/M87/M87.h"
#include "Weapons/Shotgun/Shotgun.h"
#include "Kismet/GameplayStatics.h"

#define LOCTEXT_NAMESPACE "OneWeaponGame"

void AOneWeaponGame::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) {
	Super::InitGame(MapName, Options, ErrorMessage);
	bHasArmour = UGameplayStatics::GetIntOption(Options, TEXT("HasArmour"), bHasArmour ? 1 : 0) != 0;
}

void AOneWeaponGame::InitGameState() {
	Super::InitGameState();
	ADFGameState* GS = GetGameState<ADFGameState>();
	if (!GS)
		return;
	// PreBeginPlay destroyed every LoadoutCrate and InventorySpot: no loadout, nothing lying
	// around (a pickup spot holds only items of the allowed groups; none is)
	GS->LoadoutAccess = 0;
	GS->AllowedItemGroups = { INDEX_NONE };
}

void AOneWeaponGame::SetPlayerDefaults(APawn* PlayerPawn) {
	Super::SetPlayerDefaults(PlayerPawn); // the knife
	ADFCharacter* C = Cast<ADFCharacter>(PlayerPawn);
	if (!C)
		return;
	if (bHasArmour)
		C->GiveWeapon(AArmour::StaticClass());
	for (int32 i = 0; i < Weapons.Num(); ++i) {
		if (!Weapons[i] || !Weapons[i]->GetDefaultObject<ADFWeapon>()->IsContentAvailable())
			continue;
		if (Weapons[i]->IsChildOf<AKnife>())
			break; // carried already
		if (ADFWeapon* W = C->GiveWeapon(Weapons[i], true, true)) {
			if (WeaponClips.IsValidIndex(i) && WeaponClips[i] > 0) {
				W->MaxClips = WeaponClips[i];
				W->GiveFullAmmo();
			}
		}
		break;
	}
}

void AOneWeaponGame::ScoreKill(ADFPlayerState* Killer, ADFPlayerState* Victim) {
	Super::ScoreKill(Killer, Victim);
	if (Killer && Victim && Killer != Victim)
		Event(LOCTEXT("Blood", "Ooooh, blood everywhere now...").ToString(), DF::NoTeam);
}

FText AOneWeaponGame::GetSpreeNote(int32 Level) const {
	return SpreeNotes.IsValidIndex(Level) ? SpreeNotes[Level] : Super::GetSpreeNote(Level);
}

FName AOneWeaponGame::GetSpreeSound(int32 Level) const {
	return SpreeSounds.IsValidIndex(Level) ? SpreeSounds[Level] : Super::GetSpreeSound(Level);
}

AKnifeFight::AKnifeFight() {
	GameName = TEXT("Knife Fight");
	MFGameClass = TEXT("KnifeFightGame");
	InstructionSound = TEXT("Commando_Knife");
	ScoreLimit = 20; // FragLimit
	TimeLimit = 10;
	Weapons = { AKnife::StaticClass() };
	// KFMessages
	SpreeNotes = { LOCTEXT("KF0", "is carving out a combat sortie!"), LOCTEXT("KF1", "is making a sustained attack!"),
		LOCTEXT("KF2", "is skinning the oposition!"), LOCTEXT("KF3", "is leading a military campaign!!"),
		LOCTEXT("KF4", "is absolutely DOMINATING!!!") };
	SpreeSounds = { TEXT("combat_sortie"), TEXT("sustained_attack"), TEXT("skinned_alive"), TEXT("military_campaign"), TEXT("Domination") };
}

AShotgunWars::AShotgunWars() {
	GameName = TEXT("Shotgun Wars");
	MFGameClass = TEXT("ShotgunWarsGame");
	InstructionSound = TEXT("shotgun");
	TimeLimit = 6;
	Weapons = { AM87::StaticClass(), AShotgun::StaticClass() };
	WeaponClips = { 100, 5000 }; // m87Swars MaxClips, SWShotgun MaxClips
	// SWMessages (no sound for the first)
	SpreeNotes = { LOCTEXT("SW0", "got easy killin' spree!"), LOCTEXT("SW1", "is shooting a combat shotgun!"),
		LOCTEXT("SW2", "is skinning the oposition!"), LOCTEXT("SW3", "is DOMINATING!!"), LOCTEXT("SW4", "is LEGENDARY!!!") };
	SpreeSounds = { NAME_None, TEXT("Combat_Shotgun"), TEXT("skinned_alive"), TEXT("Domination"), TEXT("legendary") };
}

#undef LOCTEXT_NAMESPACE
