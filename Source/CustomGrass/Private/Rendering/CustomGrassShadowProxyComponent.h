#pragma once

#include "CoreMinimal.h"
#include "CustomGrassShadowProxyComponent.generated.h"

class ULandscapeComponent;
class UCustomGrassDataAsset;
class UCustomGrassPrimitiveComponent;

UCLASS()
class UCustomGrassShadowProxyComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	struct FInitConfig
	{
		int32 PlaneMeshResolution;
		int32 TileIndex;
		UMaterialInterface* Material;
	};
	
	explicit UCustomGrassShadowProxyComponent(const FObjectInitializer& ObjectInitializer);
	static UCustomGrassShadowProxyComponent* Make(
		const FInitConfig& Config,
		const UCustomGrassPrimitiveComponent* ParentGrassTile);

	void UpdateRenderSettings(const UCustomGrassDataAsset& DataAsset);

	void BuildMesh(const UCustomGrassDataAsset& DataAsset);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 PlaneResolution = 64;

	UPROPERTY()
	TObjectPtr<const UCustomGrassPrimitiveComponent> ParentGrassTile;
};
