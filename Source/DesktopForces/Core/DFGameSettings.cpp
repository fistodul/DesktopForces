#include "Core/DFGameSettings.h"
#include "Weapons/DFWeapon.h"
#include "UObject/UObjectIterator.h"

UDFGameSettings::UDFGameSettings()
{
	// Defaults live in Config/DefaultGame.ini; nothing hard-coded here on purpose.
}

FText UDFGameSettings::GetTeamName(uint8 TeamId) const
{
	return Teams.IsValidIndex(TeamId) ? Teams[TeamId].Name : FText::GetEmpty();
}

FLinearColor UDFGameSettings::GetTeamColor(uint8 TeamId) const
{
	return Teams.IsValidIndex(TeamId) ? Teams[TeamId].Color : FLinearColor::White;
}

const TArray<TSubclassOf<ADFWeapon>>& UDFGameSettings::GetLoadoutItems() const
{
	if (CachedItems.Num() == 0)
	{
		for (const TSoftClassPtr<ADFWeapon>& Soft : LoadoutItems)
		{
			if (UClass* C = Soft.LoadSynchronous())
			{
				CachedItems.Add(C);
			}
		}
		// Weapons from mod plugins flag themselves (bAddToLoadout) instead of editing config
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(ADFWeapon::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
				&& It->GetDefaultObject<ADFWeapon>()->bAddToLoadout && It->GetDefaultObject<ADFWeapon>()->IsContentAvailable()
				&& !CachedItems.Contains(*It))
			{
				CachedItems.Add(*It);
			}
		}
	}
	return CachedItems;
}
