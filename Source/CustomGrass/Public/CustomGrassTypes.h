#pragma once

#include "CoreMinimal.h"
#include "CustomGrassTypes.generated.h"

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
	inline constexpr int32 GNumLODs = static_cast<int32>(ECustomGrassLOD::NumLODs);
}


UENUM(BlueprintType)
enum class EClumpFacingType : uint8
{
	NoClumpFacing,
	SameDirection,
	FaceClumpCenter,
	OppositeClumpCenter
};


USTRUCT(BlueprintType)
struct FCustomGrassMaterial
{
	GENERATED_BODY()
	
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TwoSided;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> NoTwoSided;

	explicit operator bool() const { return TwoSided && NoTwoSided; }
};
