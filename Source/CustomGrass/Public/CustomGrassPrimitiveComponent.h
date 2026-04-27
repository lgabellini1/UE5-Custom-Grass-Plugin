// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CustomGrassPrimitiveComponent.generated.h"

class ULandscapeComponent;

UCLASS()
class UCustomGrassPrimitiveComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	explicit UCustomGrassPrimitiveComponent(const FObjectInitializer& ObjectInitializer);

	UPROPERTY()
	TObjectPtr<const ULandscapeComponent> LandscapeTile;
	
	TObjectPtr<const ULandscapeComponent> GetLandscapeTile() const { return LandscapeTile; }

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material;

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;

protected:
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
};
