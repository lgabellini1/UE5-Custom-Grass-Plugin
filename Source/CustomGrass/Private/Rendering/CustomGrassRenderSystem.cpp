#include "CustomGrassRenderSystem.h"
#include "ConsoleVars.h"
#include "Constants.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassGlobalShaders.h"
#include "CustomGrassSceneProxy.h"
#include "RenderGraphUtils.h"
#include "ShaderTypes.h"
#include "Utilities.h"
#include "SceneViewExtension.h"
#include "Engine/TextureRenderTarget2D.h"

class FCustomGrassSceneViewExtension final : public FSceneViewExtensionBase
{
public:
	FCustomGrassSceneViewExtension(const FAutoRegister& AutoRegister, FCustomGrassRenderSystem* InRenderSystem)
		: FSceneViewExtensionBase(AutoRegister), RenderSystem(InRenderSystem)
	{}
	
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override
	{
		checkf(RenderSystem, TEXT("SceneViewExtension outlives render system!"));

		checkf(RenderSystem->SelectedWork.IsEmpty(),
			TEXT("Selected rendering work should be empty at start of frame!"))

		// If the view has not changed, reuse previous frame's work. Useful to
		// avoid flickering issues due to race conditions between grass tiles.
		
		if (!RenderSystem->IsViewSameBetweenFrames())
		{
			for (const auto& Proxy : RenderSystem->RegisteredProxies)
    		{
    			RenderSystem->SelectProxyIfRelevant(Proxy, InView);
    		}	
		}

		RenderSystem->PrepareSelectedWorkForRendering(GraphBuilder);
	}

private:
	FCustomGrassRenderSystem* RenderSystem;
};

namespace CustomGrass 
{
	template <class ResourceType, class SRVType, class UAVType>
	struct TResourceAccess
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

const uint32 InstanceCountPerTile = CustomGrass::GetInstanceCount(ECustomGrassLOD::LOD0).X *
	CustomGrass::GetInstanceCount(ECustomGrassLOD::LOD0).Y;

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

ENUM_CLASS_FLAGS(FCustomGrassRenderSystem::ERenderSystemState);

FCustomGrassRenderSystem::FCustomGrassRenderSystem(const UCustomGrassDataAsset& DataAsset,
	const CustomGrass::FTextureRenderTargetsGT& RenderTargets)
: SceneViewExtension(FSceneViewExtensions::NewExtension<FCustomGrassSceneViewExtension>(this)),
DataAssetProxy(DataAsset)
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
			
			SelectedWork.Reserve(CustomGrass::MaxRenderedTiles);

			// Required because the lambda may run after the first call of BeginFrame()
			EnumAddFlags(SystemState, ERenderSystemState::BuffersInitialized);
		}
	);

	if (RenderTargets.ShadowMapTextureAtlas)
	{
		CreateShadowMapResource(*RenderTargets.ShadowMapTextureAtlas);		
	}
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

	DestroyShadowMapResourceIfSet();
}

void FCustomGrassRenderSystem::CreateShadowMapResource(UTextureRenderTarget2D& ShadowMap)
{
	ENQUEUE_RENDER_COMMAND(CreateShadowMapAtlasPooledResource)
	(
		[this, ShadowMapResource = ShadowMap.GameThread_GetRenderTargetResource()]
		(FRHICommandListImmediate& RHICmdList)
		{
			ShadowMapTextureAtlas = CreateRenderTarget(ShadowMapResource->GetRenderTargetTexture(),
				TEXT("ShadowMapTextureAtlas"));
			
			EnumAddFlags(SystemState, ERenderSystemState::ShadowMapInitialized);
		}
	);
}

void FCustomGrassRenderSystem::DestroyShadowMapResourceIfSet()
{
	ENQUEUE_RENDER_COMMAND(DestroyShadowMapPooledResource)
	(
		[ShadowMap = MoveTemp(ShadowMapTextureAtlas), StateCopy = SystemState]
		(FRHICommandListImmediate& RHICmdList) mutable
		{
			if (EnumHasAllFlags(StateCopy, ERenderSystemState::ShadowMapInitialized))
			{
				ShadowMap.SafeRelease();
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
	return EnumHasAllFlags(SystemState, ERenderSystemState::BuffersInitialized);
}

bool FCustomGrassRenderSystem::IsShadowMapInitialized() const
{
	check(IsInAnyRenderingThread());
	return EnumHasAllFlags(SystemState, ERenderSystemState::ShadowMapInitialized);
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

	if (!SelectedWork.IsEmpty())
	{
		const CustomGrass::FVolatileBuffers& Buffers = CreatePerFrameResources(GraphBuilder);
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

	PrevFrameSelectedWork = MoveTemp(SelectedWork);
	SelectedWork.Reset();
}

void FCustomGrassRenderSystem::SelectProxyIfRelevant(const FRegisteredProxy& RegisteredProxy, const FSceneView& View)
{
	if (RegisteredProxy.Proxy->IsShown(&View))
	{
		const CustomGrass::FProxyLandscapeData& LandscapeData = RegisteredProxy.Proxy->GetLandscapeData();

		const float TilePriorityScore = CalcTilePriorityScore(&View, LandscapeData);
		ECustomGrassLOD AssignedLOD	  = AssignTileLOD(&View, LandscapeData);

		auto VSData = MakeUnique<CustomGrass::FProxyVertexShaderData>(AssignedLOD, View.Family->Time);
			
		SelectedWork.Push(CustomGrass::FProxyRenderWorkDesc{
			View.ViewMatrices.GetViewOrigin(),
			View.ViewMatrices.GetViewProjectionMatrix(),
			RegisteredProxy.Proxy,
			MoveTemp(VSData),
			nullptr,
			TilePriorityScore
		});
	}
}

void FCustomGrassRenderSystem::PrepareSelectedWorkForRendering(FRDGBuilder& GraphBuilder) 
{
	SelectedWork.Sort([](
		const CustomGrass::FProxyRenderWorkDesc& A,
		const CustomGrass::FProxyRenderWorkDesc& B)
	{
		return A.TilePriorityScore > B.TilePriorityScore;
	});

	if (SelectedWork.Num() > CustomGrass::MaxRenderedTiles)
	{
		SelectedWork.SetNum(CustomGrass::MaxRenderedTiles);
	}
	
	for (int32 i = 0; i < SelectedWork.Num(); i++)
	{
		CustomGrass::FProxyRenderWorkDesc& Work = SelectedWork[i];
		
		auto& [ InstanceData, IndirectDrawArgs] = Work.VSData->RenderingResources;
		InstanceData	 = TryGetSRV(InstanceDataBuffer);
		IndirectDrawArgs = TryGetRHI(IndirectDrawArgsBuffer[i]);

		checkf(InstanceData, TEXT("CustomGrass: TryGetSRV() failed! Null InstanceDataBuffer."));
		checkf(InstanceData, TEXT("CustomGrass: TryGetRHI() failed! Null IndirectDrawArgsBuffer."));

		Work.VSData->TileIndex		  = i;
		Work.VSData->TileBufferOffset = i * InstanceCountPerTile;
		
		Work.HeightmapTexture = RegisterExternalTexture(GraphBuilder,
			Work.Proxy->GetLandscapeData().HeightmapTexture,
			*(FString::Printf(TEXT("Heightmap_[%d]"), i)));
		
		TileDebugChannel.SetDebugStateForWork(i, Work);
	}
	
	TileDebugChannel.PublishDebugStateSnapshot_RenderThread();	
}

void FCustomGrassRenderSystem::RebuildRenderStateFromGameThread(const UCustomGrassDataAsset& DataAsset)
{
	check(IsInGameThread());
	
	BuildDataAssetProxy(DataAsset);
}

bool FCustomGrassRenderSystem::IsViewSameBetweenFrames() const
{
	check(IsInAnyRenderingThread());
	
	if (SelectedWork.IsEmpty() || PrevFrameSelectedWork.IsEmpty())
	{
		return false;
	}

	return PrevFrameSelectedWork[0].ViewMatrix.Equals(
		SelectedWork[0].ViewMatrix);
}

CustomGrass::FVertexShaderParams FCustomGrassRenderSystem::GetVertexShaderDataAssetParams() const
{
	check(IsInAnyRenderingThread());
	
	return CustomGrass::FVertexShaderParams(
		DataAssetProxy.ViewSpaceCorrection,
		DataAssetProxy.NormalRoundnessStrength,
		DataAssetProxy.ShortHeightThreshold
	);
}

ECustomGrassLOD FCustomGrassRenderSystem::AssignTileLOD(
	const FSceneView* View,
	const CustomGrass::FProxyLandscapeData& LandscapeData) const
{
	check(IsInAnyRenderingThread());
	
	ECustomGrassLOD AssignedLOD = ECustomGrassLOD::LOD2;
	
	if (DataAssetProxy.bFixedLOD)
	{
		AssignedLOD = DataAssetProxy.GlobalLOD;
	}
	else
	{
		const FVector Camera = View->ViewMatrices.GetViewOrigin();
		const FVector NearestTileBounds = GetNearestTileBoundsPointFromCamera(View, LandscapeData);
	
		const float CameraToTileDist = FVector::Distance(Camera, NearestTileBounds);

		for (ECustomGrassLOD LOD : TEnumRange<ECustomGrassLOD>())
		{
			if (CameraToTileDist <= CustomGrass::GetDistanceThreshold(LOD))
			{
				AssignedLOD = LOD;
				break;
			}
		}
	}

	return AssignedLOD;
}

void FCustomGrassRenderSystem::RegisterProxy(const FCustomGrassSceneProxy& Proxy)
{
	check(IsInGameThread());

	ENQUEUE_RENDER_COMMAND(RegisterCustomGrassProxyToRenderSystem)
	(
		[this, ProxyPtr = &Proxy](FRHICommandListImmediate& RHICmdList)
		{
			const auto RegisteredProxy = FRegisteredProxy(ProxyPtr->GetPrimitiveComponentId(), ProxyPtr);

			if (FRegisteredProxy* Found = RegisteredProxies.FindByPredicate(
				[&RegisteredProxy](const FRegisteredProxy& Entry)
				{
					return Entry == RegisteredProxy;
				}))
			{
				Found->Proxy = ProxyPtr;
			}
			else
			{
				RegisteredProxies.Add(RegisteredProxy);
			}
		}
	);
}

void FCustomGrassRenderSystem::UnregisterProxy(const FCustomGrassSceneProxy& Proxy)
{
	check(IsInAnyRenderingThread());

	ENQUEUE_RENDER_COMMAND(UnregisterCustomGrassProxyToRenderSystem)
	(
		[this, ProxyPtr = &Proxy](FRHICommandListImmediate& RHICmdList)
		{
			RegisteredProxies.RemoveAll([ProxyPtr](const FRegisteredProxy& Entry)
			{
				return Entry.Proxy == ProxyPtr;
			});
		}
	);
}

CustomGrass::FProxyVertexShaderData* FCustomGrassRenderSystem::GetProxyRenderResources(
	const FCustomGrassSceneProxy& Proxy) const
{
	check(IsInAnyRenderingThread());
	
	if (!RegisteredProxies.Contains(FRegisteredProxy(Proxy.GetPrimitiveComponentId(), &Proxy)))
		return nullptr;

	const auto* ProxyAsSelected = SelectedWork.FindByPredicate(
		[&Proxy](const CustomGrass::FProxyRenderWorkDesc& Work)
	{
		return Work.Proxy == &Proxy;
	});

	return ProxyAsSelected ? ProxyAsSelected->VSData.Get() : nullptr; 
}

void FCustomGrassRenderSystem::CreateTileAtlasMapping(FRDGBuilder& GraphBuilder)
{
	check(IsInAnyRenderingThread());
	
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
	check(IsInAnyRenderingThread());
	
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
	check(IsInAnyRenderingThread());
	
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

CustomGrass::FLandscapeParams FCustomGrassRenderSystem::BuildLandscapeParams(
	FRDGBuilder& GraphBuilder,
	int32 TileIndex,
	const CustomGrass::FProxyLandscapeData& LandscapeTile) const
{
	check(IsInAnyRenderingThread());

	checkf(SelectedWork[TileIndex].HeightmapTexture, TEXT("CustomGrass: Warning! Heightmap not set."))
	
	CustomGrass::FLandscapeParams LandscapeParams;
	
	LandscapeParams.TileSizeInQuads		  = LandscapeTile.ComponentSizeQuads;
	LandscapeParams.LandscapeSizeInQuadsX = LandscapeTile.TotalSizeInQuads.X;
	LandscapeParams.LandscapeSizeInQuadsY = LandscapeTile.TotalSizeInQuads.Y;
	LandscapeParams.QuadOffsetFromOriginX = LandscapeTile.SectionBase.X;
	LandscapeParams.QuadOffsetFromOriginY = LandscapeTile.SectionBase.Y;
	LandscapeParams.LandscapeLocalToWorld = FMatrix44f(LandscapeTile.LocalToWorldMatrix);
	LandscapeParams.HeightmapTexture	  = GraphBuilder.CreateSRV(SelectedWork[TileIndex].HeightmapTexture);
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
	Params->InstanceCountPerTileX = CustomGrass::GetInstanceCount(Work.VSData->LOD).X;
	Params->InstanceCountPerTileY = CustomGrass::GetInstanceCount(Work.VSData->LOD).Y;
	Params->BufferRegionSize	  = InstanceCountPerTile;
#if WITH_EDITOR
	Params->ViewProjectionMatrix  = bFrozenViewFrustum ? CachedViewFrustum : FMatrix44f(Work.ViewMatrix);
#else
	Params->ViewProjectionMatrix  = FMatrix44f(Work.ViewMatrix);
#endif
	Params->ViewOrigin			  = FVector4f(FLinearColor(Work.ViewOrigin));
	Params->MaxRenderDistance	  = DataAssetProxy.MaxRenderDistance;
	Params->LandscapeParams		  = BuildLandscapeParams(GraphBuilder, TileIndex, Tile);
	Params->GrassParams			  = BuildGrassParams();
	Params->ShadowParams		  = BuildShadowParams(GraphBuilder, TileIndex, Buffers);

	if (DataAssetProxy.bShadowsOn)
	{
		check(Params->InstanceCountPerTileX >= CustomGrass::ShadowMapTextureSlotResolution.X);
		check(Params->InstanceCountPerTileY >= CustomGrass::ShadowMapTextureSlotResolution.Y);
	}
	
	const FIntVector ThreadCount = FIntVector(CustomGrass::GetInstanceCount(Work.VSData->LOD).X,
		CustomGrass::GetInstanceCount(Work.VSData->LOD).Y, 1); // Total thread count, split among groups
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
	Params->GrassBladeVertexCount	  = CustomGrass::GetGrassBladeVertexCount(Work.VSData->LOD);

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

	if (IsShadowMapInitialized())
	{
		Buffers.ShadowMapTextureAtlas.Resource = GraphBuilder.RegisterExternalTexture(ShadowMapTextureAtlas);
		Buffers.ShadowMapTextureAtlas.UAV	   = GraphBuilder.CreateUAV(Buffers.ShadowMapTextureAtlas.Resource);
	}

	Buffers.DensityAccumTextureAtlas.Resource = GraphBuilder.CreateTexture(DensityAccumAtlasDesc,
		TEXT("DensityAccumAtlas"));
	Buffers.DensityAccumTextureAtlas.UAV	  = GraphBuilder.CreateUAV(Buffers.DensityAccumTextureAtlas.Resource);
	Buffers.DensityAccumTextureAtlas.SRV	  = GraphBuilder.CreateSRV(Buffers.DensityAccumTextureAtlas.Resource);

	return Buffers;
}

void FCustomGrassRenderSystem::BuildDataAssetProxy(const UCustomGrassDataAsset& DataAsset)
{
	check(IsInGameThread());

	ENQUEUE_RENDER_COMMAND(RebuildDataAssetProxy)
	(
		[this, NewDataAssetProxy = FDataAssetProxy(DataAsset)]
		(FRHICommandListImmediate& RHICmdList) mutable
		{
			DataAssetProxy = MoveTemp(NewDataAssetProxy);
		}
	);
}

void FCustomGrassRenderSystem::CompareAndCheckResourcesValidity(
	const CustomGrass::FProxyVertexShaderData* VSData) const
{
	if (VSData->TileIndex < SelectedWork.Num())
	{
		checkf(VSData == SelectedWork[VSData->TileIndex].VSData.Get(),
			TEXT("CustomGrass: proxy tries to render with different VSData from the one handed!"));
	}
	
	checkf(VSData->RenderingResources.InstanceData, TEXT("InstanceData resource handle is NULL!"))
	checkf(VSData->RenderingResources.IndirectDrawArgs, TEXT("IndirectDrawArgs resource handle is NULL!"))

	checkf(VSData->RenderingResources.InstanceData == InstanceDataBuffer->GetSRV(),
		TEXT("CustomGrass: InstanceData resource handle "
	   "doesn't correspond with authoritative InstanceDataBuffer"));
	
	checkf(VSData->RenderingResources.IndirectDrawArgs == IndirectDrawArgsBuffer[VSData->TileIndex]->GetRHI(),
		TEXT("CustomGrass: IndirectDrawArgsBuffer resource handle "
	   "doesn't correspond with authoritative IndirectDrawArgsBuffer"));
}

void CustomGrass::FTileDebugChannel::SetDebugStateForWork(int32 WorkIndex, const FProxyRenderWorkDesc& Work)
{
	check(IsInAnyRenderingThread());
	
	DebugState[WorkIndex] = FTileDebugInfoRT(Work.Proxy->GetPrimitiveComponentId(), Work.VSData->LOD);
}

void CustomGrass::FTileDebugChannel::PublishDebugStateSnapshot_RenderThread()
{
	check(IsInAnyRenderingThread());
	
	AsyncTask(ENamedThreads::GameThread,
		[this, DebugStateSnapshot = DebugState]()
	{
		if (this)
		{
			GTDebugStateSnapshot = DebugStateSnapshot;
		}
	});
}

CustomGrass::FRTDebugState CustomGrass::FTileDebugChannel::GetDebugStateSnapshot_GameThread() const
{
	check(IsInGameThread());
	
	return GTDebugStateSnapshot;
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

	bShadowsOn		   = DataAsset.bShadowsEnabled;
	ShadowProxyZOffset = DataAsset.ShadowProxyZOffset;

	bFixedLOD = DataAsset.bFixedLOD;
	GlobalLOD = DataAsset.GlobalLOD;

	WindParams = CustomGrass::FWindParams(
		DataAsset.NoiseTexture ? DataAsset.NoiseTexture->GetResource() : nullptr,
		DataAsset.WindDirection,
		DataAsset.WindStrength);
}
