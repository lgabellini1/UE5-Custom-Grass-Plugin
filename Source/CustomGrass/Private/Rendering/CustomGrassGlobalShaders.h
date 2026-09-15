#pragma once

#include "ShaderParameterStruct.h"
#include "ShaderTypes.h"

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
		SHADER_PARAMETER_STRUCT(CustomGrass::FLandscapeParams, LandscapeParams)
		SHADER_PARAMETER_STRUCT(CustomGrass::FShadowParams, ShadowParams)
		SHADER_PARAMETER_STRUCT(CustomGrass::FGrassParams, GrassParams)
	END_SHADER_PARAMETER_STRUCT()
	
	DECLARE_EXPORTED_GLOBAL_SHADER(FInstanceGrassBladeCS, );
	using FParameters = FInstanceGrassBladeCSParams;
	SHADER_USE_PARAMETER_STRUCT(FInstanceGrassBladeCS, FGlobalShader);

public:
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
