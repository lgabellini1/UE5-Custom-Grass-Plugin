#include "CustomGrassDebugVisualizer.h"
#include "Console/CustomGrassConsoleVars.h"
#include "Landscape.h"

void FCustomGrassDebugVisualizer::Initialize(const UWorld& InWorld)
{
	World = &InWorld;
}

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

void FCustomGrassDebugVisualizer::RegisterLandscapeTiles(const FLandscapeTileArray& TileArray)
{
	RegisteredLandscapeTiles = &TileArray;
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
				static_cast<uint64>(TileDebugInfo.TileComponentId.PrimIDValue),
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
			return FString::Printf(TEXT("GrassTile[%u]_LOD%d"),
				TileDebugInfo.TileComponentId.PrimIDValue, static_cast<int32>(TileDebugInfo.LOD));
		});
	}
}

void FCustomGrassDebugVisualizer::ScreenPrintRenderedTiles() const
{
	if (CustomGrass::CVarScreenPrintRenderedTiles.GetValueOnGameThread())
	{
		ScreenPrintBase([](const CustomGrass::FTileDebugInfoRT TileDebugInfo)
		{
			return FString::Printf(TEXT("GrassTile[%u]"), TileDebugInfo.TileComponentId.PrimIDValue);
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
