// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FCustomGrassModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

extern TAutoConsoleVariable<int32> CVarCustomGrassEnabled;

extern TAutoConsoleVariable<int32> CVarFrozenViewFrustum;

/* Delegates */

DECLARE_MULTICAST_DELEGATE(FOnGrassDataAssetLoad);
extern FOnGrassDataAssetLoad OnGrassDataAssetLoadDelegate;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnCVarGrassEnableChange, bool);
extern FOnCVarGrassEnableChange OnCVarGrassEnableChangeDelegate;
