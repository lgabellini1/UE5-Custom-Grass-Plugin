// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomGrassSettings.h"
#include "CustomGrass.h"

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
		if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UCustomGrassSettings, GrassDataAsset))
		{
			OnGrassDataAssetLoadDelegate.Broadcast();
		}
	}
}
