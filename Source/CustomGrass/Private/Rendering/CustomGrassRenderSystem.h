#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "RenderTypes.h"
#include "ShaderTypes.h"
#include "Types.h"

namespace CustomGrass
{
	struct FVolatileBuffers;
	class FShadowParams;
}
class FCustomGrassSceneProxy;
class UCustomGrassDataAsset;

/*
 * Order of execution is roughly:
 * -> engine renderer module calls GetDynamicMeshElements() on each FCustomGrassSceneProxy
 *    (gathers mesh elements, passes resources to vertex factory / shader, etc...);
 * -> BeginFrame() callback runs on FCustomGrassRenderSystem: compute shaders get dispatched
 * -> compute pass
 * -> draw call execution
 * -> EndFrame() callback
 */

struct FDataAssetProxy
{
	explicit FDataAssetProxy(const UCustomGrassDataAsset& DataAsset);
		
	TRandomVariationValue<float> Height;
	TRandomVariationValue<float> Width;
	TRandomVariationValue<float> Tilt;
	TRandomVariationValue<float> Bend;
		
	TRandomVariationValue<float> ClumpStrength;
	int ClumpGridSize;
	EClumpFacingType ClumpFacingType;
	float ClumpFacingStrength;
		
	float ShortHeightThreshold;
		
	float ViewSpaceCorrection;
		
	float NormalRoundnessStrength;
		
	float MaxRenderDistance;

	float TilePriorityDistancePenalty;

	bool bShadowsOn;
	float ShadowProxyZOffset;

	CustomGrass::FWindParams WindParams;

	bool bFixedLOD;
	CustomGrass::EGrassLOD GlobalLOD;
};

namespace CustomGrass
{
	struct FProxyRenderWorkDesc
	{
		FVector ViewOrigin;
		FMatrix ViewMatrix;
		const FCustomGrassSceneProxy* Proxy;
		TUniquePtr<FProxyVertexShaderData> VSData;
		float TilePriorityScore;
		
		bool operator==(const FProxyRenderWorkDesc& Other) const
		{
			return (Proxy == Other.Proxy) && (VSData->LOD == Other.VSData->LOD);
		}
	};
}

class FCustomGrassRenderSystem
{
	using FRDGPooledBufferRef  = TRefCountPtr<FRDGPooledBuffer>;
	using FRDGPooledTextureRef = TRefCountPtr<IPooledRenderTarget>;

public:
	explicit FCustomGrassRenderSystem(const UCustomGrassDataAsset& DataAsset);

	~FCustomGrassRenderSystem();
	
	void BeginFrame(FRDGBuilder& GraphBuilder);
	void EndFrame(FRDGBuilder& GraphBuilder);

	CustomGrass::FProxyVertexShaderData* AddProxyRenderingWork(
		const FCustomGrassSceneProxy& Proxy,
		const FSceneView* View);
	
	CustomGrass::FVertexShaderParams GetVertexShaderDataAssetParams() const;

	void RebuildRenderState(const UCustomGrassDataAsset& DataAsset);

	void UpdateShadowMapResourceFromGameThread(UTextureRenderTarget2D& ShadowMap) const;

protected:
	bool IsRunning() const;
	
	bool bRunningState,
	bResourcesInitialized;
	
	FCriticalSection AddRenderingWorkCS;
	
	TArray<CustomGrass::FProxyRenderWorkDesc> NextFrameQueuedWork, SelectedWork;
	
	TArray<CustomGrass::FProxyRenderWorkDesc> CreateWorkSelectionFromQueue();

	bool IsViewSameBetweenFrames() const;

	void SubmitWork(FRDGBuilder& GraphBuilder, const CustomGrass::FVolatileBuffers& Buffers);

	CustomGrass::FVolatileBuffers CreatePerFrameResources(FRDGBuilder& GraphBuilder) const;

	float CalcTilePriorityScore(const FSceneView* View,
		const CustomGrass::FProxyLandscapeData& LandscapeData) const;

	CustomGrass::EGrassLOD AssignTileLOD(const FSceneView* View,
		const CustomGrass::FProxyLandscapeData& LandscapeData) const;
	
	/**
	 * Each of these buffers is made up of several "partitions", one
	 *  for each visible grass tile, in range [(N * i)... (N * i) + N - 1]
	 *  where N represents a known value:
	 *  - for instance data, grass blade number per tile;
	 *  - for indirect draw args, the number of args which is 5.
	 *  
	 *  The index i is then per-frame and dynamic, meaning it's not necessarily
	 *  associated to the same tile each frame.
	 */
	FRDGPooledBufferRef InstanceDataBuffer;

	TStaticArray<FRDGPooledBufferRef, CustomGrass::MaxRenderedTiles> IndirectDrawArgsBuffer;

	FRDGPooledTextureRef ShadowMapTextureAtlas;

	FRDGBufferRef TileAtlasMappingBuffer;
	void CreateTileAtlasMapping(FRDGBuilder& GraphBuilder);
	
	FDataAssetProxy DataAssetProxy;
	void BuildDataAssetProxy(const UCustomGrassDataAsset& DataAsset);
	
	TStaticArray<FRDGTextureSRVRef, CustomGrass::MaxRenderedTiles> TileHeightmaps;

	float MaxDisplacement;

	CustomGrass::FRenderingResourceHandles CreateNewResourceHandles();

	CustomGrass::FGrassParams BuildGrassParams() const;
	
	CustomGrass::FShadowParams BuildShadowParams(
		FRDGBuilder& GraphBuilder,
		int32 TileIndex,
		const CustomGrass::FVolatileBuffers& Buffers) const;
	
	CustomGrass::FLandscapeParams BuildLandscapeParams(
		int32 TileIndex,
		const CustomGrass::FProxyLandscapeData& LandscapeTile) const;
	
	/**
	 * Dispatches a compute shader for instancing grass blade data
	 * in a whole landscape tile.\n
	 * Writes to the instance data buffer.
	 */
	void AddComputePass_InstanceGrassBlades(
		FRDGBuilder& GraphBuilder,
		const CustomGrass::FProxyRenderWorkDesc& Work,
		const CustomGrass::FVolatileBuffers& Buffers,
		int32 TileIndex
	) const;

	/**
	 * Dispatches a compute shader for initializing the indirect draw args buffer.\n
	 * Dependencies: InstanceGrassBlades compute pass, for the instance count.
	 */
	void AddComputePass_InitIndirectDrawArgs(
		FRDGBuilder& GraphBuilder,
		const CustomGrass::FProxyRenderWorkDesc& Work,
		const CustomGrass::FVolatileBuffers& Buffers,
		int32 TileIndex
	) const;
};
