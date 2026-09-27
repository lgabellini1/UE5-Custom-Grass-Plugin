#include "CustomGrassPrimitiveComponent.h"
#include "CustomGrassWorldSubsystem.h"
#include "CustomGrassDataAsset.h"
#include "Rendering/CustomGrassSceneProxy.h"
#include "Rendering/CustomGrassShadowProxyComponent.h"
#include "Landscape.h"

UCustomGrassPrimitiveComponent::UCustomGrassPrimitiveComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	Mobility = EComponentMobility::Stationary;

	bCastDynamicShadow = false;
	bCastContactShadow = true;
}

void UCustomGrassPrimitiveComponent::Initialize(
	const FInitConfig& Config,
	ULandscapeComponent& AssociatedLandscapeTile,
	const UCustomGrassWorldSubsystem& WorldSubsystem)
{
	LandscapeTile = &AssociatedLandscapeTile;
	
	Material  = Config.Material;
	TileIndex = Config.TileIndex;

	RegisterComponentWithWorld(&WorldSubsystem.GetWorldRef());
	AttachToComponent(LandscapeTile, FAttachmentTransformRules::KeepRelativeTransform);

	SetCastShadow(WorldSubsystem.GetDataAsset().bShadowsEnabled);
	CreateShadowProxy(WorldSubsystem.GetDataAsset());
}

void UCustomGrassPrimitiveComponent::UpdateRenderSettings(const UCustomGrassDataAsset& DataAsset)
{
	SetCastShadow(DataAsset.bShadowsEnabled);
	ShadowProxy->UpdateRenderSettings(DataAsset);

	MarkRenderStateDirty();
}

void UCustomGrassPrimitiveComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UCustomGrassPrimitiveComponent::GetUsedMaterials(
	TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	OutMaterials.Append({ Material.TwoSided, Material.NoTwoSided });
}

FPrimitiveSceneProxy* UCustomGrassPrimitiveComponent::CreateSceneProxy()
{
	const auto WorldSubsystem =
		GetWorld()->GetSubsystemChecked<UCustomGrassWorldSubsystem>();
	
	return new FCustomGrassSceneProxy(*this, WorldSubsystem->GetRenderSystem(), TileIndex);
}

FBoxSphereBounds UCustomGrassPrimitiveComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const FBox& TileBoundingBox = LandscapeTile->GetLandscapeInfo()->GetLoadedBounds();

	const auto WorldSubsystem =
		GetWorld()->GetSubsystemChecked<UCustomGrassWorldSubsystem>();
	const auto& GrassDataAsset = WorldSubsystem->GetDataAsset();
	
	constexpr float VerticalOffset = 100.f;

	return FBoxSphereBounds(TileBoundingBox.ExpandBy(FVector::UnitZ() * VerticalOffset,
		FVector::UnitZ() * (GrassDataAsset.Height.Value + VerticalOffset)));
}

void UCustomGrassPrimitiveComponent::CreateShadowProxy(const UCustomGrassDataAsset& DataAsset)
{
	ShadowProxy = UCustomGrassShadowProxyComponent::Make(DataAsset, TileIndex, *this);

	ShadowProxy->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	ShadowProxy->RegisterComponentWithWorld(GetWorld());
	ShadowProxy->UpdateRenderSettings(DataAsset);
}
