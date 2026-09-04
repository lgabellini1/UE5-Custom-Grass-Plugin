#pragma once

class FCustomGrassModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	
	virtual void ShutdownModule() override;
};


/* Delegates */

DECLARE_MULTICAST_DELEGATE(FOnGrassDataAssetLoad);
extern FOnGrassDataAssetLoad OnGrassDataAssetLoadDelegate;
