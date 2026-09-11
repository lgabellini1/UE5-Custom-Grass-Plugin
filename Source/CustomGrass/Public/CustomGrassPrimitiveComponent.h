#pragma once

#include "CoreMinimal.h"
#include "Types.h"
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
		FCustomGrassMaterial Material;
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

	TNotNull<ULandscapeComponent*> GetAssociatedLandscapeTile() const;
	FCustomGrassMaterial GetMaterial() const;

protected:
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	UPROPERTY()
	TObjectPtr<ULandscapeComponent> LandscapeTile;
	
	UPROPERTY()
	FCustomGrassMaterial MaterialSet;

	int32 TileIndex = -1;

	UPROPERTY()
	TObjectPtr<UCustomGrassShadowProxyComponent> ShadowProxy;
	
	void CreateShadowProxy(const UCustomGrassDataAsset& DataAsset);
	UMaterialInstanceDynamic* CreateShadowProxyMID(const UCustomGrassDataAsset& DataAsset);
};
