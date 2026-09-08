#include "CustomGrassPrimitiveComponent.h"
#include "CustomGrassDataAsset.h"
#include "Rendering/CustomGrassSceneProxy.h"
#include "Rendering/CustomGrassShadowProxyComponent.h"
#include "CustomGrassSettings.h"
#include "CustomGrassWorldSubsystem.h"
#include "LandscapeComponent.h"
#include "Landscape.h"
#include "Utilities.h"

UCustomGrassPrimitiveComponent::UCustomGrassPrimitiveComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	Mobility = EComponentMobility::Stationary;

	SetCastShadow(true);
	bCastDynamicShadow = false;
	SetCastContactShadow(true);
}

void UCustomGrassPrimitiveComponent::Initialize(
	const FInitConfig& Config,
	ULandscapeComponent* AssociatedLandscapeTile,
	const UCustomGrassDataAsset& DataAsset)
{
	MaterialSet = Config.Material;

	check(AssociatedLandscapeTile);
	LandscapeTile = AssociatedLandscapeTile;
	TileIndex = Config.TileIndex;

	CreateShadowProxy(DataAsset);
}

void UCustomGrassPrimitiveComponent::UpdateRenderSettings(const UCustomGrassDataAsset& DataAsset)
{
	SetCastShadow(DataAsset.bShadowsEnabled);
	ShadowProxy->UpdateRenderSettings(DataAsset);

	MarkRenderStateDirty();
}

void UCustomGrassPrimitiveComponent::OnComponentCreated()
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UCustomGrassPrimitiveComponent::GetUsedMaterials(
	TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	OutMaterials.Append({ MaterialSet.Material, MaterialSet.MaterialNoTwoSides });
}

FPrimitiveSceneProxy* UCustomGrassPrimitiveComponent::CreateSceneProxy()
{
	const auto* WorldSubsystem = GetWorld()->GetSubsystem<UCustomGrassWorldSubsystem>();
	check(WorldSubsystem);
	
	return new FCustomGrassSceneProxy(this, WorldSubsystem->GetRenderSystem(), TileIndex);
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

void UCustomGrassPrimitiveComponent::CreateShadowProxy(const UCustomGrassDataAsset& DataAsset)
{
	auto* World = GetWorld();
	
	auto* ShadowProxy = UCustomGrassShadowProxyComponent::Make(
		UCustomGrassShadowProxyComponent::FInitConfig(
			DataAsset.ShadowProxyResolution,
			TileIndex,
			CreateShadowProxyMID(DataAsset)
		));

	ShadowProxy->RegisterComponentWithWorld(World);
	ShadowProxy->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	ShadowProxy->BuildMesh(LandscapeTile, &DataAsset);

		this->ShadowProxy = ShadowProxy;
	}
}

TNotNull<ULandscapeComponent*> UCustomGrassPrimitiveComponent::GetAssociatedLandscapeTile() const
{
	return LandscapeTile;
}

FCustomGrassMaterial UCustomGrassPrimitiveComponent::GetMaterial() const
{
	return MaterialSet;
}

UMaterialInstanceDynamic* UCustomGrassPrimitiveComponent::CreateShadowProxyMID(const UCustomGrassDataAsset& DataAsset)
{
	auto* ShadowProxyMID = UMaterialInstanceDynamic::Create(
		DataAsset.ShadowProxyMaterial, this
	);
	
	ShadowProxyMID->SetTextureParameterValue(
		TEXT("ShadowWPOTextureAtlas"), ShadowWPOTextureAtlas);
	ShadowProxyMID->SetScalarParameterValue(
		TEXT("AtlasGridSize"), GMaxRenderedTiles / 2);

	const auto* LandscapeActor = LandscapeTile->GetLandscapeActor();
	
	ShadowProxyMID->SetVectorParameterValue(
		TEXT("LandscapeWorldOrigin"), FLinearColor(LandscapeActor->GetActorLocation()));
	ShadowProxyMID->SetScalarParameterValue(
		TEXT("LandscapeWorldSize"), GetLandscapeExtentInWorldUnits(LandscapeActor).X);
	
	ShadowProxyMID->SetScalarParameterValue(
		TEXT("MaxGrassHeight"), GMaxGrassBladeHeight);	
		
	return ShadowProxyMID;	
}
