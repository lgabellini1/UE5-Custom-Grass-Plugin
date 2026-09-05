#include "CustomGrassWorldSubsystem.h"
#include "CustomGrassConsoleVars.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassPrimitiveComponent.h"
#include "Rendering/CustomGrassRenderSystem.h"
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

	const auto* Settings = GetDefault<UCustomGrassSettings>();
	GrassDataAsset = Settings->GrassDataAsset.LoadSynchronous();

	if (!GrassDataAsset)
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
	ShadowWPOTextureAtlas = NewObject<UTextureRenderTarget2D>(this);
	ShadowWPOTextureAtlas->InitAutoFormat(GShadowWPOAtlasRes.X, GShadowWPOAtlasRes.Y);
	ShadowWPOTextureAtlas->RenderTargetFormat = RTF_RG16f;
	ShadowWPOTextureAtlas->bSupportsUAV		  = true;
	ShadowWPOTextureAtlas->bAutoGenerateMips  = false;
	ShadowWPOTextureAtlas->Filter			  = TF_Bilinear;
	
	ShadowWPOTextureAtlas->UpdateResource();
	RenderSystem->UpdateShadowMapResourceFromGameThread(ShadowWPOTextureAtlas);
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
	UWorld& World = GetWorldRef();
	
	for (int32 i = 0; i < RegisteredLandscapeTiles.Num(); i++)
	{
		ULandscapeComponent* LandscapeTile = RegisteredLandscapeTiles[i];
		
		auto* GrassTileComponent = NewObject<UCustomGrassPrimitiveComponent>(
			LandscapeTile->GetOwner(),
			UCustomGrassPrimitiveComponent::StaticClass(),
			*FString::Printf(TEXT("CustomGrassTile_[%d]"), i)
		);
		
		GrassTileComponent->Initialize(
			UCustomGrassPrimitiveComponent::FInitConfig(
				GrassDataAsset->GrassMaterial,
				GrassDataAsset->GrassMaterial_NoTwoSided,
				i
			),
			LandscapeTile,
			*GrassDataAsset
		);

		GrassTileComponent->RegisterComponentWithWorld(&World);
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
	const bool bIsCVarGrassEnabled = CVarGrassEnabled.GetValueOnGameThread();
	
	const bool bExpectedRunningState = bIsDataAssetLoaded
		&& bIsCVarGrassEnabled;

	if (bExpectedRunningState != bRunningState)
	{
		bRunningState = bExpectedRunningState;
		RenderSystem->NotifyRunningStateFromGameThread(bRunningState);
		MarkDirty(EDirtyFlags::Components);
	}
}

void UCustomGrassWorldSubsystem::UpdateRenderState() const
{
	RenderSystem->RebuildRenderState(*GrassDataAsset);

	for (const TObjectPtr<UCustomGrassPrimitiveComponent> GrassTile : GrassTiles)
	{
		GrassTile->UpdateRenderSettings(*GrassDataAsset);
	}
}

void UCustomGrassWorldSubsystem::UpdateComponents()
{
	const bool bAreComponentsSpawned = !GrassTiles.IsEmpty();

	if (bRunningState && !bAreComponentsSpawned)
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

UCustomGrassWorldSubsystem::EDirtyFlags UCustomGrassWorldSubsystem::operator|(
	EDirtyFlags F1,
	EDirtyFlags F2) const
{
	return static_cast<EDirtyFlags>(
		static_cast<uint8>(F1) | static_cast<uint8>(F2)
	);
}

UCustomGrassWorldSubsystem::EDirtyFlags& UCustomGrassWorldSubsystem::operator|=(
	EDirtyFlags& F1,
	EDirtyFlags F2) const
{
	return F1 = F1 | F2;
}

UCustomGrassWorldSubsystem::EDirtyFlags UCustomGrassWorldSubsystem::operator&(
	EDirtyFlags F1,
	EDirtyFlags F2) const
{
	return static_cast<EDirtyFlags>(
		static_cast<uint8>(F1) & static_cast<uint8>(F2)
	);
}
