#pragma once

#include "CoreMinimal.h"
#include "Debug/CustomGrassDebugVisualizer.h"
#include "Rendering/CustomGrassRenderSystem.h"
#include "CustomGrassWorldSubsystem.generated.h"

class ALandscape;
class ULandscapeComponent;
class UCustomGrassDataAsset;
class UCustomGrassPrimitiveComponent;
class FCustomGrassRenderSystem;

UCLASS()
class UCustomGrassWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	
	virtual TStatId GetStatId() const override { return TStatId(); }

	
	FCustomGrassRenderSystem* GetRenderSystem() const { return RenderSystem.Get(); }
	
	const UCustomGrassDataAsset* GetDataAsset() const { return GrassDataAsset; }

	UTextureRenderTarget2D* GetShadowMapTextureAtlas() const { return ShadowMapTextureAtlas; }
	
private:
	TUniquePtr<FCustomGrassRenderSystem> RenderSystem;
	void DismantleRenderSystem();
	bool bPendingRenderSystemDestroy;
	
	enum class EDirtyFlags : uint8
	{
		None		 = 0,
		RunningState = 1 << 0,
		Components	 = 1 << 1,
		Rendering    = 1 << 2
	};
	
	FRIEND_ENUM_CLASS_FLAGS(EDirtyFlags);
	
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
	
	bool bIsRunning = false;
	void UpdateRunningState();
	
	void UpdateRenderState() const;
	
	void UpdateComponents();
	void SpawnComponents();
	void DespawnComponents();
	
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> ShadowMapTextureAtlas;
	void CreateShadowMapTextureAtlas();

	FCustomGrassDebugVisualizer DebugVisualizer;
	void UpdateDebugVisualization(float DeltaTime);
};
