// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomGrassVertexFactory.h"

#include <devicetopology.h>

#include "CustomGrassSceneProxy.h"
#include "LandscapeRender.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"
#include "SkeletonTreeBuilder.h"

IMPLEMENT_VERTEX_FACTORY_TYPE(FCustomGrassVertexFactory, "/CustomShaders/VertexFactory.ush", FCustomGrassVertexFactory::Flags);

IMPLEMENT_TYPE_LAYOUT(FCustomGrassVertexFactoryShaderParams);

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FCustomGrassVertexFactory, SF_Vertex, FCustomGrassVertexFactoryShaderParams);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FCustomGrassVertexFactory, SF_Pixel, FCustomGrassVertexFactoryShaderParams);

FCustomGrassVertexFactory::FCustomGrassVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
	: FVertexFactory(InFeatureLevel)
{
	for (int32 LOD = 0; LOD < GNumLODs; LOD++)
	{
		IndexBuffers[LOD] = new FCustomGrassIndexBuffer(static_cast<EGrassLOD>(LOD));
	}
}

FCustomGrassVertexFactory::~FCustomGrassVertexFactory()
{
	for (const FCustomGrassIndexBuffer* IndexBuffer : IndexBuffers)
	{
		delete IndexBuffer;
	}
}

void FCustomGrassVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	for (FCustomGrassIndexBuffer* IndexBuffer : IndexBuffers)
	{
		IndexBuffer->InitResource(RHICmdList);
	}

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
	for (FCustomGrassIndexBuffer* IndexBuffer : IndexBuffers)
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
	{
		UE_LOG(LogTemp, Display, TEXT("CustomGrass: Compiling permutation for %s"),
			Parameters.ShaderType->GetName());
	}
#endif
	
	return bCompile;
}

void FCustomGrassVertexFactoryShaderParams::Bind(const FShaderParameterMap& ParameterMap)
{
	InstanceDataBuffer.Bind(ParameterMap, TEXT("InInstanceDataBuffer"));
	TileOffset.Bind(ParameterMap, TEXT("TileOffset"));
	GrassBladeVertexCount.Bind(ParameterMap, TEXT("GrassBladeVertexCount"));
	
	NoiseTexture.Bind(ParameterMap, TEXT("NoiseTexture"));
	NoiseSampler.Bind(ParameterMap, TEXT("NoiseSampler"));
	WindDirection.Bind(ParameterMap, TEXT("WindDirection"));
	WindStrength.Bind(ParameterMap, TEXT("WindStrength"));
	Time.Bind(ParameterMap, TEXT("Time"));

	MaxGrassHeight.Bind(ParameterMap, TEXT("MaxGrassHeight"));
	MaxGrassWidth.Bind(ParameterMap, TEXT("MaxGrassWidth"));
	MaxGrassTilt.Bind(ParameterMap, TEXT("MaxGrassTilt"));
	MaxGrassBend.Bind(ParameterMap, TEXT("MaxGrassBend"));

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
	auto* BatchUserData = static_cast<const FCustomGrassBatchUserData*>(BatchElement.UserData);
	
	const FRenderingResourceHandles* Handles = BatchUserData->ResourceHandles;
	check(Handles);

	// Skip invalid tile
	if (Handles->TileOffset == INDEX_NONE)
		return;
	
	const_cast<FMeshBatchElement&>(BatchElement).IndirectArgsBuffer = Handles->IndirectDrawArgs;
	
	ShaderBindings.Add(InstanceDataBuffer, Handles->InstanceData);
	ShaderBindings.Add(TileOffset, Handles->TileOffset);
	ShaderBindings.Add(GrassBladeVertexCount, GetGrassBladeVertexCount(BatchUserData->LOD));

	ShaderBindings.Add(MaxGrassHeight, GMaxGrassBladeHeight);
	ShaderBindings.Add(MaxGrassWidth, GMaxGrassBladeWidth);
	ShaderBindings.Add(MaxGrassTilt, GMaxGrassBladeTilt);
	ShaderBindings.Add(MaxGrassBend, GMaxGrassBladeBend);

	ShaderBindings.Add(ViewSpaceCorrection, Handles->ViewSpaceCorrection);
	ShaderBindings.Add(NormalRoundnessStrength, Handles->NormalRoundnessStrength);
	ShaderBindings.Add(ShortHeightThreshold, Handles->ShortHeightThreshold);
	
	/*
	ShaderBindings.Add(NoiseTexture, ResourceHandles->WindParams.NoiseTexture);
	ShaderBindings.Add(NoiseSampler, ResourceHandles->WindParams.NoiseSampler);
	ShaderBindings.Add(WindDirection, ResourceHandles->WindParams.Direction);
	ShaderBindings.Add(WindStrength, ResourceHandles->WindParams.Strength);
	ShaderBindings.Add(Time, ResourceHandles->WindParams.Time);
	*/
}
