#include "CustomGrassWorldSubsystem.h"
#include "CustomGrassConsoleVars.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassPrimitiveComponent.h"
#include "Rendering/CustomGrassRenderSystem.h"
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

void UCustomGrassWorldSubsystem::InitShadowMapTextureAtlas()
{
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
	UWorld& World = GetWorldRef();
	
	for (int32 i = 0; i < RegisteredLandscapeTiles.Num(); i++)
	{
		UCustomGrassPrimitiveComponent* Component = NewObject<UCustomGrassPrimitiveComponent>(
			RegisteredLandscapeTiles[i]->GetOwner(),
			UCustomGrassPrimitiveComponent::StaticClass(),
			*FString::Printf(TEXT("CustomGrassTile_[%d]"), i)
		);
		
		UCustomGrassShadowProxyComponent* ShadowProxy = NewObject<UCustomGrassShadowProxyComponent>(
			RegisteredLandscapeTiles[i]->GetOwner(),
			UCustomGrassShadowProxyComponent::StaticClass(),
			*FString::Printf(TEXT("CustomGrassShadowProxy_[%d]"), i)
		);

		ShadowProxy->PlaneResolution = GrassDataAsset->ProxyResolution;

		ShadowProxy->RegisterComponentWithWorld(&World);
		ShadowProxy->AttachToComponent(RegisteredLandscapeTiles[i], FAttachmentTransformRules::KeepRelativeTransform);
		ShadowProxy->BuildMesh(RegisteredLandscapeTiles[i], GrassDataAsset);
		ShadowProxy->SetCastShadow(GrassDataAsset->bShadowsEnabled);
		check(ShadowProxyMID);
		ShadowProxy->SetMaterial(0, ShadowProxyMID);

		Component->ShadowProxy = ShadowProxy;
		
		Component->Material			  = GrassDataAsset->GrassMaterial;
		Component->Material_NoTwoSide = GrassDataAsset->GrassMaterial_NoTwoSided;
		Component->LandscapeTile = RegisteredLandscapeTiles[i];

		Component->SetIndex(i);

		Component->RegisterComponentWithWorld(&World);
		Component->AttachToComponent(RegisteredLandscapeTiles[i], FAttachmentTransformRules::KeepRelativeTransform);
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
	for (const TObjectPtr<UCustomGrassPrimitiveComponent> Tile : GrassTiles)
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

	for (int32 i = 0; i < RegisteredLandscapeTiles.Num(); i++)
	{
		auto ShadowProxy = ShadowProxyComponents[i];
		ShadowProxy->PlaneResolution = GrassDataAsset->ProxyResolution;
		ShadowProxy->SetVisibility(GrassDataAsset->bDebugShowProxyMesh);
		ShadowProxy->SetCastShadow(GrassDataAsset->bShadowsEnabled);
		ShadowProxy->BuildMesh(RegisteredLandscapeTiles[i], GrassDataAsset);
		
		ShadowProxy->MarkRenderStateDirty();

		auto GrassTile = GrassTileComponents[i];
		GrassTile->SetCastShadow(GrassDataAsset->bShadowsEnabled);

		GrassTile->MarkRenderStateDirty();
	}
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

FVector2D GetLandscapeExtentInWorldUnits(const ALandscape* Landscape)
{
	FIntRect LandscapeExtent = Landscape->GetLandscapeInfo()->GetCompleteLandscapeExtent();
	check(LandscapeExtent.Width() == LandscapeExtent.Height());
		
	return FVector2D(
		LandscapeExtent.Width() * Landscape->GetActorScale3D().X,
		LandscapeExtent.Height() * Landscape->GetActorScale3D().Y);
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
