#pragma once

#include "CoreMinimal.h"
#include "CustomGrassShadowProxyComponent.generated.h"

class ULandscapeComponent;
class UCustomGrassDataAsset;

UCLASS()
class UCustomGrassShadowProxyComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	explicit UCustomGrassShadowProxyComponent(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 PlaneResolution = 64;

	void BuildMesh(ULandscapeComponent* LandscapeComponent,
		const UCustomGrassDataAsset* DataAsset);
};
