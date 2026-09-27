#pragma once

#include "CustomGrassDebugTypes.h"

class ULandscapeComponent;

class FCustomGrassDebugVisualizer
{
	using FLandscapeTileArray = TArray<TObjectPtr<ULandscapeComponent>>;
	
public:
	void Initialize(const UWorld& InWorld);
	
	void Tick(float DeltaTime) const;
	
	void UpdateRTDebugState(const CustomGrass::FRTDebugState& NewDebugState);

	void RegisterLandscapeTiles(const FLandscapeTileArray& TileArray);

protected:
	TWeakObjectPtr<const UWorld> World;

	const FLandscapeTileArray* RegisteredLandscapeTiles = nullptr;

	CustomGrass::FRTDebugState RTDebugState;

	void UpdateTileBoundingBoxDrawing() const;

	template <typename Func>
	void ScreenPrintBase(Func&& PrintFunction) const;
	void ScreenPrintLODs() const;
	void ScreenPrintRenderedTiles() const;
	
	static FColor GetTileBoundingBoxDebugColor(int32 TileIndex);
};
