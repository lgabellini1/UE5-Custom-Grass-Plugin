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
	
	SetCastShadow(true);
	bCastDynamicShadow = true;
	bCastStaticShadow = true;
//	bUseAsOccluder = true;

	bAffectDynamicIndirectLighting = false;
	bAffectDistanceFieldLighting = false;
	bNeverDistanceCull = true;
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
	
	return new FCustomGrassSceneProxy(this, WorldSubsystem->GetRenderSystem_GameThread());
}

FBoxSphereBounds UCustomGrassPrimitiveComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	check(LandscapeTile);
	const FBox& TileBox = LandscapeTile->Bounds.GetBox();

	const UCustomGrassSettings* Settings = GetDefault<UCustomGrassSettings>();

	FBox ExpandedBox = TileBox;
	ExpandedBox = ExpandedBox.ExpandBy(FVector(1000, 1000, 1000));
	if (const auto* GrassDataAsset = Settings->GrassDataAsset.LoadSynchronous())
	{
		ExpandedBox.Max.Z += GrassDataAsset->Height * 1.5f;
	}
	
	return FBoxSphereBounds(ExpandedBox);
}
