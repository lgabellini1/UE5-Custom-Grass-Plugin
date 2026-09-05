#pragma once

#include "CoreMinimal.h"
#include "CustomGrassPrimitiveComponent.generated.h"

class ULandscapeComponent;
class UCustomGrassShadowProxyComponent;
class UCustomGrassDataAsset;

UCLASS()
class UCustomGrassPrimitiveComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	struct FInitConfig
	{
		UMaterialInterface* Material;
		// Same as above, but with two-sided rendering disabled.
		UMaterialInterface* Material_NoTwoSides;
		int32 TileIndex;
	};
	
	explicit UCustomGrassPrimitiveComponent(const FObjectInitializer& ObjectInitializer);
	
	void Initialize(const FInitConfig& Config, ULandscapeComponent* AssociatedLandscapeTile,
		const UCustomGrassDataAsset& DataAsset);

	void UpdateRenderSettings(const UCustomGrassDataAsset& DataAsset);

	virtual void OnComponentCreated() override;
	
	UPROPERTY()
	TObjectPtr<ULandscapeComponent> LandscapeTile;
	
	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material_NoTwoSide;

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials,
		bool bGetDebugMaterials = false) const override;

protected:
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	
	void CreateShadowProxy(const UCustomGrassDataAsset& DataAsset);
	UMaterialInstanceDynamic* CreateShadowProxyMID(const UCustomGrassDataAsset& DataAsset);

	UPROPERTY()
	TObjectPtr<UCustomGrassShadowProxyComponent> ShadowProxy;

	int32 TileIndex;
};
