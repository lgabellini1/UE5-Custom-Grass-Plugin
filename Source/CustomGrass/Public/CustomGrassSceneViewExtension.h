#pragma once
#include "SceneViewExtension.h"

class FCustomGrassRenderSystem;

class FCustomGrassSceneViewExtension final : public FSceneViewExtensionBase
{
public:
	FCustomGrassSceneViewExtension(const FAutoRegister& AutoRegister, FCustomGrassRenderSystem* RenderSystem);

	virtual void PreInitViews_RenderThread(FRDGBuilder& GraphBuilder) override;

	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

private:
	FCustomGrassRenderSystem* RenderSystem;
};
