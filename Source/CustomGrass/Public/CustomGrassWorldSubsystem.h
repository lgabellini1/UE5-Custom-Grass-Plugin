#pragma once

#include "CoreMinimal.h"
#include "Shared.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassWorldSubsystem.generated.h"

class ALandscape;
struct FVolatileBuffers;
struct FRenderingResourceHandles;
struct FProxyLandscapeData;
class UCustomGrassDataAsset;
class ULandscapeComponent;
class UCustomGrassPrimitiveComponent;
class UCustomGrassShadowProxyComponent;

#define DEBUG_DRAW_TILE_BOUNDS false
#define DEBUG_LOG_TILE_LOD false
#define DEBUG_RENDERED_TILES true

UCLASS()
class UCustomGrassWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Editor-only */
	virtual void Tick(float DeltaTime) override;

	virtual bool IsTickable() const override { return WITH_EDITOR; }

	virtual TStatId GetStatId() const override { return TStatId(); }
	
	FCustomGrassRenderSystem* GetRenderSystem() const { return RenderSystem.Get(); }

protected:
	TUniquePtr<FCustomGrassRenderSystem> RenderSystem;

/* Landscape */
	
	UPROPERTY()
	TObjectPtr<ALandscape> LandscapeActor;
	
	UPROPERTY()
	TArray<TObjectPtr<ULandscapeComponent>> LandscapeTiles;

/* System components */
	
	UPROPERTY()
	TArray<TObjectPtr<UCustomGrassPrimitiveComponent>> GrassTileComponents;

	UPROPERTY()
	TArray<TObjectPtr<UCustomGrassShadowProxyComponent>> ShadowProxyComponents;

/* Textures & materials */
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ShadowProxyMID;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> ShadowWPOTextureAtlas;
	
	
	void SpawnComponents();
	void DespawnComponents();

	/** Allows changing grass aspect dynamically. */
	UPROPERTY()
	const UCustomGrassDataAsset* GrassDataAsset;

	// Delegates: respond to state change -> call RecomputeRunningState()
	
	void OnCVarChanged();
	void OnDataAssetChanged();
	
	void SetupShadowProxyMaterial();

	/** Handles state change that causes system activation / deactivation. */
	void RecomputeRunningState();

	bool bIsActive = false;
};

FVector2D GetLandscapeExtentInWorldUnits(const ALandscape* Landscape);
