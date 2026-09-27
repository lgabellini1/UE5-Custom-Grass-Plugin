#pragma once

#include "CoreMinimal.h"
#include "CustomGrassRenderTypes.h"
#include "CustomGrassShaderTypes.h"
#include "CustomGrassTypesInternal.h"
#include "Debug/CustomGrassDebugTypes.h"

namespace CustomGrass
{ struct FVolatileBuffers; class FShadowParams; }
class FCustomGrassSceneProxy;
class UCustomGrassDataAsset;
class FCustomGrassRenderSystem;
class FCustomGrassSceneViewExtension;

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

	bool bFixedLOD;
	ECustomGrassLOD GlobalLOD;
};

namespace CustomGrass
{
	struct FProxyRenderWorkDesc
	{
		FVector ViewOrigin;
		FMatrix ViewMatrix;
		const FCustomGrassSceneProxy* Proxy;
		TUniquePtr<FProxyVertexShaderData> VSData;
		FRDGTextureRef HeightmapTexture;
		float TilePriorityScore;
		
		bool operator==(const FProxyRenderWorkDesc& Other) const
		{
			return (Proxy == Other.Proxy) && (VSData->LOD == Other.VSData->LOD);
		}
	};

	class FTileDebugChannel
	{
	public:
		void SetDebugStateForWork(int32 WorkIndex, const FProxyRenderWorkDesc& Work);

		void PublishDebugStateSnapshot_RenderThread();

		FRTDebugState GetDebugStateSnapshot_GameThread() const;
	
	protected:
		FRTDebugState DebugState, GTDebugStateSnapshot;
	};
}

class FCustomGrassRenderSystem
{
	using FRDGPooledBufferRef  = TRefCountPtr<FRDGPooledBuffer>;
	using FRDGPooledTextureRef = TRefCountPtr<IPooledRenderTarget>;

public:
	FCustomGrassRenderSystem(
		const UCustomGrassDataAsset& DataAsset,
		const CustomGrass::FTextureRenderTargetsGT& RenderTargets);
	~FCustomGrassRenderSystem();
	
	void RegisterProxy(const FCustomGrassSceneProxy& Proxy);
	void UnregisterProxy(const FCustomGrassSceneProxy& Proxy);

	CustomGrass::FProxyVertexShaderData* GetProxyRenderResources(
		const FCustomGrassSceneProxy& Proxy) const;
	
	CustomGrass::FVertexShaderParams GetVertexShaderDataAssetParams() const;

	void RebuildRenderStateFromGameThread(const UCustomGrassDataAsset& DataAsset);
	
	void CompareAndCheckVSResourcesValidity(const CustomGrass::FProxyVertexShaderData* VSData) const;

	CustomGrass::FRTDebugState GetDebugStateSnapshot_GameThread() const;
	
private:
	void BeginFrame(FRDGBuilder& GraphBuilder);
	void EndFrame(FRDGBuilder& GraphBuilder);
	
	enum class ERenderSystemState : uint8
	{
		None 				 = 0,
		BuffersInitialized	 = 1 << 0,
		ShadowMapInitialized = 1 << 1,
	};
	FRIEND_ENUM_CLASS_FLAGS(ERenderSystemState);
	
	ERenderSystemState SystemState = ERenderSystemState::None;
	
	bool IsRunning() const;
	bool IsShadowMapInitialized() const;

	struct FRegisteredProxy
	{
		FPrimitiveComponentId ComponentId;
		const FCustomGrassSceneProxy* Proxy;

		bool operator==(const FRegisteredProxy& Other) const { return ComponentId == Other.ComponentId; }
	};

	TArray<FRegisteredProxy> RegisteredProxies;
	void SelectProxyIfRelevant(const FRegisteredProxy& RegisteredProxy, const FSceneView& View);
	void PrepareSelectedWorkForRendering(FRDGBuilder& GraphBuilder);
	
	TArray<CustomGrass::FProxyRenderWorkDesc> SelectedWork, PrevFrameSelectedWork;
	
	bool IsViewSameBetweenFrames() const;

	void SubmitWork(FRDGBuilder& GraphBuilder, const CustomGrass::FVolatileBuffers& Buffers);
	
	CustomGrass::FVolatileBuffers CreatePerFrameResources(FRDGBuilder& GraphBuilder) const;

	float CalcTilePriorityScore(const FSceneView* View,
		const CustomGrass::FProxyLandscapeData& LandscapeData) const;

	ECustomGrassLOD AssignTileLOD(const FSceneView* View,
		const CustomGrass::FProxyLandscapeData& LandscapeData) const;
	
	FRDGPooledBufferRef InstanceDataBuffer;
	TStaticArray<FRDGPooledBufferRef, CustomGrass::MaxRenderedTiles> IndirectDrawArgsBuffer;

	FRDGPooledTextureRef ShadowMapTextureAtlas;
	void CreateShadowMapResource(UTextureRenderTarget2D& ShadowMap);
	void DestroyShadowMapResourceIfSet();

	FRDGBufferRef TileAtlasMappingBuffer;
	void CreateTileAtlasMapping(FRDGBuilder& GraphBuilder);
	
	FDataAssetProxy DataAssetProxy;
	void RebuildDataAssetProxy(const UCustomGrassDataAsset& DataAsset);

	void AddComputePass_InstanceGrassBlades(
		FRDGBuilder& GraphBuilder,
		const CustomGrass::FProxyRenderWorkDesc& Work,
		const CustomGrass::FVolatileBuffers& Buffers,
		int32 TileIndex
	) const;

	void AddComputePass_InitIndirectDrawArgs(
		FRDGBuilder& GraphBuilder,
		const CustomGrass::FProxyRenderWorkDesc& Work,
		const CustomGrass::FVolatileBuffers& Buffers,
		int32 TileIndex
	) const;

	TSharedPtr<FCustomGrassSceneViewExtension, ESPMode::ThreadSafe> SceneViewExtension;
	friend class FCustomGrassSceneViewExtension;
	
	CustomGrass::FGrassParams BuildGrassParams() const;
	
	CustomGrass::FShadowParams BuildShadowParams(
		FRDGBuilder& GraphBuilder,
		int32 TileIndex,
		const CustomGrass::FVolatileBuffers& Buffers) const;
	
	CustomGrass::FLandscapeParams BuildLandscapeParams(
		FRDGBuilder& GraphBuilder,
		int32 TileIndex,
		const CustomGrass::FProxyLandscapeData& LandscapeTile) const;

	CustomGrass::FTileDebugChannel TileDebugChannel;
};
