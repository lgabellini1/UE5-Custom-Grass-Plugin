#include "CustomGrassWorldSubsystem.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassPrimitiveComponent.h"
#include "CustomGrassSettings.h"
#include "Console/CustomGrassConsoleVars.h"
#include "Landscape.h"
#include "SceneViewExtension.h"
#include "Kismet/GameplayStatics.h"
#include "Delegates/Delegate.h"
#include "Engine/TextureRenderTarget2D.h"

ENUM_CLASS_FLAGS(UCustomGrassWorldSubsystem::EDirtyFlags);

void UCustomGrassWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const auto* Settings = GetDefault<UCustomGrassSettings>();
	GrassDataAsset = Settings->GrassDataAsset.LoadSynchronous();

	CustomGrass::CVarGrassEnabledChanged.AddUObject(this, &UCustomGrassWorldSubsystem::OnCVarGrassEnabledChanged);
	
	CustomGrass::DataAssetLoaded.AddUObject(this, &UCustomGrassWorldSubsystem::OnDataAssetLoaded);
	CustomGrass::DataAssetValuesChanged.AddUObject(this, &UCustomGrassWorldSubsystem::OnDataAssetValuesChanged);

	CreateShadowMapTextureAtlas();

	if (GrassDataAsset)
	{
		RenderSystem = MakeUnique<FCustomGrassRenderSystem>(*GrassDataAsset,
			CustomGrass::FTextureRenderTargetsGT(ShadowMapTextureAtlas));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Grass data asset not found! System will not start."));
	}
}

void UCustomGrassWorldSubsystem::CreateShadowMapTextureAtlas()
{
	ShadowMapTextureAtlas = NewObject<UTextureRenderTarget2D>(this);
	ShadowMapTextureAtlas->InitAutoFormat(
		CustomGrass::ShadowMapAtlasResolution.X, CustomGrass::ShadowMapAtlasResolution.Y);
	ShadowMapTextureAtlas->RenderTargetFormat = RTF_RG16f;
	ShadowMapTextureAtlas->bSupportsUAV		  = true;
	ShadowMapTextureAtlas->bAutoGenerateMips  = false;
	ShadowMapTextureAtlas->Filter			  = TF_Bilinear;
	
	ShadowMapTextureAtlas->UpdateResource();
}

void UCustomGrassWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();

	CustomGrass::CVarGrassEnabledChanged.RemoveAll(this);

	CustomGrass::DataAssetLoaded.RemoveAll(this);
	CustomGrass::DataAssetValuesChanged.RemoveAll(this);

	DismantleRenderSystem();

	RegisteredLandscapeTiles.Empty();
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
			RegisteredLandscapeTiles.Append(Landscape->LandscapeComponents);
		}
	}

	DebugVisualizer.Initialize(InWorld);
	DebugVisualizer.RegisterLandscapeTiles(RegisteredLandscapeTiles);
	
	MarkDirty(EDirtyFlags::RunningState);
}

bool UCustomGrassWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Cast<UWorld>(Outer)->IsGameWorld();
}

void UCustomGrassWorldSubsystem::SpawnComponents()
{
	if (!GrassDataAsset) return;
	
	for (int32 i = 0; i < RegisteredLandscapeTiles.Num(); i++)
	{
		if (ULandscapeComponent* LandscapeTile = RegisteredLandscapeTiles[i])
		{
			auto* GrassTileComponent = NewObject<UCustomGrassPrimitiveComponent>(
				LandscapeTile->GetOwner(),
				UCustomGrassPrimitiveComponent::StaticClass(),
				*FString::Printf(TEXT("CustomGrassTile[%d]"), i)
			);
		
			const auto Material = FCustomGrassMaterial(GrassDataAsset->GrassMaterial, 
				GrassDataAsset->GrassMaterial_NoTwoSided);
		
			GrassTileComponent->Initialize(
				UCustomGrassPrimitiveComponent::FInitConfig(Material, *LandscapeTile, i),
				*this
			);
				
			GrassTiles.Add(GrassTileComponent);			
		}
	}
}

void UCustomGrassWorldSubsystem::DespawnComponents()
{
	for (UCustomGrassPrimitiveComponent* GrassTile : GrassTiles)
	{
		if (GrassTile)
		{
			GrassTile->DestroyComponent();
		}
	}
	
	GrassTiles.Empty();
}

void UCustomGrassWorldSubsystem::MarkDirty(EDirtyFlags Flags)
{
	DirtyFlags |= Flags;
}

void UCustomGrassWorldSubsystem::OnCVarGrassEnabledChanged()
{
	MarkDirty(EDirtyFlags::RunningState);
}

void UCustomGrassWorldSubsystem::OnDataAssetLoaded()
{
	MarkDirty(EDirtyFlags::RunningState);
}

void UCustomGrassWorldSubsystem::OnDataAssetValuesChanged()
{
	MarkDirty(EDirtyFlags::Rendering);
}

void UCustomGrassWorldSubsystem::UpdateRunningState()
{
	const bool bIsDataAssetLoaded  = GrassDataAsset != nullptr;
	const bool bIsCVarGrassEnabled = (CustomGrass::CVarGrassEnabled.GetValueOnGameThread() == 1);
	
	if (const bool bExpectedRunningState = bIsDataAssetLoaded && bIsCVarGrassEnabled;
		bExpectedRunningState != bIsRunning)
	{
		bIsRunning = bExpectedRunningState;

		if (bIsRunning)
		{
			RenderSystem = MakeUnique<FCustomGrassRenderSystem>(*GrassDataAsset,
				CustomGrass::FTextureRenderTargetsGT(ShadowMapTextureAtlas));
		}
		else
		{
			bPendingRenderSystemDestroy = true;
		}

		MarkDirty(EDirtyFlags::Components);
	}
}

void UCustomGrassWorldSubsystem::UpdateRenderState() const
{
	if (!GrassDataAsset) return;
	
	if (RenderSystem)
	{
		RenderSystem->RebuildRenderStateFromGameThread(*GrassDataAsset);
	}

	for (UCustomGrassPrimitiveComponent* GrassTile : GrassTiles)
	{
		if (GrassTile)
		{
			GrassTile->UpdateRenderSettings(*GrassDataAsset);
		}
	}
}

void UCustomGrassWorldSubsystem::UpdateComponents()
{
	if (const bool bAreComponentsSpawned = !GrassTiles.IsEmpty();
		bIsRunning && !bAreComponentsSpawned)
	{
		SpawnComponents();
	}
	else if (!bIsRunning)
	{
		DespawnComponents();
	}
}

void UCustomGrassWorldSubsystem::Tick(float DeltaTime)
{
	const EDirtyFlags Flags = DirtyFlags;
	DirtyFlags = EDirtyFlags::None;

	if (EnumHasAnyFlags(Flags, EDirtyFlags::RunningState))
	{
		UpdateRunningState();
	}

	if (EnumHasAnyFlags(Flags, EDirtyFlags::Rendering))
	{
		UpdateRenderState();
	}

	if (EnumHasAnyFlags(Flags, EDirtyFlags::Components))
	{
		UpdateComponents();
	}

	UpdateDebugVisualization(DeltaTime);

	if (bPendingRenderSystemDestroy)
	{
		DismantleRenderSystem();
	}
}

void UCustomGrassWorldSubsystem::UpdateDebugVisualization(float DeltaTime)
{
	const auto DebugStateSnapshot = RenderSystem->GetDebugStateSnapshot_GameThread();
	DebugVisualizer.UpdateRTDebugState(DebugStateSnapshot);
	
	DebugVisualizer.Tick(DeltaTime);
}

void UCustomGrassWorldSubsystem::DismantleRenderSystem()
{
	// Synchronous shutdown: make sure that all rendering commands referencing the render system
	// (through lambdas) finish before dismantling it.
	FlushRenderingCommands();
	
	RenderSystem = nullptr;
	bPendingRenderSystemDestroy = false;
}
