#pragma once

#include "CoreMinimal.h"
#include "RenderTypes.h"

struct FCustomGrassMaterial;
class ULandscapeComponent;
// struct FWindParams;
class UCustomGrassPrimitiveComponent;
class FCustomGrassRenderSystem;
class FCustomGrassVertexFactory;

class FCustomGrassSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FCustomGrassSceneProxy(const UCustomGrassPrimitiveComponent& Component,
		FCustomGrassRenderSystem* RenderSystem, int32 TileIndex);

	const CustomGrass::FProxyLandscapeData& GetLandscapeData() const { return LandscapeData; }

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
	
	FCustomGrassRenderSystem* RenderSystem;

	/** Render-thread copy of landscape data. */
	CustomGrass::FProxyLandscapeData LandscapeData;
	
	TUniquePtr<FCustomGrassVertexFactory> VertexFactory;
	
	struct FMaterialConfig
	{
		FMaterialRenderProxy* MaterialProxy;
		FMaterialRelevance MaterialRelevance;

		FMaterialConfig(const UMaterialInterface* Material,
			EShaderPlatform ShaderPlatform);
	};

	FMaterialConfig MaterialConfig, NoTwoSideMaterialConfig;
};
