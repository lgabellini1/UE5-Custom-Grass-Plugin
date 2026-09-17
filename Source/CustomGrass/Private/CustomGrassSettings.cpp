#include "CustomGrassSettings.h"
#include "CustomGrassDataAsset.h"

UCustomGrassSettings::UCustomGrassSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName  = TEXT("CustomGrass");	
}

void UCustomGrassSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.GetPropertyName() ==
			GET_MEMBER_NAME_CHECKED(UCustomGrassSettings, GrassDataAsset))
		{
			CustomGrass::DataAssetLoaded.Broadcast();
		}
	}
}
