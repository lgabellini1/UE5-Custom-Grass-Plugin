// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CustomGrassWorldSubsystem.h"

// struct FWindParams;
class UCustomGrassPrimitiveComponent;
class FCustomGrassRenderSystem;
class FCustomGrassVertexFactory;

/**
 * Landscape information needed for rendering. The proxy maintains a
 * copy of data originally stored by the ULandscapeComponent in the game thread.
 */
struct FProxyLandscapeData
{
	FTextureRHIRef HeightmapTexture;
	FSamplerStateRHIRef HeightmapSampler;
	FVector4f HeightmapScaleBias;
	
	int32 ComponentSizeQuads;
	
	FIntPoint SectionBase;

	FVector3f BoundingBox;
	
	FMatrix44f LocalToWorld;
};

FVector3f GetTileCenter(const FProxyLandscapeData& LandscapeData);

FVector3f GetTileExtent(const FProxyLandscapeData& LandscapeData);


class FCustomGrassSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FCustomGrassSceneProxy(const UCustomGrassPrimitiveComponent* InComponent,
		FCustomGrassRenderSystem* InRenderSystem);

protected:
	virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;

	virtual void DestroyRenderThreadResources() override;
	
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	
	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override;
	
	virtual SIZE_T GetTypeHash() const override;

	virtual uint32 GetMemoryFootprint() const override;

	
	FCustomGrassRenderSystem* RenderSystem;

	TSharedPtr<FRenderingResourceHandles, ESPMode::ThreadSafe> ResourceHandles;

	/** Render-thread copy of landscape data useful to shaders. */
	FProxyLandscapeData LandscapeData;

	
	FCustomGrassVertexFactory* VertexFactory;
	
	FMaterialRenderProxy* MaterialProxy;
	FMaterialRelevance MaterialRelevance;
};
