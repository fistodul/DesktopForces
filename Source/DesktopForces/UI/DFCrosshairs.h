#pragma once

#include "CoreMinimal.h"

namespace DF {
	/** Alpha bounds of the original 32px CrossHairA..X textures. Normalize visible size, not padding;
	 *  except the dot (A, 2 px): drawn from its whole tile it stays a dot (cropped, it filled the
	 *  crosshair size as a big square). */
	inline FIntRect CrosshairBounds(int32 Shape) {
		static const FIntRect Bounds[] = {
			FIntRect(0, 0, 32, 32),
			FIntRect(1, 1, 32, 32),
			FIntRect(4, 4, 28, 28),
			FIntRect(4, 4, 29, 29),
			FIntRect(1, 1, 32, 32),
			FIntRect(4, 4, 29, 29),
			FIntRect(1, 1, 32, 32),
			FIntRect(8, 8, 24, 24),
			FIntRect(1, 1, 32, 32),
			FIntRect(4, 4, 28, 28),
			FIntRect(4, 4, 29, 29),
			FIntRect(1, 1, 32, 32),
			FIntRect(1, 1, 32, 32),
			FIntRect(1, 1, 31, 31),
			FIntRect(1, 1, 31, 31),
			FIntRect(1, 1, 31, 31),
			FIntRect(1, 1, 31, 31),
			FIntRect(1, 1, 31, 31),
			FIntRect(1, 1, 31, 31),
			FIntRect(1, 1, 31, 31),
			FIntRect(1, 1, 31, 31),
			FIntRect(1, 1, 31, 31),
			FIntRect(1, 1, 31, 31),
			FIntRect(1, 1, 31, 31),
		};
		return Bounds[FMath::Clamp(Shape, 0, UE_ARRAY_COUNT(Bounds) - 1)];
	}

	// Keep the original texture IDs for saved profiles. D/F/H are smaller variants of E/G/N;
	// size is now independent, so only their larger versions appear when cycling shapes.
	inline int32 NextCrosshair(int32 Current, int32 Delta) {
		static constexpr int32 Shapes[] = { 0, 1, 2, 4, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };
		if (Current == 3)
			Current = 4;
		else if (Current == 5)
			Current = 6;
		else if (Current == 7)
			Current = 13;
		int32 Index = 0;
		for (int32 I = 0; I < UE_ARRAY_COUNT(Shapes); ++I)
			if (Shapes[I] == Current)
				Index = I;
		const int32 Count = UE_ARRAY_COUNT(Shapes);
		return Shapes[((Index + Delta) % Count + Count) % Count];
	}
} // namespace DF
