#pragma once

#include "CoreMinimal.h"
#include "CustomGrassWorldSubsystem.generated.h"

class ALandscape;
class ULandscapeComponent;
class UCustomGrassDataAsset;
class UCustomGrassPrimitiveComponent;
class FCustomGrassRenderSystem;

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
	TUniquePtr<FCustomGrassRenderSystem> RenderSystem;
	
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
	
	
	UPROPERTY()
	TArray<TObjectPtr<ULandscapeComponent>> RegisteredLandscapeTiles;
	
	UPROPERTY()
	TArray<TObjectPtr<UCustomGrassPrimitiveComponent>> GrassTiles;

	UPROPERTY()
	TObjectPtr<UCustomGrassDataAsset> GrassDataAsset;
	

	EDirtyFlags DirtyFlags = EDirtyFlags::None;
	void MarkDirty(EDirtyFlags Flags);
	
	void OnCVarGrassEnabledChanged();
	void OnDataAssetLoaded();
	void OnDataAssetValuesChanged();
	
	bool bRunningState = false;
	void UpdateRunningState();
	
	void UpdateRenderState() const;
	
	void UpdateComponents();
	void SpawnComponents();
	void DespawnComponents();

	
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> ShadowWPOTextureAtlas;
	
	void CreateShadowMapTextureAtlas();
};
