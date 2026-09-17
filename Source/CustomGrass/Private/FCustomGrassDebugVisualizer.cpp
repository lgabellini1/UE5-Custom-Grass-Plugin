#include "FCustomGrassDebugVisualizer.h"
#include "ConsoleVars.h"
#include "Landscape.h"

FCustomGrassDebugVisualizer::FCustomGrassDebugVisualizer(
	const UWorld* World,
	const FLandscapeTileArray& TileArray)
: World(World), RegisteredLandscapeTiles(&TileArray)
{}

void FCustomGrassDebugVisualizer::Tick(float DeltaTime) const
{
	UpdateTileBoundingBoxDrawing();

	ScreenPrintLODs();
	ScreenPrintRenderedTiles();
}

void FCustomGrassDebugVisualizer::UpdateRTDebugState(const CustomGrass::FRTDebugState& NewDebugState)
{
	RTDebugState = NewDebugState;
}

void FCustomGrassDebugVisualizer::UpdateTileBoundingBoxDrawing() const
{
	check(!World.IsStale());

	if (!RegisteredLandscapeTiles)
		return;
	
	if (CustomGrass::CVarDrawTileBounds.GetValueOnGameThread())
	{
		for (int i = 0; i < RegisteredLandscapeTiles->Num(); i++)
		{
			const FBox Bounds = (*RegisteredLandscapeTiles)[i]->Bounds.GetBox();
			const FBox ExpandedBounds = Bounds.ExpandBy(FVector::UnitZ() * 100.f);
		
			DrawDebugSolidBox(World.Get(), ExpandedBounds, GetTileBoundingBoxDebugColor(i),
				FTransform::Identity, true, -1, 1);	
		}
	}
	else
	{
		FlushPersistentDebugLines(World.Get());
	}
}

template <typename Func>
void FCustomGrassDebugVisualizer::ScreenPrintBase(Func&& PrintFunction) const
{
	if (GEngine)
	{
		for (const CustomGrass::FTileDebugInfoRT TileDebugInfo : RTDebugState)
		{
			GEngine->AddOnScreenDebugMessage(
				TileDebugInfo.TileIndex,
				1.0f,
				FColor::Yellow,
				*PrintFunction(TileDebugInfo));
		}
	}
}

void FCustomGrassDebugVisualizer::ScreenPrintLODs() const
{
	if (CustomGrass::CVarScreenPrintLODs.GetValueOnGameThread())
	{
		ScreenPrintBase([](const CustomGrass::FTileDebugInfoRT TileDebugInfo)
		{
			return FString::Printf(TEXT("GrassTile_[%d]_LOD"),
				static_cast<int32>(TileDebugInfo.LOD));
		});
	}
}

void FCustomGrassDebugVisualizer::ScreenPrintRenderedTiles() const
{
	if (CustomGrass::CVarScreenPrintRenderedTiles.GetValueOnGameThread())
	{
		ScreenPrintBase([](const CustomGrass::FTileDebugInfoRT TileDebugInfo)
		{
			return FString::Printf(TEXT("GrassTile_[%d]"), TileDebugInfo.TileIndex);
		});		
	}
}

FColor FCustomGrassDebugVisualizer::GetTileBoundingBoxDebugColor(int32 TileIndex)
{
	constexpr uint8 HueStep = 37;
	const uint8 Hue = (TileIndex * HueStep) % 255;

	FColor Color = FLinearColor::MakeFromHSV8(Hue, 200, 255).ToFColor(true);
	Color.A = 64;

	return Color;
}
