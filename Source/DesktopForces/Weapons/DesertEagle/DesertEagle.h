#pragma once

#include "CoreMinimal.h"
#include "Weapons/Pistol/Pistol.h"
#include "DesertEagle.generated.h"

/**
 * Desert Eagle (the community servers' weapon mods, desert.u; its model, arms, icon and sounds
 * are in the core pack). 25 damage, 70 to the head; 12 x 5. Not MF-mods' Deagle (ADeagle).
 */
UCLASS()
class DESKTOPFORCES_API ADesertEagle : public APistol
{
	GENERATED_BODY()
public:
	ADesertEagle();
};
