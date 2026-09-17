#pragma once

#include "CoreMinimal.h"
#include "Constants.h"
#include "Types.h"
#include "Landscape.h"
#include "LandscapeComponent.h"

class FCustomGrassSceneProxy;

namespace CustomGrass
{
	struct FLODSettings
	{
		int32 VertexCount;
		FIntPoint InstanceCount;
		float DistanceThreshold;
	};

	const auto LOD0Settings = FLODSettings(15, FIntPoint(1024, 1024), 100.f);
	const auto LOD1Settings = FLODSettings(7, FIntPoint(1024, 1024), 200.f);
	const auto LOD2Settings = FLODSettings(7, FIntPoint(512, 512), FLT_MAX);

	const auto LODSettingsMap = TMap<EGrassLOD, FLODSettings>({
		{EGrassLOD::LOD0, LOD0Settings},
		{EGrassLOD::LOD1, LOD1Settings},
		{EGrassLOD::LOD2, LOD2Settings}}
	);

	inline int32 GetGrassBladeVertexCount(EGrassLOD LOD) { return LODSettingsMap[LOD].VertexCount; }
	inline int32 GetGrassBladeTriangleCount(EGrassLOD LOD) { return GetGrassBladeVertexCount(LOD) - 2; }
	inline int32 GetGrassBladeIndicesCount(EGrassLOD LOD) { return GetGrassBladeTriangleCount(LOD) + 2; }

	inline FIntPoint GetInstanceCount(EGrassLOD LOD) { return LODSettingsMap[LOD].InstanceCount; }
	inline float GetDistanceThreshold(EGrassLOD LOD) { return LODSettingsMap[LOD].DistanceThreshold; }

	inline const FIntPoint ShadowMapTextureSlotResolution = FIntPoint(512, 512);

	inline constexpr int32 ShadowMapAtlasGridSize   = MaxRenderedTiles / 2;
	inline const FIntPoint ShadowMapAtlasResolution = ShadowMapTextureSlotResolution * ShadowMapAtlasGridSize;

	struct FRenderingResourceHandles
	{
		FShaderResourceViewRHIRef InstanceData;
		FBufferRHIRef IndirectDrawArgs;
	};

	struct FProxyVertexShaderData
	{
		FRenderingResourceHandles RenderingResources;
		int32 TileOffset = INDEX_NONE;
		EGrassLOD LOD;

		FProxyVertexShaderData(FRenderingResourceHandles RenderingResources, EGrassLOD LOD)
			: RenderingResources(MoveTemp(RenderingResources)), LOD(LOD)
		{}
	};

	struct FWindParams
	{
		FTextureRHIRef NoiseTexture;
		FSamplerStateRHIRef NoiseSampler;
		FVector2f Direction;
		float Strength;
		float Time;
	};

	struct FVertexShaderParams
	{
		float ViewSpaceCorrection;
		float NormalRoundnessStrength;
		float ShortHeightThreshold;
		/* FWindParams WindParams; */
	};

	struct FProxyLandscapeData
	{
		FTextureRHIRef HeightmapTexture;
		FSamplerStateRHIRef HeightmapSampler;
		FVector4 HeightmapScaleBias;
	
		int32 ComponentSizeQuads;
		FIntPoint TotalSizeInQuads;
		FIntPoint SectionBase;
	
		FVector BoundingBox;
	
		FMatrix LocalToWorldMatrix;

		explicit FProxyLandscapeData(const ULandscapeComponent& LandscapeTile)
		{
			HeightmapTexture   = LandscapeTile.GetHeightmap()->GetResource()->GetTextureRHI();
			HeightmapSampler   = TStaticSamplerState<SF_Bilinear>::GetRHI();
			HeightmapScaleBias = LandscapeTile.HeightmapScaleBias;
	
			ComponentSizeQuads = LandscapeTile.ComponentSizeQuads;
			SectionBase		   = FIntPoint(LandscapeTile.SectionBaseX, LandscapeTile.SectionBaseY);

			FIntRect LandscapeExtent;
			LandscapeTile.GetLandscapeInfo()->GetLandscapeExtent(LandscapeExtent);
			TotalSizeInQuads = LandscapeExtent.Max - LandscapeExtent.Min;

			BoundingBox	= LandscapeTile.Bounds.BoxExtent;

			const ALandscape* Landscape = LandscapeTile.GetLandscapeActor();
			LocalToWorldMatrix = Landscape->GetActorTransform().ToMatrixWithScale();
		}
	};
}

struct FCustomGrassBatchUserData final : public FOneFrameResource
{
	const CustomGrass::FProxyVertexShaderData* VSData;
	CustomGrass::FVertexShaderParams DataAssetParams;
};
