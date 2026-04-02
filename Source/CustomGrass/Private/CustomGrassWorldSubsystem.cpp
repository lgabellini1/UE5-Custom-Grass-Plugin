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

/** Per-frame buffers as RDG resources. */
struct FVolatileBuffers
{
	FRDGBufferRef	 InstanceDataBuffer;
	FRDGBufferSRVRef InstanceDataBufferSRV;
	FRDGBufferUAVRef InstanceDataBufferUAV;

	FRDGBufferRef	 InstanceCounter;
	FRDGBufferSRVRef InstanceCounterSRV;
	FRDGBufferUAVRef InstanceCounterUAV;

	TStaticArray<FRDGBufferRef,
		FCustomGrassRenderSystem::MaxRenderedTiles>	IndirectDrawArgs;
	TStaticArray<FRDGBufferUAVRef,
		FCustomGrassRenderSystem::MaxRenderedTiles> IndirectDrawArgsUAV;
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

void FCustomGrassRenderSystem::BeginFrame(FRDGBuilder& GraphBuilder)
{
	RDG_EVENT_SCOPE(GraphBuilder, "CustomGrass");
	
	check(IsInRenderingThread());

	if (!bIsActive)
		return;

#if WITH_EDITOR
	UE_LOG(LogTemp, Display, TEXT("--- CustomGrass: BeginFrame ---"));
#endif
	
	if (QueuedWork.Num() > MaxRenderedTiles)
	{
		// Prioritize proxy rendering according to distance to camera
		QueuedWork.Sort([](const FWorkDesc& A, const FWorkDesc& B)
		{
			return A.CameraDistanceSq < B.CameraDistanceSq;
		});

		QueuedWork.SetNum(MaxRenderedTiles);
	}

	for (int32 i = 0; i < QueuedWork.Num(); i++)
	{
		// Handle re-assignment: take the handle from each proxy to be rendered
		// and make it point to the correct buffer. Somewhat of a hack and not very
		// clean architecturally, but it works as a solution for this circular dependency
		// between render system and proxy.
		
		const FWorkDesc& Work = QueuedWork[i];

		int32 TileOffset = i * InstanceCountPerTile.X * InstanceCountPerTile.Y;
		
		FRenderingResourceHandles& ResourceHandles = *Work.ResourceHandles;
		ResourceHandles.IndirectDrawArgs = TryGetRHI(IndirectDrawArgsBuffer[i]);
		ResourceHandles.TileOffset = TileOffset;
		ResourceHandles.WindParams = DataAssetProxy.WindParams;
		ResourceHandles.WindParams.Time = Work.View->Family->Time.GetWorldTimeSeconds();
		ResourceHandles.ViewSpaceCorrection = DataAssetProxy.ViewSpaceCorrection;
		ResourceHandles.ShortHeightThreshold = DataAssetProxy.ShortHeightThreshold;
		ResourceHandles.NormalRoundnessStrength = DataAssetProxy.NormalRoundnessStrength;
	}

	if (QueuedWork.Num() > 0)
	{
		FVolatileBuffers Buffers;
		InitPerFrameResources(GraphBuilder, Buffers);

		SubmitWork(GraphBuilder, Buffers);
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
		INDEX_NONE,
		FWindParams(GBlackTexture->GetTextureRHI(), TStaticSamplerState<>::GetRHI(),
		FVector2f::Zero(), 0.f)
	);
}

void FCustomGrassRenderSystem::AddRenderingWork(const FSceneView* View, float CameraDistanceSq,
	const FProxyLandscapeData* LandscapeData, const TSharedRef<FRenderingResourceHandles>& ResourceHandles)
{
	check(IsInAnyRenderingThread());

	QueuedWork.Add(FWorkDesc(View, CameraDistanceSq, LandscapeData, ResourceHandles));
}

void FCustomGrassRenderSystem::SubmitWork(FRDGBuilder& GraphBuilder, FVolatileBuffers& InBuffers)
{
	check(IsInRenderingThread());
	
	AddClearUAVPass(GraphBuilder, InBuffers.InstanceCounterUAV, 0);
	
	for (int32 i = 0; i < QueuedWork.Num(); i++)
	{
		const FWorkDesc& WorkDesc = QueuedWork[i];
		
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
	auto* Tile = Work.LandscapeData;

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
	GrassParams.Height				= DataAssetProxy.Height;
	GrassParams.Width				= DataAssetProxy.Width;
	GrassParams.Tilt				= DataAssetProxy.Tilt;
	GrassParams.Bend				= DataAssetProxy.Bend;
	GrassParams.ClumpStrength		= DataAssetProxy.ClumpStrength;
	GrassParams.ClumpGridSize		= DataAssetProxy.ClumpGridSize;
	GrassParams.ClumpFacingType		= static_cast<uint8>(DataAssetProxy.ClumpFacingType);
	GrassParams.ClumpFacingStrength = DataAssetProxy.ClumpFacingStrength;

	GrassParams.RandHeight		  = DataAssetProxy.RandHeight;
	GrassParams.RandWidth		  = DataAssetProxy.RandWidth;
	GrassParams.RandTilt		  = DataAssetProxy.RandTilt;
	GrassParams.RandBend		  = DataAssetProxy.RandBend;
	GrassParams.RandClumpStrength = DataAssetProxy.RandClumpStrength;

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
	Params->LandscapeWorldToLocal = Tile->WorldToLocal;
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

TStaticArray<FVector4f, 4> FCustomGrassRenderSystem::BuildFrustumPlanes_RenderThread(const FSceneView* const View)
{
	check(IsInAnyRenderingThread());
	
	auto FrustumPlanesArray = TStaticArray<FVector4f, 4>();

	const FMatrix& ViewToWorld = View->ViewMatrices.GetInvViewMatrix();
	
	for (int32 i = 0; i < FrustumPlanesArray.Num(); i++)
	{
		const FPlane& Plane = View->ViewFrustum.Planes[i];
		FPlane WorldPlane = Plane.TransformBy(ViewToWorld);
		FrustumPlanesArray[i] = FVector4f(WorldPlane.X, WorldPlane.Y, WorldPlane.Z, WorldPlane.W);
	}

	return FrustumPlanesArray;
}

void FCustomGrassRenderSystem::InitPerFrameResources(FRDGBuilder& GraphBuilder, FVolatileBuffers& OutBuffers) const
{
	// Instance data buffer
	
	OutBuffers.InstanceDataBuffer	 = GraphBuilder.RegisterExternalBuffer(InstanceDataBuffer);
	OutBuffers.InstanceDataBufferSRV = GraphBuilder.CreateSRV(OutBuffers.InstanceDataBuffer);
	OutBuffers.InstanceDataBufferUAV = GraphBuilder.CreateUAV(OutBuffers.InstanceDataBuffer);

	// Instance counter

	const int32 NumVisibleTiles = QueuedWork.Num();
	
	const FRDGBufferDesc InstanceCounterDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumVisibleTiles);
	
	OutBuffers.InstanceCounter	  = GraphBuilder.CreateBuffer(InstanceCounterDesc, TEXT("InstanceCounter"));
	OutBuffers.InstanceCounterSRV = GraphBuilder.CreateSRV(
		FRDGBufferSRVDesc(OutBuffers.InstanceCounter, PF_R32_UINT));
	OutBuffers.InstanceCounterUAV = GraphBuilder.CreateUAV(
		FRDGBufferUAVDesc(OutBuffers.InstanceCounter, PF_R32_UINT));
	// @note: Typed buffers get stride from format

	// Indirect draw args

	for (int32 i = 0; i < IndirectDrawArgsBuffer.Num(); i++)
	{
		OutBuffers.IndirectDrawArgs[i]	  = GraphBuilder.RegisterExternalBuffer(IndirectDrawArgsBuffer[i]);
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
	const UCustomGrassSettings* Settings = GetDefault<UCustomGrassSettings>();
	GrassDataAsset = Settings->GrassDataAsset.LoadSynchronous();
}

void UCustomGrassWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();

	OnGrassDataAssetLoadDelegate.RemoveAll(this);

	// Synchronous shutdown: make sure that all rendering commands referencing the render system
	// (through lambdas) finish before dismantling it.
	FlushRenderingCommands();
	RenderSystem.Reset(nullptr);

	LandscapeTiles.Empty();
}

void UCustomGrassWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	
	TArray<AActor*> LandscapeActors;
	UGameplayStatics::GetAllActorsOfClass(&InWorld, ALandscape::StaticClass(), LandscapeActors);

	for (auto* Actor : LandscapeActors)
	{
		if (const ALandscape* Landscape = Cast<ALandscape>(Actor))
		{
			LandscapeTiles.Append(Landscape->LandscapeComponents);
		}
	}

	if (GrassDataAsset)
		RecomputeRunningState();
}

bool UCustomGrassWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	UWorld* World = Cast<UWorld>(Outer);
	
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

FCustomGrassRenderSystem* UCustomGrassWorldSubsystem::GetRenderSystem_GameThread() const
{
	check(IsInGameThread());
	return RenderSystem.Get();
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
	for (auto Component : GrassTileComponents)
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
	const UCustomGrassSettings* Settings = GetDefault<UCustomGrassSettings>();
	
	if (UCustomGrassDataAsset* NewAsset = Settings->GrassDataAsset.LoadSynchronous();
		NewAsset != GrassDataAsset)
	{
		GrassDataAsset = NewAsset;
		RecomputeRunningState();
	}
}

void UCustomGrassWorldSubsystem::RecomputeRunningState()
{
	check(RenderSystem);

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
