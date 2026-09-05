#include "CustomGrassDataAsset.h"

#if WITH_EDITOR
void UCustomGrassDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	GrassDataAssetValuesChanged.Broadcast();
}
#endif
