// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shared.h"
#include "CustomGrassDataAsset.generated.h"

UENUM(BlueprintType)
enum class EClumpFacingType : uint8
{
	NoClumpFacing,
	SameDirection,
	FaceClumpCenter,
	OppositeClumpCenter
};

DECLARE_MULTICAST_DELEGATE(FOnGrassDataAssetLoaded);
inline FOnGrassDataAssetLoaded GrassDataAssetLoaded;

UCLASS()
class UCustomGrassDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Appearance")
	TObjectPtr<UMaterialInterface> GrassMaterial = nullptr;

	// "ClampMax" value must match "GMaxGrassBladeHeight"!
	UPROPERTY(EditAnywhere, Category="Appearance", meta=(ClampMin="0.0", ClampMax="50.0"))
	float Height = 15.f;

	UPROPERTY(EditAnywhere, Category="Appearance", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RandomizeHeight = 0.f;

	// "ClampMax" value must match "GMaxGrassBladeWidth"!
	UPROPERTY(EditAnywhere, Category="Appearance", meta=(ClampMin="0.0", ClampMax="2.0"))
	float Width = 1.f;

	UPROPERTY(EditAnywhere, Category="Appearance", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RandomizeWidth = 0.f;

	// "ClampMax" value must match "GMaxGrassBladeTilt"!
	UPROPERTY(EditAnywhere, Category="Appearance", meta=(ClampMin="0.0", ClampMax="10.0"))
	float Tilt = 1.f;

	UPROPERTY(EditAnywhere, Category="Appearance", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RandomizeTilt = 0.f;

	// "ClampMax" value must match "GMaxGrassBladeBend"!
	UPROPERTY(EditAnywhere, Category="Appearance", meta=(ClampMin="0.0", ClampMax="10.0"))
	float Bend = 0.f;

	UPROPERTY(EditAnywhere, Category="Appearance", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RandomizeBend = 0.f;

	
	UPROPERTY(EditAnywhere, Category="Appearance|Clumps", Meta=(ClampMin="0.0", ClampMax="1.0"))
	float ClumpStrength = 0.085f;

	UPROPERTY(EditAnywhere, Category="Appearance|Clumps", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RandomizeClumpStrength = 0.f;

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
	bool bManualLOD = false;

	UPROPERTY(EditAnywhere, Category="Rendering", meta=(
		EditCondition="bManualLOD == true"))
	EGrassLOD GrassLOD = EGrassLOD::LOD0;
	
	
	UPROPERTY(EditAnywhere, Category="Rendering|Shadows")
	bool bShadowsEnabled = true;

	UPROPERTY(EditAnywhere, Category="Rendering|Shadows")
	TObjectPtr<UMaterialInterface> ShadowProxyMaterial = nullptr;

	UPROPERTY(EditAnywhere, Category="Rendering|Shadows")
	bool bDebugShowProxyMesh = false;

	UPROPERTY(EditAnywhere, Category="Rendering|Shadows")
	float ZOffset = 20.f;

	UPROPERTY(EditAnywhere, Category="Rendering|Shadows")
	int32 ProxyResolution = 64;
};
