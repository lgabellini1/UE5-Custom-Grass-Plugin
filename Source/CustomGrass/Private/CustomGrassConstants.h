#pragma once

namespace CustomGrass
{
	/**
	 *	A ceil on the number of rendered tiles. This lets us avoid unexpected memory
	 *	blow-ups while avoiding costly dynamic resizing of the buffers on each frame.
	 */
	inline constexpr int32 MaxRenderedTiles = 4;

	inline constexpr float MaxGrassBladeHeight = 50.f;
	inline constexpr float MaxGrassBladeWidth  = 2.f;
	inline constexpr float MaxGrassBladeTilt   = 10.f;
	inline constexpr float MaxGrassBladeBend   = 10.f;

	inline const FIntPoint ShadowMapTextureSlotResolution = FIntPoint(512, 512);

	inline constexpr int32 ShadowMapAtlasGridSize   = MaxRenderedTiles / 2;
	inline const FIntPoint ShadowMapAtlasResolution = ShadowMapTextureSlotResolution * ShadowMapAtlasGridSize;
}
