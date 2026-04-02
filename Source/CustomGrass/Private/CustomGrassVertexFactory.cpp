// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomGrassVertexFactory.h"

#include "CustomGrassSceneProxy.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"

IMPLEMENT_VERTEX_FACTORY_TYPE(FCustomGrassVertexFactory, "/CustomShaders/VertexFactory.ush", FCustomGrassVertexFactory::Flags);

IMPLEMENT_TYPE_LAYOUT(FCustomGrassVertexFactoryShaderParams);

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FCustomGrassVertexFactory, SF_Vertex, FCustomGrassVertexFactoryShaderParams);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FCustomGrassVertexFactory, SF_Pixel, FCustomGrassVertexFactoryShaderParams);

FCustomGrassVertexFactory::FCustomGrassVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
	: FVertexFactory(InFeatureLevel)
{
	IndexBuffer = new FCustomGrassIndexBuffer();
}

FCustomGrassVertexFactory::~FCustomGrassVertexFactory()
{
	delete IndexBuffer;
}

void FCustomGrassVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	IndexBuffer->InitResource(RHICmdList);

	FVertexStream NullVertexStream;
	NullVertexStream.VertexBuffer = nullptr;
	NullVertexStream.Stride = 0;
	NullVertexStream.Offset = 0;
	NullVertexStream.VertexStreamUsage = EVertexStreamUsage::ManualFetch;
	
	check(Streams.Num() == 0);
	Streams.Add(NullVertexStream);

	FVertexDeclarationElementList Elements;
	InitDeclaration(Elements);
}

void FCustomGrassVertexFactory::ReleaseRHI()
{
	if (IndexBuffer)
	{
		IndexBuffer->ReleaseResource();
	}

	FVertexFactory::ReleaseRHI();
}

bool FCustomGrassVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	bool bCompile = false;
	
	if (Parameters.MaterialParameters.bIsDefaultMaterial || Parameters.MaterialParameters.bIsSpecialEngineMaterial)
		bCompile = true;
	
	if (Parameters.MaterialParameters.MaterialDomain == MD_Surface
		&& IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6))
		bCompile = true;

#if WITH_EDITOR
	if (bCompile)
		UE_LOG(LogTemp, Display, TEXT("CustomGrass: Compiling permutation for %s"), Parameters.ShaderType->GetName());
#endif
	
	return bCompile;
}

void FCustomGrassVertexFactoryShaderParams::Bind(const FShaderParameterMap& ParameterMap)
{
	InstanceDataBuffer.Bind(ParameterMap, TEXT("InInstanceDataBuffer"));
	TileOffset.Bind(ParameterMap, TEXT("TileOffset"));
	
	NoiseTexture.Bind(ParameterMap, TEXT("NoiseTexture"));
	NoiseSampler.Bind(ParameterMap, TEXT("NoiseSampler"));
	WindDirection.Bind(ParameterMap, TEXT("WindDirection"));
	WindStrength.Bind(ParameterMap, TEXT("WindStrength"));
	Time.Bind(ParameterMap, TEXT("Time"));

	ViewSpaceCorrection.Bind(ParameterMap, TEXT("ViewSpaceCorrection"));

	NormalRoundnessStrength.Bind(ParameterMap, TEXT("NormalRoundnessStrength"));

	ShortHeightThreshold.Bind(ParameterMap, TEXT("ShortHeightThreshold"));
}

void FCustomGrassVertexFactoryShaderParams::GetElementShaderBindings(
	const FSceneInterface* Scene, const FSceneView* View,
	const FMeshMaterialShader* Shader,
	const EVertexInputStreamType InputStreamType,
	ERHIFeatureLevel::Type FeatureLevel,
	const FVertexFactory* VertexFactory,
	const FMeshBatchElement& BatchElement,
	FMeshDrawSingleShaderBindings& ShaderBindings,
	FVertexInputStreamArray& VertexStreams) const
{
	const auto* VSParams = static_cast<const FCustomGrassVertexShaderParams* const>(BatchElement.UserData);
	const FRenderingResourceHandles& Handles = *VSParams->ResourceHandles;

	const_cast<FMeshBatchElement&>(BatchElement).IndirectArgsBuffer = Handles.IndirectDrawArgs;

	ShaderBindings.Add(InstanceDataBuffer, Handles.InstanceData);
	ShaderBindings.Add(TileOffset, Handles.TileOffset);
	
	ShaderBindings.Add(NoiseTexture, Handles.WindParams.NoiseTexture);
	ShaderBindings.Add(NoiseSampler, Handles.WindParams.NoiseSampler);
	ShaderBindings.Add(WindDirection, Handles.WindParams.Direction);
	ShaderBindings.Add(WindStrength, Handles.WindParams.Strength);
	ShaderBindings.Add(Time, Handles.WindParams.Time);

	ShaderBindings.Add(ViewSpaceCorrection, Handles.ViewSpaceCorrection);

	ShaderBindings.Add(NormalRoundnessStrength, Handles.NormalRoundnessStrength);

	ShaderBindings.Add(ShortHeightThreshold, Handles.ShortHeightThreshold);

	UE_LOG(LogTemp, Display, TEXT("GetElementShaderBindings: TileOffset=%d"), Handles.TileOffset);
}
