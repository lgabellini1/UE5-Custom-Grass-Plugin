// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomGrassSceneProxy.h"
#include "CustomGrassPrimitiveComponent.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "CustomGrassVertexFactory.h"
#include "RenderGraphUtils.h"

FCustomGrassSceneProxy::FCustomGrassSceneProxy(const UCustomGrassPrimitiveComponent* InComponent,
                                               FCustomGrassRenderSystem* InRenderSystem)
: FPrimitiveSceneProxy(InComponent, TEXT("CustomGrassTileProxy")), RenderSystem(InRenderSystem)
{
	if (const UMaterialInterface* Material = InComponent->Material;
		!ensure(Material))
	{
		UE_LOG(LogTemp, Error, TEXT("CustomGrass: Material is NULL!"));
	}
	else
	{
		MaterialProxy = Material->GetRenderProxy();
		MaterialRelevance = Material->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
	}
	
	TObjectPtr<const ULandscapeComponent> LandscapeTile = InComponent->GetLandscapeTile();
	check(LandscapeTile);
	
	// Maintaining a reference should be fine as it's a RHI resource,
	// i.e. a render-thread resource. Same goes for sampler.
	LandscapeData.HeightmapTexture	 = LandscapeTile->GetHeightmap()->GetResource()->GetTextureRHI();
	LandscapeData.HeightmapSampler	 = TStaticSamplerState<SF_Bilinear>::GetRHI();
	LandscapeData.HeightmapScaleBias = FVector4f(LandscapeTile->HeightmapScaleBias);
	LandscapeData.ComponentSizeQuads = LandscapeTile->ComponentSizeQuads;
	LandscapeData.SectionBase		 = FIntPoint(LandscapeTile->SectionBaseX, LandscapeTile->SectionBaseY);
	LandscapeData.LocalToWorld		 = FMatrix44f(LandscapeTile->GetLandscapeActor()->GetActorTransform().ToMatrixWithScale());
	LandscapeData.BoundingBox		 = FVector3f(LandscapeTile->Bounds.BoxExtent);
	
	// @note: this code assumes that the landscape does not change at runtime, and
	// it's position remains unchanged!

	/*
	bCastDynamicShadow = true;
	bCastStaticShadow  = true;
	*/
}

void FCustomGrassSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	VertexFactory = new FCustomGrassVertexFactory(GetScene().GetFeatureLevel());
	VertexFactory->InitResource(RHICmdList);
	
	ResourceHandles = MakeShared<FRenderingResourceHandles>(
		RenderSystem->GetBufferHandles_RenderThread());
}

void FCustomGrassSceneProxy::DestroyRenderThreadResources()
{
	check(VertexFactory);
	
	VertexFactory->ReleaseResource();
	delete VertexFactory;
	VertexFactory = nullptr;

	ResourceHandles = nullptr;	
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

			EGrassLOD LOD;
			bool bWillRender = RenderSystem->AddRenderingWork(View, &LandscapeData,
				ResourceHandles.ToSharedRef(), this, LOD);
			if (!bWillRender)
				continue;

			FMeshBatch& Mesh = Collector.AllocateMesh();
			Mesh.MaterialRenderProxy = MaterialProxy;
			Mesh.VertexFactory = VertexFactory;
			Mesh.Type = PT_TriangleStrip;

			Mesh.bUseForMaterial  = true;
			Mesh.bUseForDepthPass = true;
			Mesh.CastShadow		  = true;

			Mesh.Elements.SetNumZeroed(1);
			FMeshBatchElement& BatchElement = Mesh.Elements[0];

			BatchElement.IndexBuffer = VertexFactory->GetIndexBuffer(LOD);
			
			BatchElement.IndirectArgsBuffer = ResourceHandles->IndirectDrawArgs;
			BatchElement.IndirectArgsOffset = 0;
			
			BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();

			BatchElement.FirstIndex		= 0;
			BatchElement.NumPrimitives  = 0; // means "use indirect args"
			BatchElement.MinVertexIndex = 0;
			BatchElement.MaxVertexIndex = 0;

			auto* BatchUserData = &Collector.AllocateOneFrameResource<FCustomGrassBatchUserData>();
			BatchUserData->ResourceHandles = ResourceHandles.Get();
			BatchUserData->LOD = LOD;
			
			BatchElement.UserData = BatchUserData;
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

FVector GetTileCenter(const FProxyLandscapeData& LandscapeData)
{
	float QuadSize = LandscapeData.LocalToWorld.GetScaleVector().X;

	FVector2f TileCenterInQuads = FVector2f(LandscapeData.SectionBase) + LandscapeData.ComponentSizeQuads * 0.5f;

	return FVector(LandscapeData.LocalToWorld.GetOrigin() + FVector3f(TileCenterInQuads * QuadSize, 0.f));
}

FVector GetTileExtent(const FProxyLandscapeData& LandscapeData)
{
	return FVector(LandscapeData.BoundingBox);
}

FVector GetClosestPointToTile(const FSceneView* View, const FProxyLandscapeData& LandscapeData)
{
	FVector Camera = View->ViewMatrices.GetViewOrigin();

	FVector TileCenter = GetTileCenter(LandscapeData);
	FVector TileExtent = GetTileExtent(LandscapeData);
	
	FVector TileMin = TileCenter - TileExtent;
	FVector TileMax = TileCenter + TileExtent;

	return FVector(
		FMath::Clamp(Camera.X, TileMin.X, TileMax.X),
		FMath::Clamp(Camera.Y, TileMin.Y, TileMax.Y),
		FMath::Clamp(Camera.Z, TileMin.Z, TileMax.Z)
	);
}
