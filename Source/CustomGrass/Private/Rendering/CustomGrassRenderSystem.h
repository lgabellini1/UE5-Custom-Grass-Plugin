#pragma once

#include "CoreMinimal.h"

struct FWindParams
{
	FTextureRHIRef NoiseTexture;
	FSamplerStateRHIRef NoiseSampler;
	FVector2f Direction;
	float Strength;
	float Time;
};

class FCustomGrassRenderSystem
{
	friend class UCustomGrassWorldSubsystem;

	using FRDGPooledBufferRef  = TRefCountPtr<FRDGPooledBuffer>;
	using FRDGPooledTextureRef = TRefCountPtr<IPooledRenderTarget>;

	/** Rendering work uploaded by a proxy. */
	struct FWorkDesc
	{
		const FVector ViewOrigin;
		const FMatrix ViewMatrix;
		const FProxyLandscapeData LandscapeData;
		const FCustomGrassSceneProxy* GrassProxy;
		const TSharedRef<FRenderingResourceHandles> ResourceHandles;
		EGrassLOD LOD;
		float SortingScore;
		
		int32 TileIndex; // for debug
		
		bool operator==(const FWorkDesc& Other) const
		{
			return (LandscapeData.SectionBase == Other.LandscapeData.SectionBase)
				&& (LOD == Other.LOD);
		}
	};

	/** RT-copy of grass parameters from the data asset. */
	struct FDataAssetProxy
	{
		template<class T = float>
		struct TRandomValue { T Val; float Random; };
		
		TRandomValue<> Height;
		TRandomValue<> Width;
		TRandomValue<> Tilt;
		TRandomValue<> Bend;
		
		TRandomValue<> ClumpStrength;
		int ClumpGridSize;
		EClumpFacingType ClumpFacingType;
		float ClumpFacingStrength;
		
		float ShortHeightThreshold;
		
		float ViewSpaceCorrection;
		
		float NormalRoundnessStrength;
		
		float MaxRenderDistance;

		bool bShadowsOn;
		float ShadowProxyZOffset;

		FWindParams WindParams;

		bool bManualLOD;
		EGrassLOD GlobalLOD;

		FDataAssetProxy() = default;
		explicit FDataAssetProxy(const UCustomGrassDataAsset* const DataAsset);
	};

public:
	FCustomGrassRenderSystem();

	~FCustomGrassRenderSystem();
	
	/** Called by renderer before rendering frame: submits accumulated rendering work. */ 
	void BeginFrame(FRDGBuilder& GraphBuilder);

	/** Called by renderer after rendering frame: cleanup of rendering resources. */ 
	void EndFrame(FRDGBuilder& GraphBuilder);

	/**
	 * Called by proxies to register themselves for rendering work.
	 */ 
	void AddRenderingWork(const FSceneView* View, 
		const FProxyLandscapeData* LandscapeData,
		const TSharedRef<FRenderingResourceHandles>& ResourceHandles,
		const FCustomGrassSceneProxy* Proxy,
		EGrassLOD& InLOD);

	FRenderingResourceHandles GetBufferHandles_RenderThread() const;

	/*void SetGrassDensityRTResource_RenderThread(const FTextureRenderTargetResource* RTResource);*/

	void SetShadowWPOResource_RenderThread(const FTextureRenderTargetResource* RTResource);

	/*void SetMaxDisplacement_RenderThread(float NewVal) { MaxDisplacement = NewVal; }*/

protected:

	bool bIsActive = false;
	bool bResourcesInitialized = false;
	bool bHasActiveSelection = false;
	
	FCriticalSection AddRenderingWorkCS;
	
	/** Scheduled rendering work for the current frame. */
	TArray<FWorkDesc> QueuedWork;

	TArray<FWorkDesc> PreviousFrameWork;
	FMatrix PreviousFrameViewMatrix;

	static bool IsPreviousFrameView(const FMatrix& ThisFrameView, const FMatrix& PrevFrameView);

	void SubmitWork(FRDGBuilder& GraphBuilder, FVolatileBuffers& InBuffers, const TArray<FWorkDesc>& Work);

	void InitPerFrameResources(FRDGBuilder& GraphBuilder, FVolatileBuffers& OutBuffers) const;

	void InitGrassParams();
	
	static float CalcTileSortingScore(const FSceneView* View,
		const FProxyLandscapeData& LandscapeData);
	
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

	TStaticArray<FRDGPooledBufferRef, GMaxRenderedTiles> IndirectDrawArgsBuffer;

	const FRDGBufferDesc InstanceDataBufferDesc = FRDGBufferDesc::CreateStructuredDesc(
		sizeof(FGrassBladeDataPacked),
		GMaxRenderedTiles * GetInstanceCount(EGrassLOD::LOD0).X * GetInstanceCount(EGrassLOD::LOD0).Y);

	const FRDGBufferDesc IndirectDrawArgsDesc = FRDGBufferDesc::CreateIndirectDesc(
		sizeof(uint32), GIndexedIndirectDrawArgsNum);
	
	const FRDGTextureDesc DensityAccumAtlasDesc = FRDGTextureDesc::Create2D(
		GShadowWPOTextureSlotRes * GShadowWPOAtlasGridSize,
		PF_R16_UINT,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV
	);

	FRDGPooledTextureRef ShadowWPOTextureAtlas;

	FRDGBufferRef TileAtlasMappingBuffer;
	void CreateTileAtlasMapping(FRDGBuilder& GraphBuilder, const TArray<FWorkDesc>& Work);
	
	/**
	 * Representation of the data asset as cached on the render-thread.
	 */
	FDataAssetProxy DataAssetProxy;

	FGrassParams GrassParams;
	
	/** Cached heightmap SRVs for this frame. */
	TStaticArray<FRDGTextureSRVRef, GMaxRenderedTiles> TileHeightmaps;

	float MaxDisplacement;
	
	/**
	 * Dispatches a compute shader for instancing grass blade data
	 * in a whole landscape tile.\n
	 * Writes to the instance data buffer.
	 */
	void AddComputePass_InstanceGrassBlades(
		FRDGBuilder& GraphBuilder,
		const FWorkDesc& Work,
		const FVolatileBuffers& InBuffers,
		int32 TileIndex
	) const;

	/**
	 * Dispatches a compute shader for initializing the indirect draw args buffer.\n
	 * Dependencies: InstanceGrassBlades compute pass, for the instance count.
	 */
	void AddComputePass_InitIndirectDrawArgs(
		FRDGBuilder& GraphBuilder,
		const FWorkDesc& Work,
		const FVolatileBuffers& InBuffers,
		int32 TileIndex
	) const;
};
