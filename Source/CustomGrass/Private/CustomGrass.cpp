#include "CustomGrass.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FCustomGrassModule"

void FCustomGrassModule::StartupModule()
{
	const FString BasePath = IPluginManager::Get().FindPlugin(TEXT("CustomGrass"))->GetBaseDir();
	const FString PluginShaderPath = FPaths::Combine(BasePath, TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/CustomShaders"), PluginShaderPath);
}

void FCustomGrassModule::ShutdownModule()
{}

FOnGrassDataAssetLoad OnGrassDataAssetLoadDelegate;

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FCustomGrassModule, CustomGrass)
