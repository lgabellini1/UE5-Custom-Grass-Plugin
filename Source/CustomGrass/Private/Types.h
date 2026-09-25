#pragma once

#include "Constants.h"
#include "Types.generated.h"

UENUM(BlueprintType)
enum class ECustomGrassLOD : uint8
{
	LOD0,
	LOD1,
	LOD2,
	NumLODs UMETA(Hidden)
};

ENUM_RANGE_BY_COUNT(ECustomGrassLOD, ECustomGrassLOD::NumLODs);

namespace CustomGrass
{
	constexpr int32 NumLODs = static_cast<int32>(ECustomGrassLOD::NumLODs);
}

UENUM(BlueprintType)
enum class EClumpFacingType : uint8
{
	NoClumpFacing,
	SameDirection,
	FaceClumpCenter,
	OppositeClumpCenter
};

namespace CustomGrass
{
	struct FTileDebugInfoRT
	{
		FPrimitiveComponentId TileComponentId;
		ECustomGrassLOD LOD;
	};
	
	using FRTDebugState = TStaticArray<FTileDebugInfoRT, MaxRenderedTiles>;

	struct FTextureRenderTargetsGT
	{
		UTextureRenderTarget2D* ShadowMapTextureAtlas;
	};
}

USTRUCT()
struct FCustomGrassMaterial
{
	GENERATED_BODY()
	
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TwoSided;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> NoTwoSided;

	explicit operator bool() const
	{
		return TwoSided && NoTwoSided;
	}
};

template <class T>
struct TRandomVariationValue
{
	static_assert(TIsArithmetic<T>::Value,
		"T must be of arithmetic type");
	
	T Value;
	float VariationPercentage;
};
