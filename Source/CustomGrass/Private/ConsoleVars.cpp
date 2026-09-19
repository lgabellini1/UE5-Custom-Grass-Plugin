#include "ConsoleVars.h"

namespace CustomGrass
{
	TAutoConsoleVariable<int32> CVarGrassEnabled(
	TEXT("r.CustomGrass.Enable"),
	1,
	TEXT("Enable custom grass spawning"),
	FConsoleVariableDelegate::CreateLambda(
		[](IConsoleVariable*)
		{
			CustomGrass::CVarGrassEnabledChanged.Broadcast();
		}),
	ECVF_RenderThreadSafe
);

	TAutoConsoleVariable<int32> CVarFrozenViewFrustum(
		TEXT("r.CustomGrass.FreezeViewFrustum"),
		0,
		TEXT("Freeze the view frustum for culling visualization"),
		ECVF_RenderThreadSafe
	);

	TAutoConsoleVariable<int32> CVarDrawTileBounds(
		TEXT("r.CustomGrass.DrawTileBounds"),
		0,
		TEXT("Draw bounds of landscape tiles for visualization"),
		ECVF_RenderThreadSafe
	);

	TAutoConsoleVariable<int32> CVarScreenPrintRenderedTiles(
		TEXT("r.CustomGrass.ScreenPrintRenderedTiles"),
		0,
		TEXT("Print on screen the indices of tiles rendered this frame"),
		ECVF_RenderThreadSafe
	);

	TAutoConsoleVariable<int32> CVarScreenPrintLODs(
		TEXT("r.CustomGrass.ScreenPrintLODs"),
		0,
		TEXT("Print on screen the LODs of tiles rendered this frame"),
		ECVF_RenderThreadSafe
	);
}
