#pragma once

#include "CoreMinimal.h"

extern TAutoConsoleVariable<int32> CVarGrassEnabled;

extern TAutoConsoleVariable<int32> CVarFrozenViewFrustum;

DECLARE_MULTICAST_DELEGATE(FOnCVarGrassEnabledChanged);
extern FOnCVarGrassEnabledChanged CVarGrassEnabledChanged;
