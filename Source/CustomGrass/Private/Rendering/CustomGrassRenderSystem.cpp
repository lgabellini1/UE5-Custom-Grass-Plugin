#include "CustomGrassRenderSystem.h"
#include "ConsoleVars.h"
#include "Constants.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassGlobalShaders.h"
#include "CustomGrassSceneProxy.h"
#include "RenderGraphUtils.h"
#include "ShaderTypes.h"
#include "Utilities.h"
#include "Engine/TextureRenderTarget2D.h"

namespace CustomGrass 
{
	template <class ResourceType, class SRVType, class UAVType>
	struct TResourceAccess<ResourceType, SRVType, UAVType>
	{
		ResourceType Resource = nullptr;
		SRVType SRV = nullptr;
		UAVType UAV = nullptr;
	};

	using FRDGBufferAccess  = TResourceAccess<FRDGBufferRef, FRDGBufferSRVRef,  FRDGBufferUAVRef>;
	using FRDGTextureAccess = TResourceAccess<FRDGTextureRef, FRDGTextureSRVRef, FRDGTextureUAVRef>;

	struct FVolatileBuffers
	{
		FRDGBufferAccess InstanceDataBuffer;
		FRDGBufferAccess InstanceCounter;
		TStaticArray<FRDGBufferAccess, MaxRenderedTiles> IndirectDrawArgs;

		FRDGTextureAccess DensityAccumTextureAtlas;
		FRDGTextureAccess ShadowMapTextureAtlas;
	};
}

constexpr int32 IndexedIndirectDrawArgsNum = 5;

const uint32 InstanceCountPerTile = CustomGrass::GetInstanceCount(CustomGrass::EGrassLOD::LOD0).X *
	CustomGrass::GetInstanceCount(CustomGrass::EGrassLOD::LOD0).Y;

const uint32 MaxInstanceCount = CustomGrass::MaxRenderedTiles * InstanceCountPerTile;

const FRDGBufferDesc InstanceDataBufferDesc = FRDGBufferDesc::CreateStructuredDesc(
	sizeof(CustomGrass::FGrassBladeDataPacked), MaxInstanceCount);

const FRDGBufferDesc IndirectDrawArgsDesc = FRDGBufferDesc::CreateIndirectDesc(
	sizeof(uint32), IndexedIndirectDrawArgsNum);

const FRDGBufferDesc InstanceCounterDesc = FRDGBufferDesc::CreateBufferDesc(
	sizeof(uint32), CustomGrass::MaxRenderedTiles);

const FRDGTextureDesc DensityAccumAtlasDesc = FRDGTextureDesc::Create2D(
	CustomGrass::ShadowMapTextureSlotResolution * CustomGrass::ShadowMapAtlasGridSize,
	PF_R16_UINT,
	FClearValueBinding::Black,
	TexCreate_ShaderResource | TexCreate_UAV
);

FCustomGrassRenderSystem::FCustomGrassRenderSystem(const UCustomGrassDataAsset& DataAsset)
	: DataAssetProxy(DataAsset)
{
	check(GEngine);
	GEngine->GetPreRenderDelegateEx().AddRaw(this, &FCustomGrassRenderSystem::BeginFrame);
	GEngine->GetPostRenderDelegateEx().AddRaw(this, &FCustomGrassRenderSystem::EndFrame);

	SelectedWork.Reserve(CustomGrass::MaxRenderedTiles);
	
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
		// (executes after the destructor) once this lambda runs.
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

float FCustomGrassRenderSystem::CalcTilePriorityScore(const FSceneView* View,
	const CustomGrass::FProxyLandscapeData& LandscapeData) const
{
	check(IsInAnyRenderingThread());
	
	const FVector CameraToTile = GetLandscapeTileOrigin(LandscapeData) - View->ViewMatrices.GetViewOrigin();
	const float Depth = FVector::DotProduct(CameraToTile, View->GetViewDirection());

	return Depth - DataAssetProxy.TilePriorityDistancePenalty * CameraToTile.SizeSquared();
}

bool FCustomGrassRenderSystem::IsRunning() const
{
	check(IsInAnyRenderingThread());
	return bResourcesInitialized;
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
	
	for (int32 i = 0, Count = FMath::Min(SelectedWork.Num(), CustomGrass::MaxRenderedTiles);
		i < Count; i++)
	{
		// Handle re-assignment: take the handle from each proxy to be rendered
		// and make it point to the correct buffer. Somewhat of a hack and not very
		// clean architecturally, but it works as a solution for this circular dependency
		// between render system and proxy.
		
		const CustomGrass::FProxyRenderWorkDesc& Work = SelectedWork[i];
		
		auto& [ InstanceData, IndirectDrawArgs] = Work.VSData->RenderingResources;
		InstanceData	 = TryGetSRV(InstanceDataBuffer);
		IndirectDrawArgs = TryGetRHI(IndirectDrawArgsBuffer[i]);
		
		Work.VSData->TileOffset	= i * InstanceCountPerTile;
		
		/*
		ResourceHandles.WindParams = DataAssetProxy.WindParams;
		ResourceHandles.WindParams.Time = Work.View->Family->Time.GetWorldTimeSeconds();
		*/

		const FRDGTextureRef HeightmapRDG = RegisterExternalTexture(GraphBuilder,
			Work.Proxy->GetLandscapeData().HeightmapTexture,
			*(FString::Printf(TEXT("Heightmap_[%d]"), i)));
		
		TileHeightmaps[i] = GraphBuilder.CreateSRV(HeightmapRDG);
	}

	if (!SelectedWork.IsEmpty())
	{
		const CustomGrass::FVolatileBuffers Buffers = CreatePerFrameResources(GraphBuilder);
		SubmitWork(GraphBuilder, Buffers);
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

	// If the view has not changed, reuse previous frame's work. Useful to
	// avoid flickering issues due to race conditions between grass tiles.
	
	if (!IsViewSameBetweenFrames())
	{
		SelectedWork = CreateWorkSelectionFromQueue();
	}

	NextFrameQueuedWork.Reset();

	for (FRDGTextureSRVRef& Heightmap : TileHeightmaps)
		Heightmap = nullptr;
}

TArray<CustomGrass::FProxyRenderWorkDesc> FCustomGrassRenderSystem::CreateWorkSelectionFromQueue()
{
	check(IsInAnyRenderingThread());

	if (NextFrameQueuedWork.IsEmpty())
	{
		return {};
	}
	
	NextFrameQueuedWork.Sort([](
		const CustomGrass::FProxyRenderWorkDesc& A,
		const CustomGrass::FProxyRenderWorkDesc& B)
	{
		return A.TilePriorityScore > B.TilePriorityScore;
	});
	
	if (NextFrameQueuedWork.Num() > CustomGrass::MaxRenderedTiles)
	{
		NextFrameQueuedWork.SetNum(CustomGrass::MaxRenderedTiles);
	}

	return MoveTemp(NextFrameQueuedWork);
}

void FCustomGrassRenderSystem::RebuildRenderState(const UCustomGrassDataAsset& DataAsset)
{
	BuildDataAssetProxy(DataAsset);
}

bool FCustomGrassRenderSystem::IsViewSameBetweenFrames() const
{
	if (SelectedWork.IsEmpty() || NextFrameQueuedWork.IsEmpty())
	{
		return false;
	}

	return NextFrameQueuedWork[0].ViewMatrix.Equals(
		SelectedWork[0].ViewMatrix);
}

/*
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
*/

CustomGrass::FVertexShaderParams FCustomGrassRenderSystem::GetVertexShaderDataAssetParams() const
{
	return CustomGrass::FVertexShaderParams(
		DataAssetProxy.ViewSpaceCorrection,
		DataAssetProxy.NormalRoundnessStrength,
		DataAssetProxy.ShortHeightThreshold
	);
}

CustomGrass::EGrassLOD FCustomGrassRenderSystem::AssignTileLOD(
	const FSceneView* View,
	const CustomGrass::FProxyLandscapeData& LandscapeData) const
{
	CustomGrass::EGrassLOD AssignedLOD = CustomGrass::EGrassLOD::LOD2;
	
	if (DataAssetProxy.bFixedLOD)
	{
		AssignedLOD = DataAssetProxy.GlobalLOD;
	}
	else
	{
		const FVector Camera = View->ViewMatrices.GetViewOrigin();
		const FVector NearestTileBounds = GetNearestTileBoundsPointFromCamera(View, LandscapeData);
	
		const float CameraToTileDist = FVector::Distance(Camera, NearestTileBounds);

		for (CustomGrass::EGrassLOD LOD : TEnumRange<CustomGrass::EGrassLOD>())
		{
			if (CameraToTileDist <= GetDistanceThreshold(LOD))
			{
				AssignedLOD = LOD;
				break;
			}
		}
	}

	return AssignedLOD;
}

CustomGrass::FProxyVertexShaderData* FCustomGrassRenderSystem::AddProxyRenderingWork(
	const FCustomGrassSceneProxy& Proxy,
	const FSceneView* View) 
{
	check(IsInAnyRenderingThread());

	const CustomGrass::FProxyLandscapeData& LandscapeData = Proxy.GetLandscapeData();

	if (IsTileOutsideViewFrustum(View, LandscapeData))
	{
		return nullptr;
	}

	bool bDrawThisFrame = SelectedWork.ContainsByPredicate(
		[&Proxy](const CustomGrass::FProxyRenderWorkDesc& Work)
		{
			return Work.Proxy == &Proxy;
		});
	
	if (!bDrawThisFrame)
	{
		return nullptr;
	}

	/* Ensure thread-safe writing of NextFrameQueuedWork from the various (render) worker threads, so that
	 * only one can insert at a time. */
	FScopeLock Lock(&AddRenderingWorkCS);
	
	CustomGrass::EGrassLOD AssignedLOD = AssignTileLOD(View, LandscapeData);
	
	auto VSData = MakeUnique<CustomGrass::FProxyVertexShaderData>(
		CreateNewResourceHandles(),
		AssignedLOD);
	CustomGrass::FProxyVertexShaderData* VSDataHandle = VSData.Get();

	float TilePriorityScore = CalcTilePriorityScore(View, LandscapeData);
	
	NextFrameQueuedWork.Push(CustomGrass::FProxyRenderWorkDesc{
		View->ViewMatrices.GetViewOrigin(),
		View->ViewMatrices.GetViewProjectionMatrix(),
		&Proxy,
		MoveTemp(VSData),
		TilePriorityScore
	});

	return VSDataHandle;
}

CustomGrass::FRenderingResourceHandles FCustomGrassRenderSystem::CreateNewResourceHandles()
{
	return CustomGrass::FRenderingResourceHandles(TryGetSRV(InstanceDataBuffer),
		TryGetRHI(IndirectDrawArgsBuffer[0])
	);
}

void FCustomGrassRenderSystem::CreateTileAtlasMapping(FRDGBuilder& GraphBuilder)
{
	struct FTileAtlasMapping
	{
		FIntPoint LandscapeTileCoord;
		FIntPoint AtlasTile;
	};
	
	TArray<FTileAtlasMapping> TileAtlasMappings;
	
	for (int32 i = 0; i < SelectedWork.Num(); i++)
	{
		const FIntPoint AtlasCoord = FIntPoint(
			i % CustomGrass::ShadowMapAtlasGridSize,
			i / CustomGrass::ShadowMapAtlasGridSize);

		check(SelectedWork[i].Proxy);
		
		TileAtlasMappings.Push(FTileAtlasMapping(
			SelectedWork[i].Proxy->GetLandscapeData().SectionBase,
			AtlasCoord
		));
	}
	
	TileAtlasMappingBuffer = CreateStructuredBuffer(
		GraphBuilder, TEXT("TileAtlasMappingBuf"),
		TConstArrayView<FTileAtlasMapping>(TileAtlasMappings));
}

void FCustomGrassRenderSystem::SubmitWork(FRDGBuilder& GraphBuilder, const CustomGrass::FVolatileBuffers& Buffers)
{
	check(IsInRenderingThread());
	
	AddClearUAVPass(GraphBuilder, Buffers.InstanceCounter.UAV, 0);
	AddClearUAVPass(GraphBuilder, Buffers.DensityAccumTextureAtlas.UAV, 0.f);
	
	if (DataAssetProxy.bShadowsOn)
	{
		AddClearUAVPass(GraphBuilder, Buffers.ShadowMapTextureAtlas.UAV, 0.f);
	}
	
	CreateTileAtlasMapping(GraphBuilder);
	
	for (int32 i = 0; i < SelectedWork.Num(); i++)
	{
		const CustomGrass::FProxyRenderWorkDesc& WorkDesc = SelectedWork[i];
		
		AddComputePass_InstanceGrassBlades(GraphBuilder, WorkDesc, Buffers, i);
		
		AddComputePass_InitIndirectDrawArgs(GraphBuilder, WorkDesc, Buffers, i);
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
	const CustomGrass::FProxyRenderWorkDesc& Work,
	const CustomGrass::FVolatileBuffers& Buffers,
	int32 TileIndex) const
{
	check(IsInAnyRenderingThread());

	const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM6);
	const TShaderRef InstanceGrassCS = TShaderMapRef<FInstanceGrassBladeCS>(GlobalShaderMap);
	
	const CustomGrass::FProxyLandscapeData& Tile = Work.Proxy->GetLandscapeData();

#if WITH_EDITOR
	static bool bFrozenViewFrustum = false;
	static FMatrix44f CachedViewFrustum;
	
	if (CustomGrass::CVarFrozenViewFrustum.GetValueOnRenderThread() == 1 && !bFrozenViewFrustum)
	{
		CachedViewFrustum = FMatrix44f(Work.ViewMatrix);
		bFrozenViewFrustum = true;
	}
	else if (CustomGrass::CVarFrozenViewFrustum.GetValueOnRenderThread() == 0)
	{
		bFrozenViewFrustum = false;
	}
#endif

	FInstanceGrassBladeCS::FParameters* Params = GraphBuilder.AllocParameters<FInstanceGrassBladeCS::FParameters>();
	Params->OutInstanceDataBuffer = Buffers.InstanceDataBuffer.UAV;
	Params->OutInstanceCounter	  = Buffers.InstanceCounter.UAV;
	Params->TileIndex			  = TileIndex;
	Params->InstanceCountPerTileX = GetInstanceCount(Work.VSData->LOD).X;
	Params->InstanceCountPerTileY = GetInstanceCount(Work.VSData->LOD).Y;
	Params->BufferRegionSize	  = InstanceCountPerTile;
#if WITH_EDITOR
	Params->ViewProjectionMatrix  = bFrozenViewFrustum ? CachedViewFrustum : FMatrix44f(Work.ViewMatrix);
#else
	Params->ViewProjectionMatrix  = FMatrix44f(Work.ViewMatrix);
#endif
	Params->ViewOrigin			  = FVector4f(FLinearColor(Work.ViewOrigin));
	Params->MaxRenderDistance	  = DataAssetProxy.MaxRenderDistance;
	Params->LandscapeParams		  = BuildLandscapeParams(TileIndex, Tile);
	Params->GrassParams			  = BuildGrassParams();
	Params->ShadowParams		  = BuildShadowParams(GraphBuilder, TileIndex, Buffers);

	if (DataAssetProxy.bShadowsOn)
	{
		check(Params->InstanceCountPerTileX >= CustomGrass::ShadowMapTextureSlotResolution.X);
		check(Params->InstanceCountPerTileY >= CustomGrass::ShadowMapTextureSlotResolution.Y);
	}
	
	const FIntVector ThreadCount = FIntVector(GetInstanceCount(Work.VSData->LOD).X,
		GetInstanceCount(Work.VSData->LOD).Y, 1); // Total thread count, split among groups
	const int32 GroupSize 		 = CustomGrass::GroupThreadCount.X;
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
	const CustomGrass::FProxyRenderWorkDesc& Work,
	const CustomGrass::FVolatileBuffers& Buffers,
	int32 TileIndex) const
{
	check(IsInAnyRenderingThread());
	
	const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM6);
	const TShaderRef InitIndirectDrawArgsCS = TShaderMapRef<FInitIndirectDrawArgsCS>(GlobalShaderMap);

	FInitIndirectDrawArgsCS::FParameters* Params = GraphBuilder.AllocParameters<FInitIndirectDrawArgsCS::FParameters>();
	Params->OutIndirectDrawArgsBuffer = Buffers.IndirectDrawArgs[TileIndex].UAV;
	Params->InInstanceCounter		  = Buffers.InstanceCounter.SRV;
	Params->TileIndex			      = TileIndex;
	Params->GrassBladeVertexCount	  = GetGrassBladeVertexCount(Work.VSData->LOD);

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

CustomGrass::FVolatileBuffers FCustomGrassRenderSystem::CreatePerFrameResources(
	FRDGBuilder& GraphBuilder) const
{
	check(IsInRenderingThread());

	CustomGrass::FVolatileBuffers Buffers;
	
	// Instance data buffer
	
	Buffers.InstanceDataBuffer.Resource = GraphBuilder.RegisterExternalBuffer(InstanceDataBuffer);
	Buffers.InstanceDataBuffer.SRV	    = GraphBuilder.CreateSRV(Buffers.InstanceDataBuffer.Resource);
	Buffers.InstanceDataBuffer.UAV	    = GraphBuilder.CreateUAV(Buffers.InstanceDataBuffer.Resource);

	// Instance counter
	
	Buffers.InstanceCounter.Resource = GraphBuilder.CreateBuffer(InstanceCounterDesc, TEXT("InstanceCounter"));
	Buffers.InstanceCounter.SRV	     = GraphBuilder.CreateSRV(
		FRDGBufferSRVDesc(Buffers.InstanceCounter.Resource, PF_R32_UINT));
	Buffers.InstanceCounter.UAV	     = GraphBuilder.CreateUAV(
		FRDGBufferUAVDesc(Buffers.InstanceCounter.Resource, PF_R32_UINT));
	// @note: Typed buffers get stride from format

	// Indirect draw args

	for (int32 i = 0; i < IndirectDrawArgsBuffer.Num(); i++)
	{
		Buffers.IndirectDrawArgs[i].Resource = GraphBuilder.RegisterExternalBuffer(IndirectDrawArgsBuffer[i]);
		Buffers.IndirectDrawArgs[i].UAV	     = GraphBuilder.CreateUAV(Buffers.IndirectDrawArgs[i].Resource);
	}
	
	// Shadows: texture atlases
	
	Buffers.ShadowMapTextureAtlas.Resource = GraphBuilder.RegisterExternalTexture(ShadowMapTextureAtlas);
	Buffers.ShadowMapTextureAtlas.UAV	   = GraphBuilder.CreateUAV(Buffers.ShadowMapTextureAtlas.Resource);

	Buffers.DensityAccumTextureAtlas.Resource = GraphBuilder.CreateTexture(DensityAccumAtlasDesc,
		TEXT("DensityAccumAtlas"));
	Buffers.DensityAccumTextureAtlas.UAV	  = GraphBuilder.CreateUAV(Buffers.DensityAccumTextureAtlas.Resource);
	Buffers.DensityAccumTextureAtlas.SRV	  = GraphBuilder.CreateSRV(Buffers.DensityAccumTextureAtlas.Resource);

	return Buffers;
}

void FCustomGrassRenderSystem::BuildDataAssetProxy(const UCustomGrassDataAsset& DataAsset)
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

void FCustomGrassRenderSystem::UpdateShadowMapResourceFromGameThread(UTextureRenderTarget2D& ShadowMap) const
{
	ENQUEUE_RENDER_COMMAND(CreateShadowMapAtlasPooledResource)
	(
		[this, ShadowMapResource = ShadowMap.GameThread_GetRenderTargetResource()]
		(FRHICommandListImmediate& RHICmdList)
		{
			ShadowMapTextureAtlas = CreateRenderTarget(ShadowMapResource->GetRenderTargetTexture(),
				TEXT("ShadowMapTextureAtlas"));
		}
	);
}

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
			
	ShortHeightThreshold		= DataAsset.ShortHeightThreshold;
			
	ViewSpaceCorrection			= DataAsset.ViewSpaceCorrection;
			
	NormalRoundnessStrength		= DataAsset.NormalRoundnessStrength;
			
	MaxRenderDistance			= DataAsset.MaxRenderDistance;

	TilePriorityDistancePenalty = DataAsset.TilePriorityDistancePenalty;

	bShadowsOn			= DataAsset.bShadowsEnabled;
	ShadowProxyZOffset	= DataAsset.ShadowProxyZOffset;

	bFixedLOD = DataAsset.bFixedLOD;
	GlobalLOD = DataAsset.GlobalLOD;

	const FTextureRHIRef NoiseTexture = DataAsset.NoiseTexture
		? DataAsset.NoiseTexture->GetResource()->GetTextureRHI() : GBlackTexture->GetTextureRHI();
	WindParams = CustomGrass::FWindParams(NoiseTexture, TStaticSamplerState<SF_Point>::GetRHI(),
		DataAsset.WindDirection.GetSafeNormal(), DataAsset.WindStrength, 0.f);
}
