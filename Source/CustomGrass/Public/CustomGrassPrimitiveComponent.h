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
	
	ULandscapeComponent* GetAssociatedTile() const { return LandscapeTile.Get(); }

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material;

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;

	UPROPERTY()
	TObjectPtr<ULandscapeComponent> LandscapeTile;

protected:
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
};
