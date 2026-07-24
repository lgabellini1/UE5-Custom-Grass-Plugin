#pragma once

#include "CoreMinimal.h"

UENUM(BlueprintType)
enum class EGrassLOD : uint8
{
	LOD0,
	LOD1,
	LOD2,
	NumLODs
};

struct FLODSettings
{
	int32 VertexCount;
	FIntPoint InstanceCount;
	float DistanceThreshold;
};

/**
 * Landscape information needed for rendering. The proxy maintains a
 * copy of data originally stored by the ULandscapeComponent in the game thread.
 */
struct FProxyLandscapeData
{
	FTextureRHIRef HeightmapTexture;
	FSamplerStateRHIRef HeightmapSampler;
	FVector4f HeightmapScaleBias;
	
	int32 ComponentSizeQuads;
	
	FIntPoint SectionBase;

	FIntPoint TotalSizeInQuads;

	FVector3f BoundingBox;
	
	FMatrix44f LocalToWorld;

	float ShadowProxyPlaneHeight;
};

/**
 * Static references to resources needed by proxy for
 * rendering. The memory these refs point to is expected to be
 * completely handled by the render system.
 */
struct FRenderingResourceHandles
{
	FShaderResourceViewRHIRef InstanceData;
	FBufferRHIRef IndirectDrawArgs;
	int32 TileOffset;
	float ViewSpaceCorrection;
	float NormalRoundnessStrength;
	float ShortHeightThreshold;

	/*
	FWindParams WindParams;
	*/
};

