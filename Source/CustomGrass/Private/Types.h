#pragma once

UENUM(BlueprintType)
enum class EClumpFacingType : uint8
{
	NoClumpFacing,
	SameDirection,
	FaceClumpCenter,
	OppositeClumpCenter
};

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
