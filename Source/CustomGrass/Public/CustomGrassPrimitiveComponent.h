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
	explicit UCustomGrassPrimitiveComponent(const FObjectInitializer& ObjectInitializer);

	struct FInitConfig
	{
		FCustomGrassMaterial Material;
		int32 TileIndex;
	};
	
	void Initialize(const FInitConfig& Config, ULandscapeComponent& AssociatedLandscapeTile,
		const UCustomGrassDataAsset& DataAsset);

	virtual void OnComponentCreated() override;

	void UpdateRenderSettings(const UCustomGrassDataAsset& DataAsset);

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials,
		bool bGetDebugMaterials = false) const override;

	ULandscapeComponent& GetAssociatedLandscapeTile() const { return *LandscapeTile; }

	FCustomGrassMaterial GetMaterial() const { return Material; }

protected:
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	UPROPERTY()
	TObjectPtr<ULandscapeComponent> LandscapeTile;

	UPROPERTY()
	FCustomGrassMaterial Material;

	int32 TileIndex = INDEX_NONE;

	UPROPERTY()
	TObjectPtr<UCustomGrassShadowProxyComponent> ShadowProxy;
	void CreateShadowProxy(const UCustomGrassDataAsset& DataAsset);
};
