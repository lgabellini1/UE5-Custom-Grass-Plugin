#pragma once

#include "Constants.h"

namespace CustomGrass
{
	UENUM(BlueprintType)
	enum class EGrassLOD : uint8
	{
		LOD0,
		LOD1,
		LOD2,
		NumLODs UMETA(Hidden)
	};
	
	constexpr int32 NumLODs = static_cast<int32>(EGrassLOD::NumLODs);
	
	UENUM(BlueprintType)
	enum class EClumpFacingType : uint8
	{
		NoClumpFacing,
		SameDirection,
		FaceClumpCenter,
		OppositeClumpCenter
	};
	
	struct FTileDebugInfoRT
	{
		int32 TileIndex;
		EGrassLOD LOD;
	};
	
	using FRTDebugState = TStaticArray<FTileDebugInfoRT, MaxRenderedTiles>;
}

ENUM_RANGE_BY_COUNT(CustomGrass::EGrassLOD, CustomGrass::EGrassLOD::NumLODs);

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
struct TRandomVariationValue<T>
{
	static_assert(TIsArithmetic<T>::Value,
		"T must be of arithmetic type");
	
	T Value;
	float VariationPercentage;
};
