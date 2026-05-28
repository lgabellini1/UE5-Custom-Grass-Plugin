// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomGrassWorldSubsystem.h"
#include "CustomGrass.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassPrimitiveComponent.h"
#include "CustomGrassShadowProxyComponent.h"
#include "CustomGrassSceneProxy.h"
#include "CustomGrassSettings.h"
#include "Landscape.h"
#include "LandscapeStreamingProxy.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneViewExtension.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "Delegates/Delegate.h"
#include "Engine/TextureRenderTarget2D.h"

IMPLEMENT_GLOBAL_SHADER(FInstanceGrassBladeCS, "/CustomShaders/Compute.usf", "CSInstanceGrassBlades", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FInitIndirectDrawArgsCS, "/CustomShaders/Compute.usf", "CSInitIndirectDrawArgs", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FResolveGrassDensityCS, "/CustomShaders/Compute.usf", "CSResolveGrassDensity", SF_Compute);

FCustomGrassRenderSystem::FDataAssetProxy::FDataAssetProxy(const UCustomGrassDataAsset* const DataAsset)
{
	Height = { DataAsset->Height, DataAsset->RandomizeHeight };
	Width  = { DataAsset->Width, DataAsset->RandomizeWidth };
	Tilt   = { DataAsset->Tilt, DataAsset->RandomizeTilt };
	Bend   = { DataAsset->Bend, DataAsset->RandomizeBend };
	ClumpStrength = { DataAsset->ClumpStrength, DataAsset->RandomizeClumpStrength };
			
	ClumpGridSize			= DataAsset->ClumpGridSize;
			
	ClumpFacingType			= DataAsset->ClumpFacingType;
	ClumpFacingStrength		= DataAsset->ClumpFacingStrength;
			
	ShortHeightThreshold	= DataAsset->ShortHeightThreshold;
			
	ViewSpaceCorrection		= DataAsset->ViewSpaceCorrection;
			
	NormalRoundnessStrength = DataAsset->NormalRoundnessStrength;
			
	MaxRenderDistance		= DataAsset->MaxRenderDistance;

	ShadowProxyZOffset		= DataAsset->ZOffset;

	const FTextureRHIRef NoiseTexture = DataAsset->NoiseTexture
		? DataAsset->NoiseTexture->GetResource()->GetTextureRHI() : GBlackTexture->GetTextureRHI();
	WindParams = FWindParams(NoiseTexture, TStaticSamplerState<SF_Point>::GetRHI(),
		DataAsset->WindDirection.GetSafeNormal(), DataAsset->WindStrength, 0.f);
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
		// Get ownership of buffers as the 'this' ptr will deterministically be destroyed
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

void FCustomGrassRenderSystem::BeginFrame(FRDGBuilder& GraphBuilder)
{
	check(IsInRenderingThread());
	
	if (!bIsActive || !bResourcesInitialized)
		return;

	RDG_EVENT_SCOPE(GraphBuilder, "CustomGrass");

#if WITH_EDITOR
	UE_LOG(LogTemp, Display, TEXT("--- CustomGrass: BeginFrame ---"));
#endif

	// If the view has not changed, reuse previous frame's work. Useful to
	// avoid flickering issues due to race conditions between grass tiles.
	// We assume same SceneView for each work.
	
	if (const FSceneView* ThisView = QueuedWork.Num() > 0 ? QueuedWork[0].View : nullptr;
		ThisView && IsPreviousFrameView(ThisView, PreviousFrameView) || (QueuedWork == PreviousFrameWork))
	{
		// Patch stale View pointer from previous frame
		for (FWorkDesc& Work : PreviousFrameWork)
			Work.View = ThisView;

		QueuedWork = PreviousFrameWork;
	}
	else
	{
		if (QueuedWork.Num() > GMaxRenderedTiles)
		{
			QueuedWork.Sort([](const FWorkDesc& A, const FWorkDesc& B)
			{
				return A.SortingScore > B.SortingScore;
			});
		
			QueuedWork.SetNum(GMaxRenderedTiles);
		}

		PreviousFrameWork = QueuedWork;
		PreviousFrameView = ThisView;
	}

	for (int32 i = 0; i < QueuedWork.Num(); i++)
	{
		// Handle re-assignment: take the handle from each proxy to be rendered
		// and make it point to the correct buffer. Somewhat of a hack and not very
		// clean architecturally, but it works as a solution for this circular dependency
		// between render system and proxy.
		
		const FWorkDesc& Work = QueuedWork[i];
		
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
			Work.LandscapeData->HeightmapTexture,
			*(FString::Printf(TEXT("Heightmap_[%d]"), i)));
		TileHeightmaps[i] = GraphBuilder.CreateSRV(HeightmapRDG);
	}

	if (QueuedWork.Num() > 0)
	{
		FVolatileBuffers Buffers;
		InitPerFrameResources(GraphBuilder, Buffers);
		InitGrassParams();

		SubmitWork(GraphBuilder, Buffers, QueuedWork);
	}
}

void FCustomGrassRenderSystem::EndFrame(FRDGBuilder& GraphBuilder)
{
	check(IsInRenderingThread());

	if (!bIsActive)
		return;

#if WITH_EDITOR
	UE_LOG(LogTemp, Display, TEXT("--- CustomGrass: EndFrame ---"));
#endif

	QueuedWork.Empty();

	for (int i = 0; i < TileHeightmaps.Num(); i++)
		TileHeightmaps[i] = nullptr;
}

bool FCustomGrassRenderSystem::IsPreviousFrameView(const FSceneView* ThisFrameView, const FSceneView* PrevFrameView)
{
	if (!ThisFrameView || !PrevFrameView)
		return ThisFrameView == PrevFrameView;
	
	return ThisFrameView->ViewMatrices.GetViewProjectionMatrix().Equals(
		PrevFrameView->ViewMatrices.GetViewProjectionMatrix());
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

/*
void FCustomGrassRenderSystem::SetGrassDensityRTResource_RenderThread(const FTextureRenderTargetResource* RTResource)
{
	check(IsInAnyRenderingThread());
	check(RTResource);

	GrassDensityTextureAtlas = CreateRenderTarget(RTResource->GetRenderTargetTexture(),
		TEXT("GrassDensityTextureAtlas"));
}
*/

void FCustomGrassRenderSystem::SetShadowWPOResource_RenderThread(const FTextureRenderTargetResource* RTResource)
{
	check(IsInAnyRenderingThread());
	check(RTResource);

	ShadowWPOTextureAtlas = CreateRenderTarget(RTResource->GetRenderTargetTexture(),
		TEXT("ShadowWPOTextureAtlas"));
}

bool FCustomGrassRenderSystem::AddRenderingWork(const FSceneView* View,
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

	auto CameraXY = FVector2f(FVector3f(View->ViewMatrices.GetViewOrigin()));
	auto TileXY = FVector2f(FVector3f(GetClosestPointToTile(View, *LandscapeData)));
	
	float CameraToTileDist = FVector2f::Distance(CameraXY, TileXY);

	if (CameraToTileDist <= GetDistanceThreshold(EGrassLOD::LOD1))
		InLOD = EGrassLOD::LOD0;
	else
		InLOD = EGrassLOD::LOD1;
	
	float TileSortingScore = CalcTileSortingScore(View, *LandscapeData);
	
	QueuedWork.Push(FWorkDesc{ View, LandscapeData, ResourceHandles, InLOD, TileSortingScore });
	
	return true;
}

void FCustomGrassRenderSystem::CreateTileAtlasMapping(FRDGBuilder& GraphBuilder,
	const TArray<FWorkDesc>& Work)
{
	TArray<FTileAtlasMapping> TileAtlasMapping;
	for (int32 TileIndex = 0; TileIndex < Work.Num(); TileIndex++)
	{
		FIntPoint AtlasCoord = FIntPoint(
			TileIndex % GShadowWPOAtlasGridSize, TileIndex / GShadowWPOAtlasGridSize);
		
		TileAtlasMapping.Push(FTileAtlasMapping(
			Work[TileIndex].LandscapeData->SectionBase.X,
			Work[TileIndex].LandscapeData->SectionBase.Y,
			AtlasCoord.X,
			AtlasCoord.Y
		));
	}
	
	TileAtlasMappingBuffer = CreateStructuredBuffer(
		GraphBuilder, TEXT("TileAtlasMappingBuf"),
		TConstArrayView<FTileAtlasMapping>(TileAtlasMapping));
}

void FCustomGrassRenderSystem::SubmitWork(FRDGBuilder& GraphBuilder, FVolatileBuffers& InBuffers, const TArray<FWorkDesc>& Work)
{
	check(IsInRenderingThread());
	
	AddClearUAVPass(GraphBuilder, InBuffers.InstanceCounterUAV, 0);
	/*AddClearUAVPass(GraphBuilder, InBuffers.GrassDensityTextureAtlasUAV, 0.f);*/
	AddClearUAVPass(GraphBuilder, InBuffers.ShadowWPOTextureAtlasUAV, 0.f);
	AddClearUAVPass(GraphBuilder, InBuffers.DensityAccumTextureAtlasUAV, 0.f);

	CreateTileAtlasMapping(GraphBuilder, Work);
	
	for (int32 i = 0; i < Work.Num(); i++)
	{
		const FWorkDesc& WorkDesc = Work[i];
		
		AddComputePass_InstanceGrassBlades(GraphBuilder, WorkDesc, InBuffers, i);
		
		AddComputePass_InitIndirectDrawArgs(GraphBuilder, WorkDesc, InBuffers, i);

		

		AddComputePass_ResolveGrassDensity(GraphBuilder, WorkDesc, InBuffers, i);
	}
}

void FCustomGrassRenderSystem::InitGrassParams()
{
	GrassParams.Height				= DataAssetProxy.Height.Val;
	GrassParams.Width				= DataAssetProxy.Width.Val;
	GrassParams.Tilt				= DataAssetProxy.Tilt.Val;
	GrassParams.Bend				= DataAssetProxy.Bend.Val;
	GrassParams.ClumpStrength		= DataAssetProxy.ClumpStrength.Val;
	GrassParams.ClumpGridSize		= DataAssetProxy.ClumpGridSize;
	GrassParams.ClumpFacingType		= static_cast<uint8>(DataAssetProxy.ClumpFacingType);
	GrassParams.ClumpFacingStrength = DataAssetProxy.ClumpFacingStrength;

	GrassParams.MaxHeight = GMaxGrassBladeHeight;
	GrassParams.MaxWidth  = GMaxGrassBladeWidth;
	GrassParams.MaxTilt	  = GMaxGrassBladeTilt;
	GrassParams.MaxBend   = GMaxGrassBladeBend;

	GrassParams.RandHeight		  = DataAssetProxy.Height.Random;
	GrassParams.RandWidth		  = DataAssetProxy.Width.Random;
	GrassParams.RandTilt		  = DataAssetProxy.Tilt.Random;
	GrassParams.RandBend		  = DataAssetProxy.Bend.Random;
	GrassParams.RandClumpStrength = DataAssetProxy.ClumpStrength.Random;	
}

void FCustomGrassRenderSystem::AddComputePass_InstanceGrassBlades(
	FRDGBuilder& GraphBuilder,
	const FWorkDesc& Work,
	const FVolatileBuffers& InBuffers,
	int32 TileIndex) const
{
	check(IsInAnyRenderingThread());
	
	const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(Work.View->GetFeatureLevel());
	const TShaderRef InstanceGrassCS = TShaderMapRef<FInstanceGrassBladeCS>(GlobalShaderMap);
	const FProxyLandscapeData* Tile = Work.LandscapeData;

#if WITH_EDITOR
	static bool bFrozenViewFrustum = false;
	static FMatrix44f CachedViewFrustum;
	
	if (CVarFrozenViewFrustum.GetValueOnRenderThread() == 1 && !bFrozenViewFrustum)
	{
		CachedViewFrustum = FMatrix44f(Work.View->ViewMatrices.GetViewProjectionMatrix());
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
		FMatrix44f(Work.View->ViewMatrices.GetViewProjectionMatrix());
#else
	Params->ViewProjectionMatrix = FMatrix44f(Work.View->ViewMatrices.GetViewProjectionMatrix());
#endif
	Params->ViewOrigin			  = FVector4f(FLinearColor(Work.View->ViewMatrices.GetViewOrigin()));
	Params->MaxRenderDistance	  = DataAssetProxy.MaxRenderDistance;
	Params->HeightmapScaleBias    = Tile->HeightmapScaleBias;
	Params->TileSizeInQuads		  = Tile->ComponentSizeQuads;
	Params->LandscapeSizeInQuadsX = Tile->TotalSizeInQuads.X;
	Params->LandscapeSizeInQuadsY = Tile->TotalSizeInQuads.Y;
	Params->QuadOffsetFromOriginX = Tile->SectionBase.X;
	Params->QuadOffsetFromOriginY = Tile->SectionBase.Y;
	Params->LandscapeLocalToWorld = Tile->LocalToWorld;
	Params->HeightmapTexture	  = TileHeightmaps[TileIndex];
	Params->HeightmapSampler   = Tile->HeightmapSampler;
	Params->GrassParams			  = GrassParams;

	Params->OutShadowWPOTextureAtlas = InBuffers.ShadowWPOTextureAtlasUAV;
	Params->AtlasOffsetX     = (TileIndex % GShadowWPOAtlasGridSize) * GShadowWPOTextureSlotRes.X;
	Params->AtlasOffsetY     = (TileIndex / GShadowWPOAtlasGridSize) * GShadowWPOTextureSlotRes.Y;
	Params->AtlasSlotSize    = GShadowWPOTextureSlotRes.X;
	Params->AtlasGridSize	 = GShadowWPOAtlasGridSize;
	Params->TileAtlasMapping = GraphBuilder.CreateSRV(TileAtlasMappingBuffer);
	Params->ProxyZOffset     = DataAssetProxy.ShadowProxyZOffset;
	Params->OutDensityAccum  = InBuffers.DensityAccumTextureAtlasUAV;

	check(Params->InstanceCountPerTileX >= GShadowWPOTextureSlotRes.X);
	check(Params->InstanceCountPerTileY >= GShadowWPOTextureSlotRes.Y);
	
	const FIntVector ThreadCount = FIntVector(GetInstanceCount(Work.LOD).X, GetInstanceCount(Work.LOD).Y, 1); // Total thread count, split among groups
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
	const FWorkDesc& Work,
	const FVolatileBuffers& InBuffers,
	int32 TileIndex) const
{
	check(IsInAnyRenderingThread());
	
	const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(Work.View->GetFeatureLevel());
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

void FCustomGrassRenderSystem::AddComputePass_ResolveGrassDensity(
	FRDGBuilder& GraphBuilder,
	const FWorkDesc& Work,
	const FVolatileBuffers& InBuffers,
	int32 TileIndex) const
{
	check(IsInAnyRenderingThread());

	const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(Work.View->GetFeatureLevel());
	const TShaderRef ResolveGrassDensityCS = TShaderMapRef<FResolveGrassDensityCS>(GlobalShaderMap);

	const FProxyLandscapeData* Tile = Work.LandscapeData;
	
	FResolveGrassDensityCS::FParameters* Params = GraphBuilder.AllocParameters<FResolveGrassDensityCS::FParameters>();
	Params->OutShadowWPOTextureAtlas = InBuffers.ShadowWPOTextureAtlasUAV;
	Params->DensityAccum			 = InBuffers.DensityAccumTextureAtlasSRV;
	Params->MaxExpectedDensity	     = 128.f;
	
	Params->AtlasOffsetX     = (TileIndex % GShadowWPOAtlasGridSize) * GShadowWPOTextureSlotRes.X;
	Params->AtlasOffsetY     = (TileIndex / GShadowWPOAtlasGridSize) * GShadowWPOTextureSlotRes.Y;

	
	const FIntVector ThreadCount = FIntVector(GShadowWPOTextureSlotRes.X, GShadowWPOTextureSlotRes.Y, 1);
	const int32 GroupSize 		 = FResolveGrassDensityCS::GroupThreadCount.X;
	const FIntVector GroupCount  = FComputeShaderUtils::GetGroupCount(ThreadCount, GroupSize);
	FComputeShaderUtils::ValidateGroupCount(GroupCount);
	
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ResolveGrassDensity"),
		ERDGPassFlags::Compute,
		ResolveGrassDensityCS,
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

void UCustomGrassWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	RenderSystem = MakeUnique<FCustomGrassRenderSystem>();

	// Set up delegates
	OnGrassDataAssetLoadDelegate.AddUObject(this, &UCustomGrassWorldSubsystem::OnDataAssetChanged);
	OnCVarGrassEnableChangeDelegate.AddUObject(this, &UCustomGrassWorldSubsystem::OnCVarChanged);

	// Try loading the data asset from the plugin settings; if not found the system won't start
	const auto* Settings = GetDefault<UCustomGrassSettings>();
	GrassDataAsset = Settings->GrassDataAsset.LoadSynchronous();

	// Shadows: create the texture atlas on the GT
	
	ShadowWPOTextureAtlas = NewObject<UTextureRenderTarget2D>();
	ShadowWPOTextureAtlas->InitAutoFormat(
		GShadowWPOAtlasRes.X, GShadowWPOAtlasRes.Y);
	ShadowWPOTextureAtlas->RenderTargetFormat = RTF_RG16f;
	ShadowWPOTextureAtlas->bSupportsUAV = true;
	
	ShadowWPOTextureAtlas->UpdateResource();
	FlushRenderingCommands();

	const FTextureRenderTargetResource* ShadowWPOResource =
		ShadowWPOTextureAtlas->GameThread_GetRenderTargetResource();
	
	ENQUEUE_RENDER_COMMAND(SetGrassDensityAtlasRTResource)
	(
		[=, RenderSystem = RenderSystem.Get()](FRHICommandListImmediate& RHICmd)
		{
			/*RenderSystem->SetGrassDensityRTResource_RenderThread(GrassDensityRTResource);*/
			RenderSystem->SetShadowWPOResource_RenderThread(ShadowWPOResource);
		}
	);
}

void UCustomGrassWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();

	OnGrassDataAssetLoadDelegate.RemoveAll(this);
	OnCVarGrassEnableChangeDelegate.RemoveAll(this);

	// Synchronous shutdown: make sure that all rendering commands referencing the render system
	// (through lambdas) finish before dismantling it.
	FlushRenderingCommands();
	RenderSystem = nullptr;

	LandscapeTiles.Empty();
}

void UCustomGrassWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	
	TArray<AActor*> LandscapeActors;
	UGameplayStatics::GetAllActorsOfClass(&InWorld, ALandscape::StaticClass(), LandscapeActors);

	for (const AActor* Actor : LandscapeActors)
	{
		if (const auto* Landscape = Cast<ALandscape>(Actor))
		{
			LandscapeTiles.Append(Landscape->LandscapeComponents);
		}
	}
	
	LandscapeActor = Cast<ALandscape>(LandscapeActors[0]);

	if (GrassDataAsset)
	{
		SetupShadowProxyMaterial();
		RecomputeRunningState();
	}
}

bool UCustomGrassWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	bool bIsPlaying = (World->WorldType == EWorldType::Game) || (World->WorldType == EWorldType::PIE);
	
	return World && bIsPlaying;
}

void UCustomGrassWorldSubsystem::SpawnComponents()
{
	check(GrassDataAsset);
	
	UWorld& World = GetWorldRef();
	
	for (int32 i = 0; i < LandscapeTiles.Num(); i++)
	{
		UCustomGrassPrimitiveComponent* Component = NewObject<UCustomGrassPrimitiveComponent>(
			LandscapeTiles[i]->GetOwner(),
			UCustomGrassPrimitiveComponent::StaticClass(),
			*FString::Printf(TEXT("CustomGrassTile_[%d]"), i)
		);
		
		UCustomGrassShadowProxyComponent* ShadowProxy = NewObject<UCustomGrassShadowProxyComponent>(
			LandscapeTiles[i]->GetOwner(),
			UCustomGrassShadowProxyComponent::StaticClass(),
			*FString::Printf(TEXT("CustomGrassShadowProxy_[%d]"), i)
		);

		ShadowProxy->PlaneResolution = GrassDataAsset->ProxyResolution;

		ShadowProxy->RegisterComponentWithWorld(&World);
		ShadowProxy->AttachToComponent(LandscapeTiles[i], FAttachmentTransformRules::KeepRelativeTransform);
		ShadowProxy->BuildMesh(LandscapeTiles[i], GrassDataAsset);
		check(ShadowProxyMID);
		ShadowProxy->SetMaterial(0, ShadowProxyMID);

		Component->ShadowProxy = ShadowProxy;
		
		Component->Material		 = GrassDataAsset->Material; 
		Component->LandscapeTile = LandscapeTiles[i];

		Component->RegisterComponentWithWorld(&World);
		Component->AttachToComponent(LandscapeTiles[i], FAttachmentTransformRules::KeepRelativeTransform);
				
		GrassTileComponents.Add(Component);
		ShadowProxyComponents.Add(ShadowProxy);
	}
}

void UCustomGrassWorldSubsystem::DespawnComponents()
{
	for (UCustomGrassPrimitiveComponent* Component : GrassTileComponents)
	{
		Component->DestroyComponent();
	}

	GrassTileComponents.Empty();
	ShadowProxyComponents.Empty();
}

void UCustomGrassWorldSubsystem::OnCVarChanged(bool bNewValue)
{
	RecomputeRunningState();
}

void UCustomGrassWorldSubsystem::OnDataAssetChanged()
{
	const auto* Settings = GetDefault<UCustomGrassSettings>();
	
	if (const UCustomGrassDataAsset* NewAsset = Settings->GrassDataAsset.LoadSynchronous();
		NewAsset != GrassDataAsset)
	{
		GrassDataAsset = NewAsset;
		RecomputeRunningState();
	}
}

void UCustomGrassWorldSubsystem::SetupShadowProxyMaterial()
{
	ShadowProxyMID = UMaterialInstanceDynamic::Create(
		GrassDataAsset->ShadowProxyMaterial, this
	);

	/*ShadowProxyMID->SetTextureParameterValue(
		TEXT("GrassDensityTextureAtlas"), GrassDensityTextureAtlas);*/
		
	ShadowProxyMID->SetTextureParameterValue(
		TEXT("ShadowWPOTextureAtlas"), ShadowWPOTextureAtlas);	

	ShadowProxyMID->SetVectorParameterValue(
		TEXT("LandscapeWorldOrigin"), FLinearColor(LandscapeActor->GetActorLocation()));

	ShadowProxyMID->SetScalarParameterValue(
		TEXT("LandscapeWorldSize"), GetLandscapeExtentInWorldUnits(LandscapeActor).X);

	ShadowProxyMID->SetScalarParameterValue(
		TEXT("AtlasGridSize"), GMaxRenderedTiles / 2);

	ShadowProxyMID->SetScalarParameterValue(
		TEXT("MaxGrassHeight"), GMaxGrassBladeHeight);

	/*ShadowProxyMID->SetScalarParameterValue(
		TEXT("MaxDisplacement"), GetMaxDisplacement(LandscapeActor));*/
}

void UCustomGrassWorldSubsystem::RecomputeRunningState()
{
	bool bWasActive = bIsActive;
	
	bool bCVarEnabled		  = CVarCustomGrassEnabled.GetValueOnGameThread() == 1;
	bool bIsDataAssetLoaded	  = GrassDataAsset != nullptr;

	bool bIsNowActive = bCVarEnabled && bIsDataAssetLoaded;

	if (bIsNowActive == bWasActive)
		return;

	bIsActive = bIsNowActive;

	bIsNowActive ? SpawnComponents() : DespawnComponents();

	for (int32 i = 0; i < LandscapeTiles.Num(); i++)
	{
		auto ShadowProxy = ShadowProxyComponents[i];
		ShadowProxy->PlaneResolution = GrassDataAsset->ProxyResolution;
		ShadowProxy->SetVisibility(GrassDataAsset->bDebugShowProxyMesh);
		ShadowProxy->BuildMesh(LandscapeTiles[i], GrassDataAsset);
		
		ShadowProxy->MarkRenderStateDirty();
	}

	const auto DataAssetProxy = FCustomGrassRenderSystem::FDataAssetProxy(GrassDataAsset);

	/*float MaxDisplacement = GetMaxDisplacement(LandscapeActor);*/
	
	// Mirrors state change on the RT through the render system

	ENQUEUE_RENDER_COMMAND(SetGrassRendererReady)
	(
		[=, RenderSystem = RenderSystem.Get()](FRHICommandListImmediate& RHICmdList)
		{
			check(RenderSystem);
				
			RenderSystem->bIsActive = bIsNowActive;

			if (bIsDataAssetLoaded)
			{
				RenderSystem->DataAssetProxy = DataAssetProxy;
			}

			/*RenderSystem->SetMaxDisplacement_RenderThread(MaxDisplacement);*/
		}
	);
}

void UCustomGrassWorldSubsystem::Tick(float DeltaTime)
{
#if WITH_EDITOR
	const auto DataAssetProxy = FCustomGrassRenderSystem::FDataAssetProxy(GrassDataAsset);

	ENQUEUE_RENDER_COMMAND(UpdateDataAsset)
	(
		[=, RenderSystem = RenderSystem.Get()](FRHICommandListImmediate& RHICmdList)
		{
			RenderSystem->DataAssetProxy = DataAssetProxy;
		}
	);
#endif
}

FVector2D GetLandscapeExtentInWorldUnits(const ALandscape* Landscape)
{
	FIntRect LandscapeExtent = Landscape->GetLandscapeInfo()->GetCompleteLandscapeExtent();
	check(LandscapeExtent.Width() == LandscapeExtent.Height());
		
	return FVector2D(
		LandscapeExtent.Width() * Landscape->GetActorScale3D().X,
		LandscapeExtent.Height() * Landscape->GetActorScale3D().Y);
}

/*
float GetMaxDisplacement(const ALandscape* Landscape)
{
	return 2.f * Landscape->GetCompleteBounds().GetExtent().Z + GMaxGrassBladeHeight;
}
*/
