#include "CustomGrassWorldSubsystem.h"
#include "ConsoleVars.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassPrimitiveComponent.h"
#include "Rendering/CustomGrassRenderSystem.h"
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

	const auto* Settings = GetDefault<UCustomGrassSettings>();
	GrassDataAsset = Settings->GrassDataAsset.LoadSynchronous();

	CustomGrass::CVarGrassEnabledChanged.AddUObject(this, &UCustomGrassWorldSubsystem::OnCVarGrassEnabledChanged);
	
	CustomGrass::DataAssetLoaded.AddUObject(this, &UCustomGrassWorldSubsystem::OnDataAssetLoaded);
	CustomGrass::DataAssetValuesChanged.AddUObject(this, &UCustomGrassWorldSubsystem::OnDataAssetValuesChanged);

	if (GrassDataAsset)
	{
		RenderSystem = MakeUnique<FCustomGrassRenderSystem>(*GrassDataAsset);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Grass data asset not found! System will not start."));
	}
	
	RenderSystem = MakeUnique<FCustomGrassRenderSystem>();

	CVarGrassEnabledChanged.AddUObject(this, &UCustomGrassWorldSubsystem::OnCVarGrassEnabledChanged);
	GrassDataAssetLoaded.AddUObject(this, &UCustomGrassWorldSubsystem::OnDataAssetLoaded);
	GrassDataAssetValuesChanged.AddUObject(this, &UCustomGrassWorldSubsystem::OnDataAssetValuesChanged);
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
	
	if (RenderSystem)
	{
		RenderSystem->UpdateShadowMapResourceFromGameThread(*ShadowMapTextureAtlas);
	}
}

void UCustomGrassWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();

	CustomGrass::CVarGrassEnabledChanged.RemoveAll(this);

	CustomGrass::DataAssetLoaded.RemoveAll(this);
	CustomGrass::DataAssetValuesChanged.RemoveAll(this);

	// Synchronous shutdown: make sure that all rendering commands referencing the render system
	// (through lambdas) finish before dismantling it.
	FlushRenderingCommands();
	RenderSystem = nullptr;

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
	
	MarkDirty(EDirtyFlags::RunningState);
}

bool UCustomGrassWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return GetWorldRef().IsGameWorld();
}

void UCustomGrassWorldSubsystem::SpawnComponents()
{
	if (!GrassDataAsset) return;
	
	for (int32 i = 0; i < RegisteredLandscapeTiles.Num(); i++)
	{
		ULandscapeComponent* LandscapeTile = RegisteredLandscapeTiles[i];
		check(LandscapeTile);
		
		auto* GrassTileComponent = NewObject<UCustomGrassPrimitiveComponent>(
			LandscapeTile->GetOwner(),
			UCustomGrassPrimitiveComponent::StaticClass(),
			*FString::Printf(TEXT("CustomGrassTile_[%d]"), i)
		);
		
		const auto Material = FCustomGrassMaterial(GrassDataAsset->GrassMaterial, 
			GrassDataAsset->GrassMaterial_NoTwoSided);
		
		GrassTileComponent->Initialize(
			UCustomGrassPrimitiveComponent::FInitConfig(Material, i),
			*LandscapeTile,
			*GrassDataAsset
		);

		GrassTileComponent->RegisterComponentWithWorld(&GetWorldRef());
		GrassTileComponent->AttachToComponent(LandscapeTile, FAttachmentTransformRules::KeepRelativeTransform);
				
		GrassTiles.Add(GrassTileComponent);
		
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
	for (const auto Tile : GrassTiles)
	{
		Tile->DestroyComponent();
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
	const bool bIsCVarGrassEnabled = CustomGrass::CVarGrassEnabled.GetValueOnGameThread();
	
	const bool bExpectedRunningState = bIsDataAssetLoaded
		&& bIsCVarGrassEnabled;

	if (bExpectedRunningState != bRunningState)
	{
		bRunningState = bExpectedRunningState;

		RenderSystem = bRunningState ?
			MakeUnique<FCustomGrassRenderSystem>(*GrassDataAsset) : nullptr;

		MarkDirty(EDirtyFlags::Components);
	}
}

void UCustomGrassWorldSubsystem::UpdateRenderState() const
{
	if (RenderSystem)
	{
		RenderSystem->RebuildRenderState(*GrassDataAsset);
	}

	for (const TObjectPtr<UCustomGrassPrimitiveComponent>& GrassTile : GrassTiles)
	{
		GrassTile->UpdateRenderSettings(*GrassDataAsset);
	}
}

void UCustomGrassWorldSubsystem::UpdateComponents()
{
	if (const bool bAreComponentsSpawned = !GrassTiles.IsEmpty();
		bRunningState && !bAreComponentsSpawned)
	{
		SpawnComponents();
	}
	else if (!bRunningState)
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
}

UCustomGrassWorldSubsystem::EDirtyFlags operator|(
	UCustomGrassWorldSubsystem::EDirtyFlags F1,
	UCustomGrassWorldSubsystem::EDirtyFlags F2)
{
	return static_cast<UCustomGrassWorldSubsystem::EDirtyFlags>(
		static_cast<uint8>(F1) | static_cast<uint8>(F2)
	);
}

UCustomGrassWorldSubsystem::EDirtyFlags& operator|=(
	UCustomGrassWorldSubsystem::EDirtyFlags& F1,
	UCustomGrassWorldSubsystem::EDirtyFlags F2)
{
	return F1 = F1 | F2;
}

UCustomGrassWorldSubsystem::EDirtyFlags operator&(
	UCustomGrassWorldSubsystem::EDirtyFlags F1,
	UCustomGrassWorldSubsystem::EDirtyFlags F2)
{
	return static_cast<UCustomGrassWorldSubsystem::EDirtyFlags>(
		static_cast<uint8>(F1) & static_cast<uint8>(F2)
	);
}
