#pragma once

#include "CoreMinimal.h"

UENUM(BlueprintType)
enum class EClumpFacingType : uint8
{
	NoClumpFacing,
	SameDirection,
	FaceClumpCenter,
	OppositeClumpCenter
};

/**
 * A ceil on the number of rendered tiles. This lets us avoid unexpected memory
 * blow-ups while avoiding costly dynamic resizing of the buffers on each frame.
 */
static constexpr int32 GMaxRenderedTiles = 4;

static constexpr float GMaxGrassBladeHeight = 50.f;
static constexpr float GMaxGrassBladeWidth  = 2.f;
static constexpr float GMaxGrassBladeTilt   = 10.f;
static constexpr float GMaxGrassBladeBend   = 10.f;

struct FCustomGrassMaterial
{
	UMaterialInterface* Material;
	UMaterialInterface* MaterialNoTwoSides;

	explicit operator bool() const
	{
		return Material != nullptr && MaterialNoTwoSides != nullptr;
	}
};
