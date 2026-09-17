#pragma once

#include "Types.h"

class ULandscapeComponent;

class FCustomGrassDebugVisualizer
{
	using FLandscapeTileArray = TArray<TObjectPtr<const ULandscapeComponent>>;
	
public:
	FCustomGrassDebugVisualizer(const UWorld* World, 
		const FLandscapeTileArray& TileArray);

	void Tick(float DeltaTime) const;
	
	void UpdateRTDebugState(const CustomGrass::FRTDebugState& NewDebugState);

protected:
	TWeakObjectPtr<const UWorld> World;

	const FLandscapeTileArray* RegisteredLandscapeTiles;

	CustomGrass::FRTDebugState RTDebugState;

	void UpdateTileBoundingBoxDrawing() const;

	template <typename Func>
	void ScreenPrintBase(Func&& PrintFunction) const;
	void ScreenPrintLODs() const;
	void ScreenPrintRenderedTiles() const;
	
	static FColor GetTileBoundingBoxDebugColor(int32 TileIndex);
};
