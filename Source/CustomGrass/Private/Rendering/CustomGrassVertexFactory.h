#pragma once

#include "CoreMinimal.h"
#include "CustomGrassRenderTypes.h"
#include "CustomGrassTypes.h"

class FCustomGrassRenderSystem;

struct FCustomGrassBatchUserData final : FOneFrameResource
{
	const CustomGrass::FProxyVertexShaderData* VSData;
	CustomGrass::FVertexShaderParams DataAssetParams;
};

/**
 * Custom empty index buffer. In theory, a proper index buffer
 * is not needed as we do not use vertex buffers.
 */
class FCustomGrassIndexBuffer : public FIndexBuffer
{
public:
	explicit FCustomGrassIndexBuffer(ECustomGrassLOD LOD)
	: LOD(LOD), NumIndices(CustomGrass::GetGrassBladeIndicesCount(LOD))
	{}
	
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;

	const ECustomGrassLOD LOD;

protected:
	int32 NumIndices;
};

class FCustomGrassVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FCustomGrassVertexFactory);

public:
	FCustomGrassVertexFactory(ERHIFeatureLevel::Type InFeatureLevel, const FCustomGrassRenderSystem* RenderSystem);

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;
	
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters &Parameters);
	
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment ) {}
	static void ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform,
		const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors) {}

	FIndexBuffer* GetIndexBuffer(ECustomGrassLOD LOD) const { return IndexBuffers[static_cast<int32>(LOD)].Get(); }

	const FCustomGrassRenderSystem* GetRenderSystem() const { return RenderSystem; }
	
	static constexpr EVertexFactoryFlags Flags =
		EVertexFactoryFlags::UsedWithMaterials	
	  |	EVertexFactoryFlags::SupportsDynamicLighting
	  |	EVertexFactoryFlags::SupportsManualVertexFetch
	  | EVertexFactoryFlags::SupportsCachingMeshDrawCommands;

protected:
	TStaticArray<TUniquePtr<FCustomGrassIndexBuffer>, CustomGrass::NumLODs> IndexBuffers;

	const FCustomGrassRenderSystem* RenderSystem;
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
	LAYOUT_FIELD(FShaderParameter, TileBufferOffset);
	LAYOUT_FIELD(FShaderParameter, GrassBladeVertexCount);
	LAYOUT_FIELD(FShaderParameter, LOD);

	/* Wind parameters */
	
	LAYOUT_FIELD(FShaderResourceParameter, NoiseTexture);
	LAYOUT_FIELD(FShaderResourceParameter, NoiseSampler);
	LAYOUT_FIELD(FShaderParameter, WindDirection);
	LAYOUT_FIELD(FShaderParameter, WindStrength);
	LAYOUT_FIELD(FShaderParameter, Time);

	/* Thresholds */

	LAYOUT_FIELD(FShaderParameter, MaxGrassHeight);
	LAYOUT_FIELD(FShaderParameter, MaxGrassWidth);
	LAYOUT_FIELD(FShaderParameter, MaxGrassTilt);
	LAYOUT_FIELD(FShaderParameter, MaxGrassBend);

	/* Others */

	LAYOUT_FIELD(FShaderParameter, ViewSpaceCorrection);
	LAYOUT_FIELD(FShaderParameter, NormalRoundnessStrength);
	LAYOUT_FIELD(FShaderParameter, ShortHeightThreshold);
};
