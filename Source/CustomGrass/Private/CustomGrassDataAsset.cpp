#include "CustomGrassDataAsset.h"
#include "CustomGrassTypesInternal.h"

TRandomVariationValue<float> FRandomVariationFloatProperty::ToValue() const
{
	return TRandomVariationValue(Value, VariationPercentage);
}

#if WITH_EDITOR
void UCustomGrassDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	CustomGrass::DataAssetValuesChanged.Broadcast();
}
#endif
