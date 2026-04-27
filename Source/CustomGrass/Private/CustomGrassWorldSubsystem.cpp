// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomGrassWorldSubsystem.h"
#include "CustomGrass.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassPrimitiveComponent.h"
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

IMPLEMENT_GLOBAL_SHADER(FInstanceGrassBladeCS, "/CustomShaders/Compute.usf", "CSInstanceGrassBlades", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FInitIndirectDrawArgsCS, "/CustomShaders/Compute.usf", "CSInitIndirectDrawArgs", SF_Compute);

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

	TStaticArray<FRDGBufferRef, MaxRenderedTiles> IndirectDrawArgs;
	TStaticArray<FRDGBufferUAVRef, MaxRenderedTiles> IndirectDrawArgsUAV;
};

FCustomGrassRenderSystem::FCustomGrassRenderSystem()
{
	check(GEngine);
	GEngine->GetPreRenderDelegateEx().AddRaw(this, &FCustomGrassRenderSystem::BeginFrame);
	GEngine->GetPostRenderDelegateEx().AddRaw(this, &FCustomGrassRenderSystem::EndFrame);
//	ViewExtension = FSceneViewExtensions::NewExtension<FCustomGrassViewExtension>(this);
	
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

//	ViewExtension = nullptr;
	
	ENQUEUE_RENDER_COMMAND(DestroyRTResources)
	(
		// Get ownership of buffers as 'this' will deterministically be destroyed
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

float FCustomGrassRenderSystem::CalcTileSortingScore(const FWorkDesc& Work)
{
	const FVector3f CameraPos = FVector3f(Work.View->ViewMatrices.GetViewOrigin());
	const FVector3f CameraToTile = GetTileCenter(*Work.LandscapeData) - CameraPos;
	const FVector3f CameraFwd = FVector3f(Work.View->GetViewDirection());

	const float Depth = FVector3f::DotProduct(CameraToTile, CameraFwd);
	
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
	
	TArray<FWorkDesc> SelectedWork;
	SelectedWork.Reserve(MaxRenderedTiles);

	for (const FWorkDesc& Work : QueuedWork)
	{
		if (Work.View->ViewFrustum.IntersectBox(FVector(GetTileCenter(*Work.LandscapeData)),
			FVector(GetTileExtent(*Work.LandscapeData))))
		{
			SelectedWork.Push(Work);
		}
	}
	
	if (SelectedWork.Num() > MaxRenderedTiles)
	{
		// Prioritize rendering according to distance to camera
		SelectedWork.Sort([](const FWorkDesc& A, const FWorkDesc& B)
		{
			return CalcTileSortingScore(A) > CalcTileSortingScore(B);
		});

		SelectedWork.SetNum(MaxRenderedTiles);
	}

	for (int32 i = 0; i < SelectedWork.Num(); i++)
	{
		// Handle re-assignment: take the handle from each proxy to be rendered
		// and make it point to the correct buffer. Somewhat of a hack and not very
		// clean architecturally, but it works as a solution for this circular dependency
		// between render system and proxy.
		
		const FWorkDesc& Work = SelectedWork[i];
		
		FRenderingResourceHandles& ResourceHandles = Work.ResourceHandles.Get();
		ResourceHandles.InstanceData	 = TryGetSRV(InstanceDataBuffer);
		ResourceHandles.IndirectDrawArgs = TryGetRHI(IndirectDrawArgsBuffer[i]);
		ResourceHandles.TileOffset		 = i * InstanceCountPerTile.X * InstanceCountPerTile.Y;
		/*
		ResourceHandles.WindParams = DataAssetProxy.WindParams;
		ResourceHandles.WindParams.Time = Work.View->Family->Time.GetWorldTimeSeconds();
		ResourceHandles.ViewSpaceCorrection = DataAssetProxy.ViewSpaceCorrection;
		ResourceHandles.ShortHeightThreshold = DataAssetProxy.ShortHeightThreshold;
		ResourceHandles.NormalRoundnessStrength = DataAssetProxy.NormalRoundnessStrength;
		*/
	}

	if (SelectedWork.Num() > 0)
	{
		FVolatileBuffers Buffers;
		InitPerFrameResources(GraphBuilder, Buffers);

		SubmitWork(GraphBuilder, Buffers, SelectedWork);
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
//	RegisteredProxies.Empty();
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

bool FCustomGrassRenderSystem::AddRenderingWork(const FSceneView* View, float CameraDistanceSqr,
	const FProxyLandscapeData* LandscapeData,
	const TSharedRef<FRenderingResourceHandles>& ResourceHandles,
	const FCustomGrassSceneProxy* Proxy,
	EGrassLOD LOD)
{
	check(IsInAnyRenderingThread());

	/* Ensure thread-safe writing of QueuedWork from the various worker threads, so that
	 * only one can insert at a time. */
	FScopeLock Lock(&AddRenderingWorkCS);
	
//	if (RegisteredProxies.Contains(Proxy))
//		return false;
	
/*	if (QueuedWork.Num() >= MaxRenderedTiles)
	{
		if (const float FarthestQueued = QueuedWork.Last().CameraDistanceSqr;
			CameraDistanceSqr >= FarthestQueued)
		{
			return false;
		}

		QueuedWork.Pop();
	}*/

//	RegisteredProxies.Add(Proxy);

/*	const int32 Slot = Algo::LowerBound(QueuedWork, CameraDistanceSqr,
		[](const FWorkDesc& Work, float CameraDistanceSqr)
		{
			return Work.CameraDistanceSqr < CameraDistanceSqr;
		});*/
	
	QueuedWork.Push(FWorkDesc{ View, CameraDistanceSqr, LandscapeData, ResourceHandles, LOD });
	
	return true;
}

void FCustomGrassRenderSystem::SubmitWork(FRDGBuilder& GraphBuilder, FVolatileBuffers& InBuffers, const TArray<FWorkDesc>& Work)
{
	check(IsInRenderingThread());
	
	AddClearUAVPass(GraphBuilder, InBuffers.InstanceCounterUAV, 0);
	
	for (int32 i = 0; i < Work.Num(); i++)
	{
		const FWorkDesc& WorkDesc = Work[i];
		
		AddComputePass_InstanceGrassBlades(GraphBuilder, WorkDesc, InBuffers, i);
		
		AddComputePass_InitIndirectDrawArgs(GraphBuilder, WorkDesc, InBuffers, i);
	}
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

	const FRDGTextureRef HeightmapRDG = RegisterExternalTexture(GraphBuilder, Tile->HeightmapTexture,
		*(FString::Printf(TEXT("Heightmap_[%d]"), TileIndex)));

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

	FGrassParams GrassParams;
	GrassParams.Height				= DataAssetProxy.Height.Val;
	GrassParams.Width				= DataAssetProxy.Width.Val;
	GrassParams.Tilt				= DataAssetProxy.Tilt.Val;
	GrassParams.Bend				= DataAssetProxy.Bend.Val;
	GrassParams.ClumpStrength		= DataAssetProxy.ClumpStrength.Val;
	GrassParams.ClumpGridSize		= DataAssetProxy.ClumpGridSize;
	GrassParams.ClumpFacingType		= static_cast<uint8>(DataAssetProxy.ClumpFacingType);
	GrassParams.ClumpFacingStrength = DataAssetProxy.ClumpFacingStrength;

	GrassParams.RandHeight		  = DataAssetProxy.Height.Random;
	GrassParams.RandWidth		  = DataAssetProxy.Width.Random;
	GrassParams.RandTilt		  = DataAssetProxy.Tilt.Random;
	GrassParams.RandBend		  = DataAssetProxy.Bend.Random;
	GrassParams.RandClumpStrength = DataAssetProxy.ClumpStrength.Random;

	FInstanceGrassBladeCS::FParameters* Params = GraphBuilder.AllocParameters<FInstanceGrassBladeCS::FParameters>();
	Params->OutInstanceDataBuffer = InBuffers.InstanceDataBufferUAV;
	Params->OutInstanceCounter	  = InBuffers.InstanceCounterUAV;
	Params->TileIndex			  = TileIndex;
	Params->InstanceCountPerTileX = InstanceCountPerTile.X;
	Params->InstanceCountPerTileY = InstanceCountPerTile.Y;
#if WITH_EDITOR
	Params->ViewProjectionMatrix = bFrozenViewFrustum ? CachedViewFrustum :
		FMatrix44f(Work.View->ViewMatrices.GetViewProjectionMatrix());
#else
	Params->ViewProjectionMatrix = FMatrix44f(Work.View->ViewMatrices.GetViewProjectionMatrix());
#endif
	Params->ViewOrigin			  = FVector4f(FLinearColor(Work.View->ViewMatrices.GetViewOrigin()));
	Params->MaxRenderDistance	  = DataAssetProxy.MaxRenderDistance;
	Params->HeightmapScaleBias    = Tile->HeightmapScaleBias;
	Params->ComponentSizeQuads    = Tile->ComponentSizeQuads;
	Params->SectionBaseX		  = Tile->SectionBase.X;
	Params->SectionBaseY		  = Tile->SectionBase.Y;
	Params->LandscapeLocalToWorld = Tile->LocalToWorld;
	Params->HeightmapTexture	  = GraphBuilder.CreateSRV(HeightmapRDG);
	Params->HeightmapSampler   = Tile->HeightmapSampler;
	Params->GrassParams			  = GrassParams;

	const FIntVector ThreadCount = FIntVector(InstanceCountPerTile.X, InstanceCountPerTile.Y, 1); // Total thread count, split among groups
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

void FCustomGrassRenderSystem::InitPerFrameResources(FRDGBuilder& GraphBuilder, FVolatileBuffers& OutBuffers) const
{
	check(IsInRenderingThread());
	
	// Instance data buffer
	
	OutBuffers.InstanceDataBuffer	 = GraphBuilder.RegisterExternalBuffer(InstanceDataBuffer);
	OutBuffers.InstanceDataBufferSRV = GraphBuilder.CreateSRV(OutBuffers.InstanceDataBuffer);
	OutBuffers.InstanceDataBufferUAV = GraphBuilder.CreateUAV(OutBuffers.InstanceDataBuffer);

	// Instance counter
	
	const FRDGBufferDesc InstanceCounterDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32),
		MaxRenderedTiles);
	
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

	if (GrassDataAsset)
		RecomputeRunningState();
}

bool UCustomGrassWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
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

		Component->Material		 = GrassDataAsset->Material; 
		Component->LandscapeTile = LandscapeTiles[i];

		Component->RegisterComponentWithWorld(&World);
		Component->AttachToComponent(LandscapeTiles[i], FAttachmentTransformRules::KeepRelativeTransform);
				
		GrassTileComponents.Add(Component);
	}
}

void UCustomGrassWorldSubsystem::DespawnComponents()
{
	for (TObjectPtr<UCustomGrassPrimitiveComponent> Component : GrassTileComponents)
	{
		Component->DestroyComponent();
	}

	GrassTileComponents.Empty();
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

void UCustomGrassWorldSubsystem::RecomputeRunningState()
{
	const bool bWasActive = bIsActive;
	
	const bool bCVarEnabled		  = CVarCustomGrassEnabled.GetValueOnGameThread() == 1;
	const bool bIsDataAssetLoaded = GrassDataAsset != nullptr;

	const bool bIsNowActive = bCVarEnabled && bIsDataAssetLoaded;

	if (bIsNowActive == bWasActive)
		return;

	bIsActive = bIsNowActive;

	bIsNowActive ? SpawnComponents() : DespawnComponents();

	const auto DataAssetProxy = FCustomGrassRenderSystem::FDataAssetProxy(GrassDataAsset);
	
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
