#include "CustomGrassDataAsset.h"

#if WITH_EDITOR
void UCustomGrassDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	CustomGrass::DataAssetValuesChanged.Broadcast();
}
#endif
