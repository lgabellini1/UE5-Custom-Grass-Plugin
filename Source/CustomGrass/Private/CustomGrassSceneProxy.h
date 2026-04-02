// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CustomGrassWorldSubsystem.h"

struct FWindParams;
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
	FMatrix44f LocalToWorld;
	FMatrix44f WorldToLocal;
};


/**
 * Static references to resources needed by proxy for
 * rendering. The memory these refs point to is expected to be
 * completely handled by the render system.
 */
struct FRenderingResourceHandles
{
	FShaderResourceViewRHIRef InstanceData;
	FBufferRHIRef IndirectDrawArgs;
	int32 TileOffset;
	FWindParams WindParams;
	float ViewSpaceCorrection;
	float NormalRoundnessStrength;
	float ShortHeightThreshold;
};	


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

	TSharedPtr<FRenderingResourceHandles> ResourceHandles;

	/** Render-thread copy of useful landscape data. */
	FProxyLandscapeData LandscapeData;

	
	// VF and material proxy roughly represent respectively the vertex and
	// pixel shader for this mesh.
	
	FCustomGrassVertexFactory* VertexFactory = nullptr;
	
	FMaterialRenderProxy* MaterialProxy = nullptr;
 
	FMaterialRelevance MaterialRelevance;
};
