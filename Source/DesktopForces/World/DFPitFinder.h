#pragma once

#include "CoreMinimal.h"

class UWorld;

namespace DF {
	/** Floor a soldier gets to from a spawn only by falling, with no way back up. */
	struct FPit {
		/** Its navmesh (floor height, eroded by a soldier's radius from the walls). */
		FBox Floor = FBox(ForceInit);
		/** The shortest and the longest fall in, from floor soldiers walk on (cm). */
		float MinFall = 0.f;
		float MaxFall = 0.f;
		/** Navmesh area, m2. */
		float Area = 0.f;
		/** Already in a pain zone (it kills, slowly or not). */
		bool bHurts = false;
		/** Under water: the water broke the fall (MF too), so it never was a death pit. */
		bool bWater = false;
		/** A death zone for it: boxes over its navmesh, one per navmesh tile, out to where a
		 *  soldier's middle can be and up past their head. */
		TArray<FBox> Boxes;
		/** Tiles left out: their box would reach floor soldiers walk on and get back from. */
		int32 Clashes = 0;
	};

	/**
	 * Death pits on a map's navmesh (the DFFindPits cheat, for map_edits.toml death_pits): MF's
	 * deep shafts killed a 100-health soldier with the fall; a zombie (333 health, falls hurt it
	 * at most 100) lands alive and is stuck down there for good. Navmesh islands connected by what
	 * a soldier can do: walk, step off a ledge, jump up onto one, ladders and teleporters (their
	 * nav links), lifts and doors (movers), swimming and updrafts (water and gravity zones). A
	 * pit: an island reached from a player start that reaches none back.
	 */
	DESKTOPFORCES_API TArray<FPit> FindPits(UWorld* World);
} // namespace DF
