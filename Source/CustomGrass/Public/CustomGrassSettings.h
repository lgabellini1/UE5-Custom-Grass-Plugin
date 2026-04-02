#pragma once

#include "Engine/DeveloperSettings.h"
#include "CustomGrassSettings.generated.h"

class UCustomGrassDataAsset;

UCLASS(Config = CustomGrass, DefaultConfig)
class UCustomGrassSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCustomGrassSettings();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UCustomGrassDataAsset> GrassDataAsset;
};
