#pragma once

#include "CoreMinimal.h"
#include "Types.h"
#include "Rendering/RenderTypes.h"
#include "CustomGrassDataAsset.generated.h"

namespace CustomGrass
{
	DECLARE_MULTICAST_DELEGATE(FOnDataAssetLoaded);
	inline FOnDataAssetLoaded DataAssetLoaded;

	DECLARE_MULTICAST_DELEGATE(FOnDataAssetValuesChanged)
	inline FOnDataAssetValuesChanged DataAssetValuesChanged;
}

// Reflection-compatible version of the TRandomVariationValue struct for the editor.
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

UCLASS()
class UCustomGrassDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
	UPROPERTY(EditAnywhere, Category="Appearance")
	TObjectPtr<UMaterialInterface> GrassMaterial = nullptr;

	UPROPERTY(EditAnywhere, Category="Appearance")
	FRandomVariationFloatProperty Height;

	UPROPERTY(EditAnywhere, Category="Appearance")
	FRandomVariationFloatProperty Width;

	UPROPERTY(EditAnywhere, Category="Appearance")
	FRandomVariationFloatProperty Tilt;

	UPROPERTY(EditAnywhere, Category="Appearance")
	FRandomVariationFloatProperty Bend;


	UPROPERTY(EditAnywhere, Category="Appearance|Clumps")
	FRandomVariationFloatProperty ClumpStrength;

	UPROPERTY(EditAnywhere, Category="Appearance|Clumps", Meta=(ClampMin="0"))
	int ClumpGridSize = 25;

	UPROPERTY(EditAnywhere, Category="Appearance|Clumps")
	CustomGrass::EClumpFacingType ClumpFacingType;
	
	UPROPERTY(EditAnywhere, Category="Appearance|Clumps", meta=(ClampMin="0.0", ClampMax="1.0",
		EditCondition="ClumpFacingType != CustomGrass::EClumpFacingType::NoClumpFacing"))
	float ClumpFacingStrength;

	
	UPROPERTY(EditAnywhere, Category="Appearance")
	float ShortHeightThreshold = 0.f;

	
	UPROPERTY(EditAnywhere, Category="Appearance|Wind", DisplayName="Animation Texture")
	TObjectPtr<UTexture2D> NoiseTexture = nullptr;

	UPROPERTY(EditAnywhere, Category="Appearance|Wind", DisplayName="Direction")
	FVector2f WindDirection = FVector2f::ZeroVector;
	
	UPROPERTY(EditAnywhere, Category="Appearance|Wind", meta=(ClampMin="0"), DisplayName="Strength")
	float WindStrength = 0.f;

	
	UPROPERTY(EditAnywhere, Category="Rendering")
	float ViewSpaceCorrection = 0.f;

	UPROPERTY(EditAnywhere, Category="Rendering")
	float NormalRoundnessStrength = 0.f;

	UPROPERTY(EditAnywhere, Category="Rendering")
	float MaxRenderDistance = 1000.f;

	UPROPERTY(EditAnywhere, Category="Rendering")
	float TilePriorityDistancePenalty = 0.001f;

	UPROPERTY(EditAnywhere,	Category="Rendering", DisplayName="Grass Material (No Two-sided)")
	TObjectPtr<UMaterialInterface> GrassMaterial_NoTwoSided = nullptr;

	UPROPERTY(EditAnywhere, Category="Rendering")
	bool bFixedLOD = false;

	UPROPERTY(EditAnywhere, Category="Rendering", meta=(
		EditCondition="bFixedLOD == true"))
	CustomGrass::EGrassLOD GlobalLOD = CustomGrass::EGrassLOD::LOD0;
	
	
	UPROPERTY(EditAnywhere, Category="Rendering|Shadows")
	bool bShadowsEnabled = true;

	UPROPERTY(EditAnywhere, Category="Rendering|Shadows")
	TObjectPtr<UMaterialInterface> ShadowProxyMaterial = nullptr;

	UPROPERTY(EditAnywhere, Category="Rendering|Shadows")
	bool bDebugShowShadowProxyMesh = false;

	UPROPERTY(EditAnywhere, Category="Rendering|Shadows")
	float ShadowProxyZOffset = 20.f;

	UPROPERTY(EditAnywhere, Category="Rendering|Shadows")
	int32 ShadowProxyResolution = 64;
};
