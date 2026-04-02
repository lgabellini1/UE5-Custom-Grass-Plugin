#include "CustomGrassSceneViewExtension.h"
#include "CustomGrassWorldSubsystem.h"

FCustomGrassSceneViewExtension::FCustomGrassSceneViewExtension(const FAutoRegister& AutoRegister, FCustomGrassRenderSystem* RenderSystem)
	: FSceneViewExtensionBase(AutoRegister), RenderSystem(RenderSystem)
{}

void FCustomGrassSceneViewExtension::PreInitViews_RenderThread(FRDGBuilder& GraphBuilder)
{
	RenderSystem->BeginFrame(GraphBuilder);
}

void FCustomGrassSceneViewExtension::PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	RenderSystem->EndFrame(GraphBuilder);
}
