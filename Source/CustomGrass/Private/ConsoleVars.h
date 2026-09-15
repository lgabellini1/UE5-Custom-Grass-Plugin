#pragma once

#include "CoreMinimal.h"

namespace CustomGrass
{
	extern TAutoConsoleVariable<int32> CVarGrassEnabled;

	extern TAutoConsoleVariable<int32> CVarFrozenViewFrustum;

	extern TAutoConsoleVariable<int32> CVarDrawTileBounds;

	extern TAutoConsoleVariable<int32> CVarScreenPrintRenderedTiles;
	
	extern TAutoConsoleVariable<int32> CVarScreenPrintLODs;

	DECLARE_MULTICAST_DELEGATE(FOnCVarGrassEnabledChanged);
	inline FOnCVarGrassEnabledChanged CVarGrassEnabledChanged;
}
