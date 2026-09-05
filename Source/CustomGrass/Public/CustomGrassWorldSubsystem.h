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
class FCustomGrassRenderSystem;
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

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	virtual void Tick(float DeltaTime) override;
	
	virtual TStatId GetStatId() const override { return TStatId(); }
	
	FCustomGrassRenderSystem* GetRenderSystem() const { return RenderSystem.Get(); }

protected:
	enum class EDirtyFlags : uint8
	{
		None		 = 0,
		RunningState = 1 << 0,
		Components	 = 1 << 1,
		Rendering    = 1 << 2
	};

	FORCEINLINE EDirtyFlags  operator|(EDirtyFlags, EDirtyFlags) const;
	FORCEINLINE EDirtyFlags& operator|=(EDirtyFlags&, EDirtyFlags) const;
	FORCEINLINE EDirtyFlags  operator&(EDirtyFlags, EDirtyFlags) const;
	
	TUniquePtr<FCustomGrassRenderSystem> RenderSystem;

/* Landscape */
	
	UPROPERTY()
	TObjectPtr<ALandscape> LandscapeActor;
	
	UPROPERTY()
	TArray<TObjectPtr<ULandscapeComponent>> RegisteredLandscapeTiles;

/* System components */
	
	UPROPERTY()
	TArray<TObjectPtr<UCustomGrassPrimitiveComponent>> GrassTiles;

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

	EDirtyFlags DirtyFlags = EDirtyFlags::None;
	void MarkDirty(EDirtyFlags Flags);
	
	void OnCVarGrassEnabledChanged();
	void OnDataAssetLoaded();
	void OnDataAssetValuesChanged();

	void InitShadowMapTextureAtlas();
	void SetupShadowProxyMaterial();

	void UpdateRunningState();
	void UpdateRenderState() const;
	void UpdateComponents();

	bool bRunningState = false;
};
