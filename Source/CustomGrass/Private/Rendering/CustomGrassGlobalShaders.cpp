#include "CustomGrassGlobalShaders.h"

IMPLEMENT_GLOBAL_SHADER(FInstanceGrassBladeCS, "/CustomShaders/Compute.usf", "CSInstanceGrassBlades", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FInitIndirectDrawArgsCS, "/CustomShaders/Compute.usf", "CSInitIndirectDrawArgs", SF_Compute);

bool FInstanceGrassBladeCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform,
		ERHIFeatureLevel::SM6);	// SM6 required for wave intrinsics
}

void FInstanceGrassBladeCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
	FShaderCompilerEnvironment& Environment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		
	SET_SHADER_DEFINE(Environment, THREADS_X, GroupThreadCount.X);
	SET_SHADER_DEFINE(Environment, THREADS_Y, GroupThreadCount.Y);
}

bool FInitIndirectDrawArgsCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}
