#pragma once

#include "CoreMinimal.h"
#include "Shared.h"
#include "Landscape.h"
#include "LandscapeComponent.h"

class FCustomGrassSceneProxy;

UENUM(BlueprintType)
enum class EGrassLOD : uint8
{
	LOD0,
	LOD1,
	LOD2,
	NumLODs UMETA(Hidden)
};

ENUM_RANGE_BY_COUNT(EGrassLOD, EGrassLOD::NumLODs);
constexpr int32 GNumLODs = static_cast<int32>(EGrassLOD::NumLODs);

struct FLODSettings
{
	int32 VertexCount;
	FIntPoint InstanceCount;
	float DistanceThreshold;
};

const auto LOD0Settings = FLODSettings(15, FIntPoint(1024, 1024), 100.f);
const auto LOD1Settings = FLODSettings(7, FIntPoint(1024, 1024), 200.f);
const auto LOD2Settings = FLODSettings(7, FIntPoint(512, 512), FLT_MAX);

const auto GLODSettingsMap = TMap<EGrassLOD, FLODSettings>({
	{EGrassLOD::LOD0, LOD0Settings},
	{EGrassLOD::LOD1, LOD1Settings},
	{EGrassLOD::LOD2, LOD2Settings}}
);

inline int32 GetGrassBladeVertexCount(EGrassLOD LOD) { return GLODSettingsMap[LOD].VertexCount; }
inline int32 GetGrassBladeTriangleCount(EGrassLOD LOD) { return GetGrassBladeVertexCount(LOD) - 2; }
inline int32 GetGrassBladeIndicesCount(EGrassLOD LOD) { return GetGrassBladeTriangleCount(LOD) + 2; }

inline FIntPoint GetInstanceCount(EGrassLOD LOD) { return GLODSettingsMap[LOD].InstanceCount; }
inline float GetDistanceThreshold(EGrassLOD LOD) { return GLODSettingsMap[LOD].DistanceThreshold; }

static constexpr int32 GIndexedIndirectDrawArgsNum = 5;

static const FIntPoint GShadowWPOTextureSlotRes = FIntPoint(512, 512);

static constexpr int32 GShadowWPOAtlasGridSize = GMaxRenderedTiles / 2;
static const FIntPoint GShadowWPOAtlasRes = GShadowWPOTextureSlotRes * GShadowWPOAtlasGridSize;

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

struct FWindParams
{
	FTextureRHIRef NoiseTexture;
	FSamplerStateRHIRef NoiseSampler;
	FVector2f Direction;
	float Strength;
	float Time;
};

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

namespace CustomGrass
{
	struct FVertexShaderParams
	{
		float ViewSpaceCorrection;
		float NormalRoundnessStrength;
		float ShortHeightThreshold;
		/* FWindParams WindParams; */
	};
}

struct FProxyRenderWorkDesc
{
	const FVector ViewOrigin;
	const FMatrix ViewMatrix;
	const FCustomGrassSceneProxy* Proxy;
	TUniquePtr<FProxyVertexShaderData> VSData;
	float TilePriorityScore;
		
	bool operator==(const FProxyRenderWorkDesc& Other) const
	{
		return (Proxy == Other.Proxy) && (VSData->LOD == Other.VSData->LOD);
	}
};

/** Matches the homonymous struct in shader code. */
struct FGrassBladeData
{
	FVector3f Position;
	FVector2f Facing;
	FVector3f TerrainNormal;
	float Height;
	float Width;
	float Tilt;
	float Bend;
	uint32 Hash;
};

/** Memory-efficient version of FGrassBladeData. */
struct FGrassBladeDataPacked
{
	FVector3f Position;
	uint32 Hash;
	uint32 FacingAndNormal; // Facing=uint8[2], Normal=int8[2]
	uint32 HeightWidth; // Height=uint16, Width=uint16
	uint32 TiltBend;// Tilt=uint16, Bend=uint16
};

/** Artist-controlled grass parameters. */
BEGIN_SHADER_PARAMETER_STRUCT(FGrassParams,)
	SHADER_PARAMETER(float, Height)
	SHADER_PARAMETER(float, RandHeight)
	SHADER_PARAMETER(float, MaxHeight)
	SHADER_PARAMETER(float, Width)
	SHADER_PARAMETER(float, RandWidth)
	SHADER_PARAMETER(float, MaxWidth)
	SHADER_PARAMETER(float, Tilt)
	SHADER_PARAMETER(float, RandTilt)
	SHADER_PARAMETER(float, MaxTilt)
	SHADER_PARAMETER(float, Bend)
	SHADER_PARAMETER(float, RandBend)
	SHADER_PARAMETER(float, MaxBend)
	SHADER_PARAMETER(float, ClumpStrength)
	SHADER_PARAMETER(float, RandClumpStrength)
	SHADER_PARAMETER(uint32, ClumpFacingType)
	SHADER_PARAMETER(float, ClumpFacingStrength)
	SHADER_PARAMETER(int, ClumpGridSize)
END_SHADER_PARAMETER_STRUCT()

struct FTileAtlasMapping
{
	FIntPoint LandscapeCoord;
	FIntPoint AtlasTile;
};
