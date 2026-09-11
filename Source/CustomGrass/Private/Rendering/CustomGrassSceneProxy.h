#pragma once

#include "CoreMinimal.h"
#include "RenderTypes.h"

// struct FWindParams;
class UCustomGrassPrimitiveComponent;
class FCustomGrassRenderSystem;
class FCustomGrassVertexFactory;

FVector GetTileCenter(const FProxyLandscapeData& LandscapeData);

FVector GetTileExtent(const FProxyLandscapeData& LandscapeData);

FVector GetClosestPointToTile(const FSceneView* View, const FProxyLandscapeData& LandscapeData);


class FCustomGrassSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FCustomGrassSceneProxy(const UCustomGrassPrimitiveComponent* InComponent,
		FCustomGrassRenderSystem* InRenderSystem, int32 Index);

	EGrassLOD GetGrassLOD() const { return CachedLOD.load(); }

	void StampNextFrame_RenderThread() const { FrameStamp = GFrameCounterRenderThread + 1; }

	const int32 TileIndex;

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

	
	// Last frame where this grass tile was selected for rendering.
	mutable uint64 FrameStamp = MAX_uint64;
	
	FCustomGrassRenderSystem* RenderSystem;

	TSharedPtr<FRenderingResourceHandles, ESPMode::ThreadSafe> ResourceHandles;

	// 'mutable' allows to cache it in GetDynamicMeshElements() (to elude const)
	mutable std::atomic<EGrassLOD> CachedLOD = EGrassLOD::NumLODs;

	/** Render-thread copy of landscape data useful to shaders. */
	FProxyLandscapeData LandscapeData;
	
	FCustomGrassVertexFactory* VertexFactory;
	
	struct FMaterialConfig
	{
		FMaterialRenderProxy* MaterialProxy;
		FMaterialRelevance MaterialRelevance;
	};

	FMaterialConfig MaterialConfig, NoTwoSideMaterialConfig;
};
