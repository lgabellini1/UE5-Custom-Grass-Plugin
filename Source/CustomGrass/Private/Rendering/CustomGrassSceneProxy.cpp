#include "CustomGrassSceneProxy.h"
#include "Rendering/CustomGrassRenderSystem.h"
#include "CustomGrassPrimitiveComponent.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "CustomGrassVertexFactory.h"
#include "RenderGraphUtils.h"

FCustomGrassSceneProxy::FCustomGrassSceneProxy(const UCustomGrassPrimitiveComponent& Component,
                                               FCustomGrassRenderSystem* RenderSystem, int32 TileIndex)
	: FPrimitiveSceneProxy(&Component, FName(
		FString(TEXT("CustomGrassTileProxy_[")) + FString::FromInt(TileIndex) + FString(TEXT("]")))),
	TileIndex(TileIndex), RenderSystem(RenderSystem),
	LandscapeData(CustomGrass::FProxyLandscapeData(Component.GetAssociatedLandscapeTile())),
	MaterialConfig(Component.GetMaterial().TwoSided, GetScene().GetShaderPlatform()),
	NoTwoSideMaterialConfig(Component.GetMaterial().NoTwoSided, GetScene().GetShaderPlatform())
{}

void FCustomGrassSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	VertexFactory = MakeUnique<FCustomGrassVertexFactory>(GetScene().GetFeatureLevel());
	VertexFactory->InitResource(RHICmdList);
}

void FCustomGrassSceneProxy::DestroyRenderThreadResources()
{
	VertexFactory->ReleaseResource();
	VertexFactory.Reset();
}

FPrimitiveViewRelevance FCustomGrassSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Relevance;
	Relevance.bDrawRelevance		 = IsShown(View);
	Relevance.bShadowRelevance		 = IsShadowCast(View);
	Relevance.bStaticRelevance		 = false;
	Relevance.bDynamicRelevance		 = true;
	Relevance.bOpaque				 = true;
	Relevance.bRenderInMainPass		 = ShouldRenderInMainPass();
	Relevance.bRenderInDepthPass	 = ShouldRenderInDepthPass();
	Relevance.bRenderCustomDepth	 = ShouldRenderCustomDepth();
	Relevance.bUsesLightingChannels  = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Relevance.bTranslucentSelfShadow = false;
	Relevance.bVelocityRelevance	 = false;
	
	MaterialConfig.MaterialRelevance.SetPrimitiveViewRelevance(Relevance);
	return Relevance;
}

void FCustomGrassSceneProxy::GetDynamicMeshElements(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap,
	FMeshElementCollector& Collector) const
{
	check(IsInAnyRenderingThread());
	
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			
			if (const CustomGrass::FProxyVertexShaderData* VSData = RenderSystem->AddProxyRenderingWork(*this, View))
			{
				CustomGrass::EGrassLOD LOD = VSData->LOD;
				CachedLOD.store(LOD);

				FMeshBatch& Mesh = Collector.AllocateMesh();
				Mesh.MaterialRenderProxy =
					static_cast<int32>(LOD) < 2 ? MaterialConfig.MaterialProxy : NoTwoSideMaterialConfig.MaterialProxy;
				Mesh.VertexFactory = VertexFactory.Get();
				Mesh.Type = PT_TriangleStrip;

				Mesh.bUseForMaterial  = true;
				Mesh.bUseForDepthPass = true;
				Mesh.CastShadow		  = true;

				Mesh.Elements.SetNumZeroed(1);
				FMeshBatchElement& BatchElement = Mesh.Elements[0];

				BatchElement.IndexBuffer = VertexFactory->GetIndexBuffer(LOD);
			
				BatchElement.IndirectArgsBuffer = VSData->RenderingResources.IndirectDrawArgs;
				BatchElement.IndirectArgsOffset = 0;
			
				BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();

				BatchElement.FirstIndex		= 0;
				BatchElement.NumPrimitives  = 0; // means "use indirect args"
				BatchElement.MinVertexIndex = 0;
				BatchElement.MaxVertexIndex = 0;

				auto* BatchUserData = &Collector.AllocateOneFrameResource<FCustomGrassBatchUserData>();
				BatchUserData->VSData = VSData;
				BatchUserData->DataAssetParams = RenderSystem->GetVertexShaderDataAssetParams();
			
				BatchElement.UserData = BatchUserData;
			
				Collector.AddMesh(ViewIndex, Mesh);
			}
		}
	}
}

SIZE_T FCustomGrassSceneProxy::GetTypeHash() const
{
	static size_t UniquePtr;
	return reinterpret_cast<size_t>(&UniquePtr);
}

uint32 FCustomGrassSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize();
}

FCustomGrassSceneProxy::FMaterialConfig::FMaterialConfig(
	const UMaterialInterface* Material,
	EShaderPlatform ShaderPlatform)
{
	check(Material);
	
	MaterialProxy	  = Material->GetRenderProxy(), 
	MaterialRelevance = Material->GetRelevance_Concurrent(ShaderPlatform);
}
