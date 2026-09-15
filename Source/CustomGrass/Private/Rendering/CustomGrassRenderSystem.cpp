#include "CustomGrassRenderSystem.h"
#include "CustomGrassConsoleVars.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassGlobalShaders.h"
#include "RenderGraphUtils.h"
#include "ShaderTypes.h"
#include "Utilities.h"
#include "Engine/TextureRenderTarget2D.h"

FDataAssetProxy::FDataAssetProxy(const UCustomGrassDataAsset& DataAsset)
{
	Height = DataAsset.Height.ToValue();
	Width  = DataAsset.Width.ToValue();
	Tilt   = DataAsset.Tilt.ToValue();
	Bend   = DataAsset.Bend.ToValue();
			
	ClumpGridSize		= DataAsset.ClumpGridSize;
	ClumpStrength		= DataAsset.ClumpStrength.ToValue();
	ClumpFacingType		= DataAsset.ClumpFacingType;
	ClumpFacingStrength	= DataAsset.ClumpFacingStrength;
			
	ShortHeightThreshold	= DataAsset.ShortHeightThreshold;
			
	ViewSpaceCorrection		= DataAsset.ViewSpaceCorrection;
			
	NormalRoundnessStrength = DataAsset.NormalRoundnessStrength;
			
	MaxRenderDistance		= DataAsset.MaxRenderDistance;

	bShadowsOn			= DataAsset.bShadowsEnabled;
	ShadowProxyZOffset	= DataAsset.ShadowProxyZOffset;

	bFixedLOD = DataAsset.bFixedLOD;
	GlobalLOD = DataAsset.GlobalLOD;

	const FTextureRHIRef NoiseTexture = DataAsset.NoiseTexture
		? DataAsset.NoiseTexture->GetResource()->GetTextureRHI() : GBlackTexture->GetTextureRHI();
	WindParams = FWindParams(NoiseTexture, TStaticSamplerState<SF_Point>::GetRHI(),
		DataAsset.WindDirection.GetSafeNormal(), DataAsset.WindStrength, 0.f);
}

/** Per-frame buffers as RDG resources. */
struct FVolatileBuffers
{
	FRDGBufferRef	 InstanceDataBuffer;
	FRDGBufferSRVRef InstanceDataBufferSRV;
	FRDGBufferUAVRef InstanceDataBufferUAV;

	FRDGBufferRef	 InstanceCounter;
	FRDGBufferSRVRef InstanceCounterSRV;
	FRDGBufferUAVRef InstanceCounterUAV;

	TStaticArray<FRDGBufferRef, GMaxRenderedTiles> IndirectDrawArgs;
	TStaticArray<FRDGBufferUAVRef, GMaxRenderedTiles> IndirectDrawArgsUAV;

	FRDGTextureRef DensityAccumTextureAtlas;
	FRDGTextureUAVRef DensityAccumTextureAtlasUAV;
	FRDGTextureSRVRef DensityAccumTextureAtlasSRV;

	FRDGTextureRef ShadowWPOTextureAtlas;
	FRDGTextureUAVRef ShadowWPOTextureAtlasUAV;
};

FCustomGrassRenderSystem::FCustomGrassRenderSystem()
{
	check(GEngine);
	GEngine->GetPreRenderDelegateEx().AddRaw(this, &FCustomGrassRenderSystem::BeginFrame);
	GEngine->GetPostRenderDelegateEx().AddRaw(this, &FCustomGrassRenderSystem::EndFrame);
	
	ENQUEUE_RENDER_COMMAND(InitializeRTResources)
	(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			InstanceDataBuffer = AllocatePooledBuffer(InstanceDataBufferDesc, TEXT("InstanceDataBuffer"));
			
			for (int32 i = 0; i < IndirectDrawArgsBuffer.Num(); i++)
			{
				IndirectDrawArgsBuffer[i] = AllocatePooledBuffer(IndirectDrawArgsDesc,
					*FString::Printf(TEXT("IndirectDrawArgs_[%d]"), i));
			}

			// Required because the lambda may run after the first call of BeginFrame()
			bResourcesInitialized = true;
		}
	);
}

FCustomGrassRenderSystem::~FCustomGrassRenderSystem()
{
	check(GEngine);
	GEngine->GetPreRenderDelegateEx().RemoveAll(this);
	GEngine->GetPostRenderDelegateEx().RemoveAll(this);
	
	ENQUEUE_RENDER_COMMAND(DestroyRTResources)
	(
		// Let closure get ownership of the buffers as the 'this' ptr will be destroyed
		// (we are in the destructor) once this lambda runs.
		[InstanceData = MoveTemp(InstanceDataBuffer),
			IndirectDrawArgs = MoveTemp(IndirectDrawArgsBuffer)](FRHICommandListImmediate& RHICmdList) mutable
		{
			InstanceData.SafeRelease();

			for (FRDGPooledBufferRef& Buffer : IndirectDrawArgs)
			{
				Buffer.SafeRelease();
			}
		}
	);
}

float FCustomGrassRenderSystem::CalcTileSortingScore(const FSceneView* View,
	const FProxyLandscapeData& LandscapeData)
{
	FVector CameraToTile = GetTileCenter(LandscapeData) - View->ViewMatrices.GetViewOrigin();

	float Depth = FVector::DotProduct(CameraToTile, View->GetViewDirection());
	
	return Depth - 0.001f * CameraToTile.SizeSquared();
}

bool FCustomGrassRenderSystem::IsRunning() const
{
	return bGTRunningState && bResourcesInitialized;
}

void FCustomGrassRenderSystem::BeginFrame(FRDGBuilder& GraphBuilder)
{
	check(IsInRenderingThread());
	
	if (!IsRunning())
		return;

	RDG_EVENT_SCOPE(GraphBuilder, "CustomGrass");

#if WITH_EDITOR
	UE_LOG(LogTemp, Display, TEXT("--- CustomGrass: BeginFrame ---"));
#endif

	// If the view has not changed, reuse previous frame's work. Useful to
	// avoid flickering issues due to race conditions between grass tiles.
	// We assume same SceneView for each work.

	/*
	if (const FSceneView* ThisView = QueuedWork.Num() > 0 ? QueuedWork[0].View : nullptr;
		ThisView && IsPreviousFrameView(ThisView, PreviousFrameView) || (QueuedWork == PreviousFrameWork))
	{
		// Patch stale View pointer from previous frame
		for (FProxyRenderWorkDesc& Work : PreviousFrameWork)
			Work.View = ThisView;

		QueuedWork = PreviousFrameWork;
	}
	else
	{
		if (QueuedWork.Num() > GMaxRenderedTiles)
		{
			QueuedWork.Sort([](const FProxyRenderWorkDesc& A, const FProxyRenderWorkDesc& B)
			{
				return A.SortingScore > B.SortingScore;
			});
		
			QueuedWork.SetNum(GMaxRenderedTiles);
		}

		PreviousFrameWork = QueuedWork;
		PreviousFrameView = ThisView;
	}
	*/
	
	for (int32 i = 0, Count = FMath::Min(PreviousFrameWork.Num(), IndirectDrawArgsBuffer.Num());
		i < Count; i++)
	{
		// Handle re-assignment: take the handle from each proxy to be rendered
		// and make it point to the correct buffer. Somewhat of a hack and not very
		// clean architecturally, but it works as a solution for this circular dependency
		// between render system and proxy.
		
		const FProxyRenderWorkDesc& Work = PreviousFrameWork[i];
		
		FRenderingResourceHandles& ResourceHandles = Work.ResourceHandles.Get();
		ResourceHandles.InstanceData	 = TryGetSRV(InstanceDataBuffer);
		ResourceHandles.IndirectDrawArgs = TryGetRHI(IndirectDrawArgsBuffer[i]);
		ResourceHandles.TileOffset		 = i * GetInstanceCount(EGrassLOD::LOD0).X * GetInstanceCount(EGrassLOD::LOD0).Y;
		
		ResourceHandles.ViewSpaceCorrection		= DataAssetProxy.ViewSpaceCorrection;
		ResourceHandles.ShortHeightThreshold	= DataAssetProxy.ShortHeightThreshold;
		ResourceHandles.NormalRoundnessStrength = DataAssetProxy.NormalRoundnessStrength;
		/*
		ResourceHandles.WindParams = DataAssetProxy.WindParams;
		ResourceHandles.WindParams.Time = Work.View->Family->Time.GetWorldTimeSeconds();
		*/

		const FRDGTextureRef HeightmapRDG = RegisterExternalTexture(GraphBuilder,
			Work.LandscapeData.HeightmapTexture,
			*(FString::Printf(TEXT("Heightmap_[%d]"), i)));
		TileHeightmaps[i] = GraphBuilder.CreateSRV(HeightmapRDG);

#if DEBUG_RENDERED_TILES
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(i, 1.0f, FColor::Yellow,
				*(FString::Printf(TEXT("GrassTile_[%d]"), Work.TileIndex)));
		}
#endif
	}

	if (PreviousFrameWork.Num() > 0)
	{
		FVolatileBuffers Buffers;
		InitPerFrameResources(GraphBuilder, Buffers);
		InitGrassParams();

		SubmitWork(GraphBuilder, Buffers, PreviousFrameWork);
	}
}

void FCustomGrassRenderSystem::EndFrame(FRDGBuilder& GraphBuilder)
{
	check(IsInRenderingThread());

	if (!IsRunning())
		return;

#if WITH_EDITOR
	UE_LOG(LogTemp, Display, TEXT("--- CustomGrass: EndFrame ---"));
#endif

	/*
	if (const FMatrix* ThisView = QueuedWork.Num() > 0 ? QueuedWork[0].View : nullptr;
		ThisView && (IsPreviousFrameView(ThisView, PreviousFrameView) || QueuedWork == PreviousFrameWork))
	{
		// Patch stale View pointer from previous frame
		for (FProxyRenderWorkDesc& Work : PreviousFrameWork)
			Work.View = ThisView;

		QueuedWork = PreviousFrameWork;
	}
	else
	{
		if (QueuedWork.Num() > GMaxRenderedTiles)
		{
			QueuedWork.Sort([](const FProxyRenderWorkDesc& A, const FProxyRenderWorkDesc& B)
			{
				return A.SortingScore > B.SortingScore;
			});

			QueuedWork.SetNum(GMaxRenderedTiles);
		}

		PreviousFrameWork = QueuedWork;
		PreviousFrameView = ThisView;
	}
	*/

	if (QueuedWork.Num() > 0)
	{
		const FMatrix& ThisViewProjectionMatrix = QueuedWork[0].ViewMatrix;

		if (IsPreviousFrameView(ThisViewProjectionMatrix, PreviousFrameViewMatrix) ||
			QueuedWork == PreviousFrameWork)
		{
			QueuedWork = PreviousFrameWork;
		}
		else
		{
			if (QueuedWork.Num() > GMaxRenderedTiles)
			{
				QueuedWork.Sort([](const FProxyRenderWorkDesc& A, const FProxyRenderWorkDesc& B)
				{
					return A.SortingScore > B.SortingScore;
				});

				QueuedWork.SetNum(GMaxRenderedTiles);
			}

			PreviousFrameWork = QueuedWork;
			PreviousFrameViewMatrix = ThisViewProjectionMatrix;
		}
	}
	else
	{
		// No work gathered this frame at all — nothing to reuse or trim.
		// Decide here whether you want to fall through with an empty QueuedWork,
		// or explicitly keep last frame's PreviousFrameWork stale until new work arrives.
	}

	// Mark surviving work as 'rendered this frame'
	for (const FProxyRenderWorkDesc& Work : QueuedWork)
	{
		if (Work.GrassProxy)
		{
			Work.GrassProxy->StampNextFrame_RenderThread();
		}
	}

	if (QueuedWork.Num() > 0)
		bHasActiveSelection = true;

	QueuedWork.Empty();

	for (int i = 0; i < TileHeightmaps.Num(); i++)
		TileHeightmaps[i] = nullptr;
}

void FCustomGrassRenderSystem::RebuildRenderState(const UCustomGrassDataAsset& DataAsset)
{
	...
	RebuildDataAssetProxy(DataAsset);
}

bool FCustomGrassRenderSystem::IsPreviousFrameView(const FMatrix& ThisFrameView, const FMatrix& PrevFrameView)
{
	return ThisFrameView.Equals(PrevFrameView);
}

FRenderingResourceHandles FCustomGrassRenderSystem::GetBufferHandles_RenderThread() const
{
	check(IsInAnyRenderingThread());
	
	check(InstanceDataBuffer);
	check(IndirectDrawArgsBuffer[0]);
	
	// Temporarily assign null handles to the first indirect args buffer and tile offset. Later the pointers will
	// be correctly assigned to their correct values. 
	return FRenderingResourceHandles(
		TryGetSRV(InstanceDataBuffer),
		TryGetRHI(IndirectDrawArgsBuffer[0]),
		INDEX_NONE
//		FWindParams(GBlackTexture->GetTextureRHI(), TStaticSamplerState<>::GetRHI(),
//		FVector2f::Zero(), 0.f)
	);
}

void FCustomGrassRenderSystem::AddRenderingWork(const FSceneView* View,
	const FProxyLandscapeData* LandscapeData,
	const TSharedRef<FRenderingResourceHandles>& ResourceHandles,
	const FCustomGrassSceneProxy* Proxy,
	EGrassLOD& InLOD)
{
	check(IsInAnyRenderingThread());

	/* Ensure thread-safe writing of QueuedWork from the various worker threads, so that
	 * only one can insert at a time. */
	FScopeLock Lock(&AddRenderingWorkCS);

	// Frustum culling
	if (!View->ViewFrustum.IntersectBox(FVector(GetTileCenter(*LandscapeData)),
		FVector(GetTileExtent(*LandscapeData))))
		return false;

	// LOD assignment

	if (DataAssetProxy.bManualLOD)
	{
		InLOD = DataAssetProxy.GlobalLOD;
	}
	else
	{
		auto Camera = FVector3f(View->ViewMatrices.GetViewOrigin());
		auto Tile = FVector3f(GetClosestPointToTile(View, *LandscapeData));
	
		float CameraToTileDist = FVector3f::Distance(Camera, Tile);

		for (int32 i = 0; i < GNumLODs; i++)
		{
			if (CameraToTileDist <= GetDistanceThreshold(static_cast<EGrassLOD>(i)))
			{
				InLOD = static_cast<EGrassLOD>(i);
				break;
			}
		}
	}
	
	float TileSortingScore = CalcTileSortingScore(View, *LandscapeData);
	
	QueuedWork.Push(FProxyRenderWorkDesc{ View->ViewMatrices.GetViewOrigin(),
		View->ViewMatrices.GetViewProjectionMatrix(),
		*LandscapeData,
		Proxy,
		ResourceHandles,
		InLOD,
		TileSortingScore,
		Proxy->TileIndex
	});
}

void FCustomGrassRenderSystem::CreateTileAtlasMapping(FRDGBuilder& GraphBuilder,
	const TArray<FProxyRenderWorkDesc>& Work)
{
	TArray<FTileAtlasMapping> TileAtlasMapping;
	for (int32 TileIndex = 0; TileIndex < Work.Num(); TileIndex++)
	{
		FIntPoint AtlasCoord = FIntPoint(
			TileIndex % GShadowWPOAtlasGridSize, TileIndex / GShadowWPOAtlasGridSize);
		
		TileAtlasMapping.Push(FTileAtlasMapping(
			Work[TileIndex].LandscapeData.SectionBase.X,
			Work[TileIndex].LandscapeData.SectionBase.Y,
			AtlasCoord.X,
			AtlasCoord.Y
		));
	}
	
	TileAtlasMappingBuffer = CreateStructuredBuffer(
		GraphBuilder, TEXT("TileAtlasMappingBuf"),
		TConstArrayView<FTileAtlasMapping>(TileAtlasMapping));
}

void FCustomGrassRenderSystem::SubmitWork(FRDGBuilder& GraphBuilder, FVolatileBuffers& InBuffers,
	const TArray<FProxyRenderWorkDesc>& Work)
{
	check(IsInRenderingThread());
	
	AddClearUAVPass(GraphBuilder, InBuffers.InstanceCounterUAV, 0);
	AddClearUAVPass(GraphBuilder, InBuffers.DensityAccumTextureAtlasUAV, 0.f);
	if (DataAssetProxy.bShadowsOn)
	{
		AddClearUAVPass(GraphBuilder, InBuffers.ShadowWPOTextureAtlasUAV, 0.f);
	}
	CreateTileAtlasMapping(GraphBuilder, Work);
	
	for (int32 i = 0; i < Work.Num(); i++)
	{
		const FProxyRenderWorkDesc& WorkDesc = Work[i];
		
		AddComputePass_InstanceGrassBlades(GraphBuilder, WorkDesc, InBuffers, i);
		
		AddComputePass_InitIndirectDrawArgs(GraphBuilder, WorkDesc, InBuffers, i);
	}
}

CustomGrass::FGrassParams FCustomGrassRenderSystem::BuildGrassParams() const
{
	CustomGrass::FGrassParams GrassParams;
	
	GrassParams.Height				= DataAssetProxy.Height.Value;
	GrassParams.Width				= DataAssetProxy.Width.Value;
	GrassParams.Tilt				= DataAssetProxy.Tilt.Value;
	GrassParams.Bend				= DataAssetProxy.Bend.Value;
	GrassParams.ClumpStrength		= DataAssetProxy.ClumpStrength.Value;
	GrassParams.ClumpGridSize		= DataAssetProxy.ClumpGridSize;
	GrassParams.ClumpFacingType		= static_cast<uint8>(DataAssetProxy.ClumpFacingType);
	GrassParams.ClumpFacingStrength = DataAssetProxy.ClumpFacingStrength;

	GrassParams.MaxHeight = CustomGrass::MaxGrassBladeHeight;
	GrassParams.MaxWidth  = CustomGrass::MaxGrassBladeWidth;
	GrassParams.MaxTilt	  = CustomGrass::MaxGrassBladeTilt;
	GrassParams.MaxBend   = CustomGrass::MaxGrassBladeBend;

	GrassParams.RandHeight		  = DataAssetProxy.Height.VariationPercentage;
	GrassParams.RandWidth	 	  = DataAssetProxy.Width.VariationPercentage;
	GrassParams.RandTilt		  = DataAssetProxy.Tilt.VariationPercentage;
	GrassParams.RandBend		  = DataAssetProxy.Bend.VariationPercentage;
	GrassParams.RandClumpStrength = DataAssetProxy.ClumpStrength.VariationPercentage;

	return GrassParams;
}

CustomGrass::FShadowParams FCustomGrassRenderSystem::BuildShadowParams(
	FRDGBuilder& GraphBuilder,
	int32 TileIndex,
	const CustomGrass::FVolatileBuffers& Buffers) const
{
	CustomGrass::FShadowParams ShadowParams;
	
	ShadowParams.bShadowsOn				  = static_cast<int32>(DataAssetProxy.bShadowsOn);
	ShadowParams.OutShadowWPOTextureAtlas = Buffers.ShadowMapTextureAtlas.UAV;
	ShadowParams.AtlasOffsetX			  = (TileIndex % CustomGrass::ShadowMapAtlasGridSize)
		* CustomGrass::ShadowMapTextureSlotResolution.X;
	ShadowParams.AtlasOffsetY			  = (TileIndex / CustomGrass::ShadowMapAtlasGridSize)
		* CustomGrass::ShadowMapTextureSlotResolution.Y;
	ShadowParams.AtlasSlotSize			  = CustomGrass::ShadowMapTextureSlotResolution.X;
	ShadowParams.AtlasGridSize			  = CustomGrass::ShadowMapAtlasGridSize;
	ShadowParams.TileAtlasMapping		  = GraphBuilder.CreateSRV(TileAtlasMappingBuffer);
	ShadowParams.ProxyZOffset			  = DataAssetProxy.ShadowProxyZOffset;
	ShadowParams.OutDensityAccum		  = Buffers.DensityAccumTextureAtlas.UAV;

	return ShadowParams;
}

CustomGrass::FLandscapeParams FCustomGrassRenderSystem::BuildLandscapeParams(int32 TileIndex,
	const CustomGrass::FProxyLandscapeData& LandscapeTile) const
{
	CustomGrass::FLandscapeParams LandscapeParams;
	
	LandscapeParams.TileSizeInQuads		  = LandscapeTile.ComponentSizeQuads;
	LandscapeParams.LandscapeSizeInQuadsX = LandscapeTile.TotalSizeInQuads.X;
	LandscapeParams.LandscapeSizeInQuadsY = LandscapeTile.TotalSizeInQuads.Y;
	LandscapeParams.QuadOffsetFromOriginX = LandscapeTile.SectionBase.X;
	LandscapeParams.QuadOffsetFromOriginY = LandscapeTile.SectionBase.Y;
	LandscapeParams.LandscapeLocalToWorld = FMatrix44f(LandscapeTile.LocalToWorldMatrix);
	LandscapeParams.HeightmapTexture	  = TileHeightmaps[TileIndex];
	LandscapeParams.HeightmapSampler	  = LandscapeTile.HeightmapSampler;
	LandscapeParams.HeightmapScaleBias    = FVector4f(LandscapeTile.HeightmapScaleBias);

	return LandscapeParams;
}

void FCustomGrassRenderSystem::AddComputePass_InstanceGrassBlades(
	FRDGBuilder& GraphBuilder,
	const FProxyRenderWorkDesc& Work,
	const FVolatileBuffers& InBuffers,
	int32 TileIndex) const
{
	check(IsInAnyRenderingThread());

	const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM6);
	const TShaderRef InstanceGrassCS = TShaderMapRef<FInstanceGrassBladeCS>(GlobalShaderMap);
	const FProxyLandscapeData& Tile = Work.LandscapeData;

#if WITH_EDITOR
	static bool bFrozenViewFrustum = false;
	static FMatrix44f CachedViewFrustum;
	
	if (CVarFrozenViewFrustum.GetValueOnRenderThread() == 1 && !bFrozenViewFrustum)
	{
		CachedViewFrustum = FMatrix44f(Work.ViewMatrix);
		bFrozenViewFrustum = true;
	}
	else if (CVarFrozenViewFrustum.GetValueOnRenderThread() == 0)
	{
		bFrozenViewFrustum = false;
	}
#endif

	FInstanceGrassBladeCS::FParameters* Params = GraphBuilder.AllocParameters<FInstanceGrassBladeCS::FParameters>();
	Params->OutInstanceDataBuffer = InBuffers.InstanceDataBufferUAV;
	Params->OutInstanceCounter	  = InBuffers.InstanceCounterUAV;
	Params->TileIndex			  = TileIndex;
	Params->InstanceCountPerTileX = GetInstanceCount(Work.LOD).X;
	Params->InstanceCountPerTileY = GetInstanceCount(Work.LOD).Y;
	Params->BufferRegionSize	  = GetInstanceCount(EGrassLOD::LOD0).X * GetInstanceCount(EGrassLOD::LOD0).Y;
#if WITH_EDITOR
	Params->ViewProjectionMatrix = bFrozenViewFrustum ? CachedViewFrustum :
		FMatrix44f(Work.ViewMatrix);
#else
	Params->ViewProjectionMatrix = FMatrix44f(Work.ViewMatrix);
#endif
	Params->ViewOrigin			  = FVector4f(FLinearColor(Work.ViewOrigin));
	Params->MaxRenderDistance	  = DataAssetProxy.MaxRenderDistance;
	Params->LandscapeParams		  = BuildLandscapeParams(TileIndex, Tile);
	Params->GrassParams			  = BuildGrassParams();
	Params->ShadowParams		  = BuildShadowParams(GraphBuilder, TileIndex, Buffers);

	if (DataAssetProxy.bShadowsOn)
	{
		check(Params->InstanceCountPerTileX >= GShadowWPOTextureSlotRes.X);
		check(Params->InstanceCountPerTileY >= GShadowWPOTextureSlotRes.Y);
	}
	
	const FIntVector ThreadCount = FIntVector(GetInstanceCount(Work.LOD).X,
		GetInstanceCount(Work.LOD).Y, 1); // Total thread count, split among groups
	const int32 GroupSize 		 = FInstanceGrassBladeCS::GroupThreadCount.X;
	const FIntVector GroupCount  = FComputeShaderUtils::GetGroupCount(ThreadCount, GroupSize);
	FComputeShaderUtils::ValidateGroupCount(GroupCount);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("InstanceGrassBlades"),
		ERDGPassFlags::Compute,
		InstanceGrassCS,
		Params,
		GroupCount
	);
}

void FCustomGrassRenderSystem::AddComputePass_InitIndirectDrawArgs(
	FRDGBuilder& GraphBuilder,
	const FProxyRenderWorkDesc& Work,
	const FVolatileBuffers& InBuffers,
	int32 TileIndex) const
{
	check(IsInAnyRenderingThread());
	
	const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM6);
	const TShaderRef InitIndirectDrawArgsCS = TShaderMapRef<FInitIndirectDrawArgsCS>(GlobalShaderMap);

	FInitIndirectDrawArgsCS::FParameters* Params = GraphBuilder.AllocParameters<FInitIndirectDrawArgsCS::FParameters>();
	Params->OutIndirectDrawArgsBuffer = InBuffers.IndirectDrawArgsUAV[TileIndex];
	Params->InInstanceCounter		  = InBuffers.InstanceCounterSRV;
	Params->TileIndex			      = TileIndex;
	Params->GrassBladeVertexCount	  = GetGrassBladeVertexCount(Work.LOD);

	const FIntVector ThreadCount = FIntVector(1, 1, 1);
	const FIntVector GroupCount  = FComputeShaderUtils::GetGroupCount(ThreadCount, 1);
	FComputeShaderUtils::ValidateGroupCount(GroupCount);
	
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("InitIndirectDrawArgs"),
		ERDGPassFlags::Compute,
		InitIndirectDrawArgsCS,
		Params,
		GroupCount
	);
}

void FCustomGrassRenderSystem::InitPerFrameResources(FRDGBuilder& GraphBuilder, FVolatileBuffers& OutBuffers) const
{
	check(IsInRenderingThread());
	
	// Instance data buffer
	
	OutBuffers.InstanceDataBuffer	 = GraphBuilder.RegisterExternalBuffer(InstanceDataBuffer);
	OutBuffers.InstanceDataBufferSRV = GraphBuilder.CreateSRV(OutBuffers.InstanceDataBuffer);
	OutBuffers.InstanceDataBufferUAV = GraphBuilder.CreateUAV(OutBuffers.InstanceDataBuffer);

	// Instance counter
	
	const FRDGBufferDesc InstanceCounterDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32),
		GMaxRenderedTiles);
	
	OutBuffers.InstanceCounter	  = GraphBuilder.CreateBuffer(InstanceCounterDesc, TEXT("InstanceCounter"));
	OutBuffers.InstanceCounterSRV = GraphBuilder.CreateSRV(
		FRDGBufferSRVDesc(OutBuffers.InstanceCounter, PF_R32_UINT));
	OutBuffers.InstanceCounterUAV = GraphBuilder.CreateUAV(
		FRDGBufferUAVDesc(OutBuffers.InstanceCounter, PF_R32_UINT));
	// @note: Typed buffers get stride from format

	// Indirect draw args

	for (int32 i = 0; i < IndirectDrawArgsBuffer.Num(); i++)
	{
		OutBuffers.IndirectDrawArgs[i] = GraphBuilder.RegisterExternalBuffer(IndirectDrawArgsBuffer[i]);
		OutBuffers.IndirectDrawArgsUAV[i] = GraphBuilder.CreateUAV(OutBuffers.IndirectDrawArgs[i]);
	}
	
	// Shadows: texture atlases
	
	OutBuffers.ShadowWPOTextureAtlas = GraphBuilder.RegisterExternalTexture(ShadowWPOTextureAtlas);
	OutBuffers.ShadowWPOTextureAtlasUAV = GraphBuilder.CreateUAV(OutBuffers.ShadowWPOTextureAtlas);

	OutBuffers.DensityAccumTextureAtlas = GraphBuilder.CreateTexture(DensityAccumAtlasDesc,
		TEXT("DensityAccumAtlas"));
	OutBuffers.DensityAccumTextureAtlasUAV = GraphBuilder.CreateUAV(OutBuffers.DensityAccumTextureAtlas);
	OutBuffers.DensityAccumTextureAtlasSRV = GraphBuilder.CreateSRV(OutBuffers.DensityAccumTextureAtlas);
}

void FCustomGrassRenderSystem::NotifyRunningStateFromGameThread(bool bNewGTRunningState)
{
	bGTRunningState = bNewGTRunningState;
}

void FCustomGrassRenderSystem::RebuildDataAssetProxy(const UCustomGrassDataAsset& DataAsset)
{
	ENQUEUE_RENDER_COMMAND(RebuildDataAssetProxy)
	(
		[this, NewDataAssetProxy = FDataAssetProxy(DataAsset)]
		(FRHICommandListImmediate& RHICmdList)
		{
			DataAssetProxy = MoveTemp(NewDataAssetProxy);
		}
	);
}

void FCustomGrassRenderSystem::UpdateShadowMapResourceFromGameThread(UTextureRenderTarget2D* ShadowMap) const
{
	check(ShadowMap);
	
	ENQUEUE_RENDER_COMMAND(CreateShadowMapAtlasPooledResource)
	(
		[this, ShadowMapResource = ShadowMap->GameThread_GetRenderTargetResource()]
		(FRHICommandListImmediate& RHICmdList)
		{
			ShadowWPOTextureAtlas = CreateRenderTarget(ShadowMapResource->GetRenderTargetTexture(),
				TEXT("ShadowWPOTextureAtlas"));
		}
	);
}
