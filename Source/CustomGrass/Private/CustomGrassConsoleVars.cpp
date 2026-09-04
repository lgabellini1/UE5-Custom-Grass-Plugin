#include "CustomGrassConsoleVars.h"

FOnCVarGrassEnabledChanged CVarGrassEnabledChanged;

TAutoConsoleVariable<int32> CVarGrassEnabled(
	TEXT("r.CustomGrass.Enable"),
	1,
	TEXT("Enable custom grass spawning"),
	FConsoleVariableDelegate::CreateLambda(
		[](IConsoleVariable*)
		{
			CVarGrassEnabledChanged.Broadcast();
		}),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarFrozenViewFrustum(
	TEXT("r.CustomGrass.FreezeViewFrustum"),
	0,
	TEXT("Freeze the view frustum for culling visualization"),
	ECVF_RenderThreadSafe
);
