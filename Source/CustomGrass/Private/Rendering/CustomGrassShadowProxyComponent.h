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
	explicit UCustomGrassShadowProxyComponent(const FObjectInitializer& ObjectInitializer);
	
	static UCustomGrassShadowProxyComponent* Make(
		const UCustomGrassDataAsset& DataAsset,
		int32 TileIndex,
		const UCustomGrassPrimitiveComponent& ParentGrassTile);

	void UpdateRenderSettings(const UCustomGrassDataAsset& DataAsset);

protected:
	void BuildMesh();

	static UMaterialInstanceDynamic* CreateMID(const UCustomGrassDataAsset& DataAsset,
		const UCustomGrassPrimitiveComponent& ParentGrassTile);

	int32 PlaneResolution = 64;

	UPROPERTY()
	TObjectPtr<const UCustomGrassPrimitiveComponent> ParentGrassTile;
};

class FCustomGrassShadowProxyMeshBuilder
{
public:
	FCustomGrassShadowProxyMeshBuilder(
		ULandscapeComponent& LandscapeTile, 
		int32 MeshResolution);
	
	UStaticMesh* Build(UObject* Outer) const;

private:
	UStaticMeshDescription* BuildMeshDescription(
		const UStaticMesh& Mesh,
		UObject* Outer) const;

	ULandscapeComponent& LandscapeTile;
	int32 MeshResolution;
};
