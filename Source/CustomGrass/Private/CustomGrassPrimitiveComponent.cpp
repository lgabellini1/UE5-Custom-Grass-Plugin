// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomGrassPrimitiveComponent.h"
#include "CustomGrassSceneProxy.h"
#include "CustomGrassSettings.h"
#include "CustomGrassWorldSubsystem.h"
#include "LandscapeComponent.h"

UCustomGrassPrimitiveComponent::UCustomGrassPrimitiveComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	/*
	SetCastShadow(true);
	bCastDynamicShadow = true;
	bCastStaticShadow = true;
	bUseAsOccluder = true;
	*/

	Mobility = EComponentMobility::Static;
}

void UCustomGrassPrimitiveComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	OutMaterials.Add(Material);
}

FPrimitiveSceneProxy* UCustomGrassPrimitiveComponent::CreateSceneProxy()
{
	const auto* WorldSubsystem = GetWorld()->GetSubsystem<UCustomGrassWorldSubsystem>();
	check(WorldSubsystem);
	
	return new FCustomGrassSceneProxy(this, WorldSubsystem->GetRenderSystem());
}

FBoxSphereBounds UCustomGrassPrimitiveComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	check(LandscapeTile);
	const FBox& TileBox = LandscapeTile->GetLandscapeInfo()->GetLoadedBounds();

	const UCustomGrassSettings* Settings = GetDefault<UCustomGrassSettings>();
	const UCustomGrassDataAsset* GrassDataAsset = Settings->GrassDataAsset.LoadSynchronous();

	FBox ExpandedBox = TileBox;
	float VerticalOffset = 100.f;
	ExpandedBox.Max.Z += (GrassDataAsset ? GrassDataAsset->Height : 0.f) + VerticalOffset;
	ExpandedBox.Min.Z -= VerticalOffset;
	
	return FBoxSphereBounds(ExpandedBox);
}
