#pragma once

#include "CustomGrassConstants.h"
#include "CustomGrassTypes.h"

namespace CustomGrass
{
	struct FTileDebugInfoRT
	{
		FPrimitiveComponentId TileComponentId;
		ECustomGrassLOD LOD;
	};
	
	using FRTDebugState = TStaticArray<FTileDebugInfoRT, MaxRenderedTiles>;
}
