#pragma once

#include "CoreMinimal.h"
#include "Utilities.h"
#include "Rendering/CustomGrassRenderTypes.h"
#include "CustomGrassDataAsset.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnGrassDataAssetLoaded);
inline FOnGrassDataAssetLoaded GrassDataAssetLoaded;

DECLARE_MULTICAST_DELEGATE(FOnGrassDataAssetValuesChanged)
inline FOnGrassDataAssetValuesChanged GrassDataAssetValuesChanged;

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
	EClumpFacingType ClumpFacingType;
	
	UPROPERTY(EditAnywhere, Category="Appearance|Clumps", meta=(ClampMin="0.0", ClampMax="1.0",
		EditCondition="ClumpFacingType != EClumpFacingType::NoClumpFacing"))
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

	UPROPERTY(EditAnywhere,	Category="Rendering", DisplayName="Grass Material (No TwoSided)")
	TObjectPtr<UMaterialInterface> GrassMaterial_NoTwoSided = nullptr;

	UPROPERTY(EditAnywhere, Category="Rendering")
	bool bFixedLOD = false;

	UPROPERTY(EditAnywhere, Category="Rendering", meta=(
		EditCondition="bFixedLOD == true"))
	EGrassLOD GlobalLOD = EGrassLOD::LOD0;
	
	
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
