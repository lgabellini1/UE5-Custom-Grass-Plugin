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
		ULandscapeComponent& AssociatedLandscapeTile;
		int32 TileIndex;
	};
	
	void Initialize(const FInitConfig& Config, const UCustomGrassWorldSubsystem& WorldSubsystem);

	virtual void OnComponentCreated() override;

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials,
		bool bGetDebugMaterials = false) const override;

	void UpdateRenderSettings(const UCustomGrassDataAsset& DataAsset);

	ULandscapeComponent* GetAssociatedLandscapeTile() const { return LandscapeTile; }

	FCustomGrassMaterial GetCustomGrassMaterial() const { return Material; }

protected:
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	int32 TileIndex = INDEX_NONE;

	UPROPERTY()
	TObjectPtr<ULandscapeComponent> LandscapeTile;

	UPROPERTY()
	FCustomGrassMaterial Material;

	UPROPERTY()
	TObjectPtr<UCustomGrassShadowProxyComponent> ShadowProxy;
	void CreateShadowProxy(const UCustomGrassDataAsset& DataAsset);
};
