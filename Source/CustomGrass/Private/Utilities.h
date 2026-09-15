#pragma once

namespace CustomGrass { struct FProxyLandscapeData; }
class ALandscape;
class ULandscapeComponent;

namespace CustomGrass
{
	FVector2D GetLandscapeExtentInWorldUnits(const ALandscape& Landscape);

	FVector GetLandscapeTileOrigin(const ULandscapeComponent& LandscapeTile);
	FVector GetLandscapeTileOrigin(const FProxyLandscapeData& LandscapeData);

	FVector GetLandscapeTileExtent(const ULandscapeComponent& LandscapeTile);
	FVector GetLandscapeTileExtent(const FProxyLandscapeData& LandscapeData);

	FVector GetNearestTileBoundsPointFromCamera(const FSceneView* View,
		const FProxyLandscapeData& LandscapeData);

	bool IsTileOutsideViewFrustum(const FSceneView* View,
		const FProxyLandscapeData& LandscapeData);
}
