// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CustomGrassDataAsset.generated.h"

UENUM(BlueprintType)
enum class EClumpFacingType : uint8
{
	NoClumpFacing,
	SameDirection,
	FaceClumpCenter,
	OppositeClumpCenter
};

static constexpr float GMaxGrassBladeHeight = 50.f;

static constexpr float GMaxGrassBladeWidth = 2.f;

static constexpr float GMaxGrassBladeTilt = 10.f;

static constexpr float GMaxGrassBladeBend = 10.f;

UCLASS()
class UCustomGrassDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Material")
	TObjectPtr<UMaterialInterface> Material = nullptr;

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

	
	UPROPERTY(EditAnywhere, Category="Appearance | Clumps", Meta=(ClampMin="0.0", ClampMax="1.0"))
	float ClumpStrength = 0.085f;

	UPROPERTY(EditAnywhere, Category="Appearance | Clumps", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RandomizeClumpStrength = 0.f;

	UPROPERTY(EditAnywhere, Category="Appearance | Clumps", Meta=(ClampMin="0"))
	int ClumpGridSize = 25;

	UPROPERTY(EditAnywhere, Category="Appearance | Clumps")
	EClumpFacingType ClumpFacingType;
	
	UPROPERTY(EditAnywhere, Category="Appearance | Clumps", meta=(ClampMin="0.0", ClampMax="1.0",
		EditCondition="ClumpFacingType != EClumpFacingType::NoClumpFacing"))
	float ClumpFacingStrength;

	
	UPROPERTY(EditAnywhere, Category="Appearance")
	float ShortHeightThreshold = 0.f;

	
	UPROPERTY(EditAnywhere, Category="Appearance | Wind", DisplayName="Animation Texture")
	TObjectPtr<UTexture2D> NoiseTexture = nullptr;

	UPROPERTY(EditAnywhere, Category="Appearance | Wind", DisplayName="Direction")
	FVector2f WindDirection = FVector2f::ZeroVector;
	
	UPROPERTY(EditAnywhere, Category="Appearance | Wind", meta=(ClampMin="0"), DisplayName="Strength")
	float WindStrength = 0.f;

	
	UPROPERTY(EditAnywhere, Category="Rendering")
	float ViewSpaceCorrection = 0.f;

	UPROPERTY(EditAnywhere, Category="Rendering")
	float NormalRoundnessStrength = 0.f;

	UPROPERTY(EditAnywhere, Category="Rendering")
	float MaxRenderDistance = 1000.f;
};
