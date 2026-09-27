#pragma once

#include "CoreMinimal.h"
#include "CustomGrassTypes.h"
#include "CustomGrassPrimitiveComponent.generated.h"

class UCustomGrassWorldSubsystem;
class UCustomGrassShadowProxyComponent;
class UCustomGrassDataAsset;
class ULandscapeComponent;

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
	
	void Initialize(
		const FInitConfig& Config,
		ULandscapeComponent& AssociatedLandscapeTile,
		const UCustomGrassWorldSubsystem& WorldSubsystem);

	virtual void OnComponentCreated() override;

	void UpdateRenderSettings(const UCustomGrassDataAsset& DataAsset);

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials,
		bool bGetDebugMaterials = false) const override;

	ULandscapeComponent& GetAssociatedLandscapeTile() const { return *LandscapeTile; }

	FCustomGrassMaterial GetCustomGrassMaterial() const { return Material; }

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
