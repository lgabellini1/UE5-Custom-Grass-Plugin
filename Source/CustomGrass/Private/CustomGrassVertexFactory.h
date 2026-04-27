// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CustomGrassWorldSubsystem.h"

/**
 * Custom empty index buffer. In theory, a proper index buffer
 * is not needed as we do not use vertex buffers.
 */
class FCustomGrassIndexBuffer : public FIndexBuffer
{
public:
	explicit FCustomGrassIndexBuffer(EGrassLOD LOD)
	: LOD(LOD), NumIndices(GetGrassBladeVertexCount(LOD))
	{}
	
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		// Taken from RawIndexBuffer.cpp
		
		TResourceArray<uint16, INDEXBUFFER_ALIGNMENT> Indices;
		Indices.SetNumUninitialized(NumIndices);
		
		for (uint16 i = 0; i < NumIndices; i++)
		{
			Indices[i] = i;
		}

		const FRHIBufferCreateDesc BufferDesc = FRHIBufferCreateDesc::CreateIndex(TEXT("CustomGrassIndexBuffer"),
			Indices.GetResourceDataSize(), sizeof(uint16))
		.SetInitialState(ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask)
		.SetInitActionResourceArray(&Indices);

		IndexBufferRHI = RHICmdList.CreateBuffer(BufferDesc);
	}

	const EGrassLOD LOD;

protected:
	int32 NumIndices;
};

struct FCustomGrassBatchUserData : public FOneFrameResource
{
	/*
	FShaderResourceViewRHIRef InstanceDataBuffer;
	int32 TileOffset;
	FWindParams WindParams;
	float ViewSpaceCorrection;
	float NormalRoundnessStrength;
	float ShortHeightThreshold;
	*/
	const FRenderingResourceHandles* ResourceHandles;
	EGrassLOD LOD;
};


class FCustomGrassVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FCustomGrassVertexFactory);

public:
	explicit FCustomGrassVertexFactory(ERHIFeatureLevel::Type InFeatureLevel);

	virtual ~FCustomGrassVertexFactory() override;

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;
	
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters &Parameters);
	
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment ) {}
	static void ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform,
		const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors) {}

	FIndexBuffer* GetIndexBuffer(EGrassLOD LOD) const { return IndexBuffers[static_cast<int32>(LOD)]; }
	
	static constexpr EVertexFactoryFlags Flags =
		EVertexFactoryFlags::UsedWithMaterials	
//	  | EVertexFactoryFlags::SupportsStaticLighting    
	  |	EVertexFactoryFlags::SupportsDynamicLighting
	  |	EVertexFactoryFlags::SupportsManualVertexFetch
	  | EVertexFactoryFlags::SupportsCachingMeshDrawCommands;

protected:
	TStaticArray<FCustomGrassIndexBuffer*, GNumLODs> IndexBuffers;
};


class FCustomGrassVertexFactoryShaderParams : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FCustomGrassVertexFactoryShaderParams, NonVirtual);

public:
	void Bind(const FShaderParameterMap& ParameterMap);

	void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const;

protected:
	LAYOUT_FIELD(FShaderResourceParameter, InstanceDataBuffer);

	LAYOUT_FIELD(FShaderParameter, TileOffset);

	LAYOUT_FIELD(FShaderParameter, GrassBladeVertexCount);

	/* Wind parameters */
	
	LAYOUT_FIELD(FShaderResourceParameter, NoiseTexture);
	LAYOUT_FIELD(FShaderResourceParameter, NoiseSampler);
	LAYOUT_FIELD(FShaderParameter, WindDirection);
	LAYOUT_FIELD(FShaderParameter, WindStrength);
	LAYOUT_FIELD(FShaderParameter, Time);

	LAYOUT_FIELD(FShaderParameter, ViewSpaceCorrection);

	LAYOUT_FIELD(FShaderParameter, NormalRoundnessStrength);

	LAYOUT_FIELD(FShaderParameter, ShortHeightThreshold);
};
