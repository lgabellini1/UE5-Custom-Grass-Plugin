// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomGrassSceneProxy.h"
#include "CustomGrassPrimitiveComponent.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "CustomGrassVertexFactory.h"

FCustomGrassSceneProxy::FCustomGrassSceneProxy(const UCustomGrassPrimitiveComponent* InComponent,
	FCustomGrassRenderSystem* InRenderSystem)
: FPrimitiveSceneProxy(InComponent, TEXT("CustomGrassTileProxy")), RenderSystem(InRenderSystem)
{
	const UMaterialInterface* Material = InComponent->Material;
	
	if (!ensure(Material))
	{
		UE_LOG(LogTemp, Error, TEXT("Material is NULL"));
	}
	else
	{
		MaterialProxy = Material->GetRenderProxy();
		MaterialRelevance = Material->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
	}
	
	ULandscapeComponent* LandscapeTile = InComponent->GetAssociatedTile();
	check(LandscapeTile);

	ALandscape* Landscape = LandscapeTile->GetLandscapeActor();

	// Maintaining a reference should be fine as it's a RHI resource,
	// i.e. a render-thread resource. Same goes for sampler.
	LandscapeData.HeightmapTexture	 = LandscapeTile->GetHeightmap()->GetResource()->GetTextureRHI();
	LandscapeData.HeightmapSampler	 = TStaticSamplerState<SF_Bilinear>::GetRHI();
	LandscapeData.HeightmapScaleBias = FVector4f(LandscapeTile->HeightmapScaleBias);
	LandscapeData.ComponentSizeQuads = LandscapeTile->ComponentSizeQuads;
	LandscapeData.SectionBase		 = FIntPoint(LandscapeTile->SectionBaseX, LandscapeTile->SectionBaseY);
	LandscapeData.LocalToWorld		 = FMatrix44f(Landscape->GetActorTransform().ToMatrixWithScale());
	LandscapeData.WorldToLocal		 = LandscapeData.LocalToWorld.Inverse();
	
	// @note: this code assumes that the landscape does not change at runtime, and
	// it's position remains unchanged!

	bCastDynamicShadow = true;
	bCastStaticShadow  = true;
}

void FCustomGrassSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	VertexFactory = new FCustomGrassVertexFactory(GetScene().GetFeatureLevel());
	VertexFactory->InitResource(RHICmdList);
	
	ResourceHandles = MakeShared<FRenderingResourceHandles>(RenderSystem->GetBufferHandles_RenderThread());
}

void FCustomGrassSceneProxy::DestroyRenderThreadResources()
{
	check(VertexFactory);
	
	VertexFactory->ReleaseResource();
	delete VertexFactory;
	VertexFactory = nullptr;

	ResourceHandles->InstanceData = nullptr;
	ResourceHandles->IndirectDrawArgs = nullptr;	
}

FPrimitiveViewRelevance FCustomGrassSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	UE_LOG(LogTemp, Display, TEXT("CustomGrass: GetViewRelevance"));
	
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
	
	MaterialRelevance.SetPrimitiveViewRelevance(Relevance);
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

			UE_LOG(LogTemp, Display, TEXT("GetDynamicMeshElements: ViewIndex=%d, bIsPlanarReflection=%d, bIsSceneCapture=%d"),
				ViewIndex,
				View->bIsPlanarReflection,
				View->bIsSceneCapture);

			auto CameraPos = FVector3f(View->ViewMatrices.GetViewOrigin());
			float CameraDistanceSq = FVector3f::DistSquared(LandscapeData.LocalToWorld.GetOrigin(), CameraPos);

			RenderSystem->AddRenderingWork(View, CameraDistanceSq, &LandscapeData,
				ResourceHandles.ToSharedRef());

			FMeshBatch& Mesh = Collector.AllocateMesh();
			Mesh.MaterialRenderProxy = MaterialProxy;
			Mesh.VertexFactory = VertexFactory;
			Mesh.Type = PT_TriangleStrip;

			Mesh.bUseForMaterial	 = true;
			Mesh.bUseForDepthPass 	 = true;
			Mesh.CastShadow		  	 = true;
//			Mesh.CastRayTracedShadow = true;
//			Mesh.bUseAsOccluder		 = true;

			Mesh.Elements.SetNumZeroed(1);
			FMeshBatchElement& BatchElement = Mesh.Elements[0];

			BatchElement.IndexBuffer = VertexFactory->GetIndexBuffer();
			
			BatchElement.IndirectArgsBuffer = ResourceHandles->IndirectDrawArgs;
			BatchElement.IndirectArgsOffset = 0;
			
			BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();

			BatchElement.FirstIndex		= 0;
			BatchElement.NumPrimitives  = 0; // means "use indirect args"
			BatchElement.MinVertexIndex = 0;
			BatchElement.MaxVertexIndex = 0;

			auto* VSParams = &Collector.AllocateOneFrameResource<FCustomGrassVertexShaderParams>();
			VSParams->ResourceHandles = &(*ResourceHandles);
			BatchElement.UserData = VSParams;
			/*
			VSParams->InstanceDataBuffer = ResourceHandles->InstanceData;
			VSParams->TileOffset		 = ResourceHandles->TileOffset;
			
			VSParams->WindParams = ResourceHandles->WindParams;

			VSParams->ViewSpaceCorrection = ResourceHandles->ViewSpaceCorrection;

			VSParams->NormalRoundnessStrength = ResourceHandles->NormalRoundnessStrength;

			VSParams->ShortHeightThreshold = ResourceHandles->ShortHeightThreshold;
			*/

			Collector.AddMesh(ViewIndex, Mesh);			
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
	return sizeof(*this) + FPrimitiveSceneProxy::GetAllocatedSize();
}
