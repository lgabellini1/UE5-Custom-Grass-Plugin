// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CustomGrassPrimitiveComponent.generated.h"

class UCustomGrassShadowProxyComponent;
class ULandscapeComponent;

UCLASS()
class UCustomGrassPrimitiveComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	explicit UCustomGrassPrimitiveComponent(const FObjectInitializer& ObjectInitializer);

	virtual void OnComponentCreated() override;

	void SetIndex(int32 InIndex) { Index = InIndex; }

	UPROPERTY()
	TObjectPtr<const ULandscapeComponent> LandscapeTile;
	
	TObjectPtr<const ULandscapeComponent> GetLandscapeTile() const { return LandscapeTile; }

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material_NoTwoSide;

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials,
		bool bGetDebugMaterials = false) const override;

	UPROPERTY()
	TObjectPtr<UCustomGrassShadowProxyComponent> ShadowProxy;

protected:
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	int32 Index;
};
