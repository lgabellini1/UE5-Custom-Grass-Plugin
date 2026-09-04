#include "CustomGrassWorldSubsystem.h"
#include "CustomGrassConsoleVars.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassPrimitiveComponent.h"
#include "Rendering/CustomGrassShadowProxyComponent.h"
#include "Rendering/CustomGrassSceneProxy.h"
#include "CustomGrassSettings.h"
#include "Landscape.h"
#include "LandscapeStreamingProxy.h"
#include "SceneViewExtension.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "Delegates/Delegate.h"
#include "Engine/TextureRenderTarget2D.h"

void UCustomGrassWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	RenderSystem = MakeUnique<FCustomGrassRenderSystem>();

	// Set up delegates
	GrassDataAssetLoaded.AddUObject(this, &UCustomGrassWorldSubsystem::OnDataAssetChanged);
	CVarGrassEnabledChanged.AddUObject(this, &UCustomGrassWorldSubsystem::OnCVarChanged);

	// Try loading the data asset from the plugin settings; if not found the system won't start
	const auto* Settings = GetDefault<UCustomGrassSettings>();
	GrassDataAsset = Settings->GrassDataAsset.LoadSynchronous();

	// Shadows: create the texture atlas on the GT
	
	ShadowWPOTextureAtlas = NewObject<UTextureRenderTarget2D>();
	ShadowWPOTextureAtlas->InitAutoFormat(
		GShadowWPOAtlasRes.X, GShadowWPOAtlasRes.Y);
	ShadowWPOTextureAtlas->RenderTargetFormat = RTF_RG16f;
	ShadowWPOTextureAtlas->bSupportsUAV		  = true;
	ShadowWPOTextureAtlas->bAutoGenerateMips  = false;
	ShadowWPOTextureAtlas->Filter			  = TextureFilter::TF_Bilinear;
	
	ShadowWPOTextureAtlas->UpdateResource();
	FlushRenderingCommands();

	const FTextureRenderTargetResource* ShadowWPOResource =
		ShadowWPOTextureAtlas->GameThread_GetRenderTargetResource();
	
	ENQUEUE_RENDER_COMMAND(SetGrassDensityAtlasRTResource)
	(
		[=, RenderSystem = RenderSystem.Get()](FRHICommandListImmediate& RHICmd)
		{
			RenderSystem->SetShadowWPOResource_RenderThread(ShadowWPOResource);
		}
	);
}

void UCustomGrassWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();

	GrassDataAssetLoaded.RemoveAll(this);
	CVarGrassEnabledChanged.RemoveAll(this);

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
		ShadowProxy->SetCastShadow(GrassDataAsset->bShadowsEnabled);
		check(ShadowProxyMID);
		ShadowProxy->SetMaterial(0, ShadowProxyMID);

		Component->ShadowProxy = ShadowProxy;
		
		Component->Material			  = GrassDataAsset->GrassMaterial;
		Component->Material_NoTwoSide = GrassDataAsset->GrassMaterial_NoTwoSided;
		Component->LandscapeTile = LandscapeTiles[i];

		Component->SetIndex(i);

		Component->RegisterComponentWithWorld(&World);
		Component->AttachToComponent(LandscapeTiles[i], FAttachmentTransformRules::KeepRelativeTransform);
		Component->SetCastShadow(GrassDataAsset->bShadowsEnabled);
				
		GrassTileComponents.Add(Component);
		ShadowProxyComponents.Add(ShadowProxy);
		
#if DEBUG_DRAW_TILE_BOUNDS		
		uint8 Hue = (i * 37) % 255;
		FColor DebugColor = FLinearColor::MakeFromHSV8(Hue, 200, 255).ToFColor(true);
		DebugColor.A *= 0.25;

		FBox Bounds = LandscapeTiles[i]->Bounds.GetBox();
		Bounds = Bounds.ExpandBy(FVector(0, 0, 100.f), FVector(0, 0, 100.f));
		
		DrawDebugSolidBox(GetWorld(), Bounds, DebugColor, FTransform::Identity, true, -1, 1);
#endif
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

void UCustomGrassWorldSubsystem::OnCVarChanged()
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
}

void UCustomGrassWorldSubsystem::RecomputeRunningState()
{
	bool bWasActive = bIsActive;
	
	bool bCVarEnabled		  = CVarGrassEnabled.GetValueOnGameThread() == 1;
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
		ShadowProxy->SetCastShadow(GrassDataAsset->bShadowsEnabled);
		ShadowProxy->BuildMesh(LandscapeTiles[i], GrassDataAsset);
		
		ShadowProxy->MarkRenderStateDirty();

		auto GrassTile = GrassTileComponents[i];
		GrassTile->SetCastShadow(GrassDataAsset->bShadowsEnabled);

		GrassTile->MarkRenderStateDirty();
	}

	const auto DataAssetProxy = FDataAssetProxy(GrassDataAsset);
	
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
	const auto DataAssetProxy = FDataAssetProxy(GrassDataAsset);

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
