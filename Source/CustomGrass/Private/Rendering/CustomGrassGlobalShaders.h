#pragma once

#include "ShaderParameterStruct.h"
#include "RenderTypes.h"

class FInstanceGrassBladeCS : public FGlobalShader
{
	BEGIN_SHADER_PARAMETER_STRUCT(FInstanceGrassBladeCSParams,)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FGrassBladeDataPacked>, OutInstanceDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutInstanceCounter)
		SHADER_PARAMETER(int32, TileIndex)
		SHADER_PARAMETER(int32, InstanceCountPerTileX)
		SHADER_PARAMETER(int32, InstanceCountPerTileY)
		SHADER_PARAMETER(int32, BufferRegionSize)
		SHADER_PARAMETER(FVector4f, ViewOrigin)
		SHADER_PARAMETER(FMatrix44f, ViewProjectionMatrix)
		SHADER_PARAMETER(float, MaxRenderDistance)
		SHADER_PARAMETER(FVector4f, HeightmapScaleBias)
		SHADER_PARAMETER(int32, TileSizeInQuads)
		SHADER_PARAMETER(int32, QuadOffsetFromOriginX)
		SHADER_PARAMETER(int32, QuadOffsetFromOriginY)	
		SHADER_PARAMETER(int32, LandscapeSizeInQuadsX)
		SHADER_PARAMETER(int32, LandscapeSizeInQuadsY)
		SHADER_PARAMETER(FMatrix44f, LandscapeLocalToWorld)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, HeightmapTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HeightmapSampler)
		SHADER_PARAMETER(int32, bShadowsOn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutShadowWPOTextureAtlas)
		SHADER_PARAMETER(int32, AtlasOffsetX)
		SHADER_PARAMETER(int32, AtlasOffsetY)
		SHADER_PARAMETER(int32, AtlasSlotSize)
		SHADER_PARAMETER(int32, AtlasGridSize)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTileAtlasMapping>, TileAtlasMapping)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutDensityAccum)
		SHADER_PARAMETER(float, ProxyZOffset)
		SHADER_PARAMETER_STRUCT(FGrassParams, GrassParams)
	END_SHADER_PARAMETER_STRUCT()
	
	DECLARE_EXPORTED_GLOBAL_SHADER(FInstanceGrassBladeCS, );
	using FParameters = FInstanceGrassBladeCSParams;
	SHADER_USE_PARAMETER_STRUCT(FInstanceGrassBladeCS, FGlobalShader);

public:
	static inline const FIntVector GroupThreadCount = FIntVector(8, 8, 1);
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& Environment);
};


class FInitIndirectDrawArgsCS : public FGlobalShader
{
	BEGIN_SHADER_PARAMETER_STRUCT(FInitIndirectDrawArgsCSParams,)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, InInstanceCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutIndirectDrawArgsBuffer)
		SHADER_PARAMETER(int32, TileIndex)
		SHADER_PARAMETER(int32, GrassBladeVertexCount)
	END_SHADER_PARAMETER_STRUCT()
	
	DECLARE_EXPORTED_GLOBAL_SHADER(FInitIndirectDrawArgsCS, );
	using FParameters = FInitIndirectDrawArgsCSParams;
	SHADER_USE_PARAMETER_STRUCT(FInitIndirectDrawArgsCS, FGlobalShader);

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};
