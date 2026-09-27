#include "CustomGrassUtils.h"
#include "Rendering/CustomGrassRenderTypes.h"
#include "Landscape.h"

FVector2D CustomGrass::GetLandscapeExtentInWorldUnits(const ALandscape& Landscape)
{
	const FIntRect LandscapeExtent = Landscape.GetLandscapeInfo()->GetCompleteLandscapeExtent();
		
	return FVector2D(LandscapeExtent.Width() * Landscape.GetActorScale3D().X,
		LandscapeExtent.Height() * Landscape.GetActorScale3D().Y);
}

FVector CustomGrass::GetLandscapeTileOrigin(const FProxyLandscapeData& LandscapeData)
{
	const float QuadSize = LandscapeData.LocalToWorldMatrix.GetScaleVector().X;
	
	const FVector2D TileCenterInQuads = LandscapeData.SectionBase + LandscapeData.ComponentSizeQuads * 0.5f;

	return LandscapeData.LocalToWorldMatrix.GetOrigin() + FVector(TileCenterInQuads * QuadSize, 0.f);
}

FVector CustomGrass::GetLandscapeTileExtent(const FProxyLandscapeData& LandscapeData)
{
	return LandscapeData.BoundingBox;
}

FVector CustomGrass::GetNearestTileBoundsPointFromCamera(const FSceneView* View,
	const FProxyLandscapeData& LandscapeData)
{
	const FVector Camera = View->ViewMatrices.GetViewOrigin();

	const FVector TileOrigin = GetLandscapeTileOrigin(LandscapeData);
	const FVector TileExtent = GetLandscapeTileExtent(LandscapeData);
	
	const FVector TileMin = TileOrigin - TileExtent;
	const FVector TileMax = TileOrigin + TileExtent;

	return Camera.BoundToBox(TileMin, TileMax);
}

bool CustomGrass::IsTileOutsideViewFrustum(const FSceneView* View, const FProxyLandscapeData& LandscapeData)
{
	return !View->ViewFrustum.IntersectBox(GetLandscapeTileOrigin(LandscapeData),
		GetLandscapeTileExtent(LandscapeData)
	);
}
