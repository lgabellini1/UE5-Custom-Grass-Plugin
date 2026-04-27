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

#if WITH_EDITOR
	UE_LOG(LogTemp, Warning, TEXT("GetDynamicMeshElements: proxy=%p"), this);
#endif
	
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];

			const auto Camera = FVector3f(View->ViewMatrices.GetViewOrigin());
			
			const FVector3f TileCenter = GetTileCenter(LandscapeData);
			const FVector3f TileHalfExtent = GetTileExtent(LandscapeData);

			const float CameraDist = FVector3f::Distance(TileCenter, Camera);
			const float CameraDistSqr = FMath::Square(CameraDist);
			
			const FVector3f TileMin = TileCenter - TileHalfExtent;
			const FVector3f TileMax = TileCenter + TileHalfExtent;

			const auto Closest = FVector3f(
				FMath::Clamp(Camera.X, TileMin.X, TileMax.X),
				FMath::Clamp(Camera.Y, TileMin.Y, TileMax.Y),
				FMath::Clamp(Camera.Z, TileMin.Z, TileMax.Z)
			);
			
			const float DistanceToTile = FMath::Max(0,
				FVector2f::Distance(FVector2f(Camera), FVector2f(Closest)));

			EGrassLOD LOD = EGrassLOD::LOD0;
			/*if (DistanceToTile <= GetLODDistanceThreshold(EGrassLOD::LOD1))
				LOD = EGrassLOD::LOD0;
			else
				LOD = EGrassLOD::LOD1;*/

			/*
			const FVector3f CameraOrigin = FVector3f(View->ViewMatrices.GetViewOrigin());
			const FVector3f TileCenter	 = GetTileCenter(LandscapeData);
			
			const FVector3f ViewFwd		 = FVector3f(-View->GetViewDirection());

			const FVector3f TileHalfExtent = GetTileExtent(LandscapeData) * 0.5f;

			const FVector3f ClosestPoint = FVector3f(
				FMath::Clamp(CameraOrigin.X, (TileCenter - TileHalfExtent).X, (TileCenter + TileHalfExtent).X),
				FMath::Clamp(CameraOrigin.Y, (TileCenter - TileHalfExtent).Y, (TileCenter + TileHalfExtent).Y),
				FMath::Clamp(CameraOrigin.Z, (TileCenter - TileHalfExtent).Z, (TileCenter + TileHalfExtent).Z));
			
			const FVector3f ToTile = ClosestPoint - CameraOrigin;
			
			float Depth = FMath::Abs(FVector3f::DotProduct(ToTile, ViewFwd));

			const float Threshold = GetLODDistanceThreshold(EGrassLOD::LOD1);
			const EGrassLOD LOD = (Depth >= Threshold) ? EGrassLOD::LOD1 : EGrassLOD::LOD0;
			
			const float CameraDist = FVector3f::Distance(TileCenter, CameraOrigin);
			const float CameraDistSqr = FMath::Square(CameraDist);
			*/

			/*
			const float TileHalfSize = LandscapeData.ComponentSizeQuads
				* LandscapeData.LocalToWorld.GetScaleVector().X * 0.5f;

			const float EffectiveDist = FMath::Max(0.f, CameraDist - TileHalfSize);
			const float EffectiveDistSqr = FMath::Square(EffectiveDist);

			EGrassLOD LOD;
			if (EffectiveDistSqr >= FMath::Square(GetLODDistanceThreshold(EGrassLOD::LOD1)))
				LOD = EGrassLOD::LOD1;
			else
				LOD = EGrassLOD::LOD0;
			*/

			const bool bWillRender = RenderSystem->AddRenderingWork(View, CameraDistSqr, &LandscapeData,
				ResourceHandles.ToSharedRef(), this, LOD);
			if (!bWillRender)
				continue;

			FMeshBatch& Mesh = Collector.AllocateMesh();
			Mesh.MaterialRenderProxy = MaterialProxy;
			Mesh.VertexFactory = VertexFactory;
			Mesh.Type = PT_TriangleStrip;

			Mesh.bUseForMaterial	 = true;
			Mesh.bUseForDepthPass 	 = true;
			Mesh.CastShadow		  	 = true;

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

FVector3f GetTileCenter(const FProxyLandscapeData& LandscapeData)
{
	const float QuadSize = LandscapeData.LocalToWorld.GetScaleVector().X;
	const float ComponentSizeQuads = LandscapeData.ComponentSizeQuads;

	const FVector2f TileCenterInQuads = FVector2f(LandscapeData.SectionBase) + ComponentSizeQuads * 0.5f;

	return LandscapeData.LocalToWorld.GetOrigin() + FVector3f(TileCenterInQuads * QuadSize, 0.f);
}

FVector3f GetTileExtent(const FProxyLandscapeData& LandscapeData)
{
	return LandscapeData.BoundingBox;
}
