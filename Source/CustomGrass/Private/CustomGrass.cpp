// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomGrass.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FCustomGrassModule"

void FCustomGrassModule::StartupModule()
{
	FString BasePath = IPluginManager::Get().FindPlugin(TEXT("CustomGrass"))->GetBaseDir();
	const FString PluginShaderPath = FPaths::Combine(BasePath, TEXT("Shaders"));
	// Create a symbolic path for the engine to look for custom shaders in.
	AddShaderSourceDirectoryMapping(TEXT("/CustomShaders"), PluginShaderPath);
}

void FCustomGrassModule::ShutdownModule()
{}


TAutoConsoleVariable<int32> CVarCustomGrassEnabled(
	TEXT("r.CustomGrass.Enable"),
	1,
	TEXT("Enable custom grass spawning"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarFrozenViewFrustum(
	TEXT("r.CustomGrassFreezeView.Enable"),
	0,
	TEXT("Freeze the view frustum for culling visualization"),
	ECVF_RenderThreadSafe
);

FOnGrassDataAssetLoad OnGrassDataAssetLoadDelegate;
FOnCVarGrassEnableChange OnCVarGrassEnableChangeDelegate;

static void BroadcastCVarChange_GameThread()
{
	bool bNewValue = CVarCustomGrassEnabled.GetValueOnGameThread() == 1;

	OnCVarGrassEnableChangeDelegate.Broadcast(bNewValue);
}

static FAutoConsoleVariableSink CVarCustomGrassEnabledSink(
	FConsoleCommandDelegate::CreateStatic(&BroadcastCVarChange_GameThread)
);

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FCustomGrassModule, CustomGrass)