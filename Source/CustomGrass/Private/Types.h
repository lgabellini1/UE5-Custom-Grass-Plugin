#pragma once

UENUM(BlueprintType)
enum class EClumpFacingType : uint8
{
	NoClumpFacing,
	SameDirection,
	FaceClumpCenter,
	OppositeClumpCenter
};

struct FCustomGrassMaterial
{
	UMaterialInterface* Material;
	UMaterialInterface* MaterialNoTwoSides;

	explicit operator bool() const
	{
		return Material && MaterialNoTwoSides;
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

// Reflected version of the above struct for the editor.
USTRUCT(BlueprintType)
struct FRandomVariationFloatProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	float Value;

	UPROPERTY(EditAnywhere, meta=(ClampMin="0.0", ClampMax="1.0"))
	float VariationPercentage;

	TRandomVariationValue<float> ToValue() const
	{
		return TRandomVariationValue(Value, VariationPercentage);
	}
};
