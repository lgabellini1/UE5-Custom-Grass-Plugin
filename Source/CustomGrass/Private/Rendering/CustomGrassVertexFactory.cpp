#include "CustomGrassVertexFactory.h"
#include "CustomGrassRenderSystem.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"

IMPLEMENT_VERTEX_FACTORY_TYPE(FCustomGrassVertexFactory, "/CustomShaders/VertexFactory.ush", FCustomGrassVertexFactory::Flags);

IMPLEMENT_TYPE_LAYOUT(FCustomGrassVertexFactoryShaderParams);

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FCustomGrassVertexFactory, SF_Vertex, FCustomGrassVertexFactoryShaderParams);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FCustomGrassVertexFactory, SF_Pixel, FCustomGrassVertexFactoryShaderParams);

FCustomGrassIndexBuffer::FCustomGrassIndexBuffer(ECustomGrassLOD LOD)
: LOD(LOD), NumIndices(CustomGrass::GetGrassBladeIndicesCount(LOD))
{}

void FCustomGrassIndexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	// Implementation taken from RawIndexBuffer.cpp
		
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

FCustomGrassVertexFactory::FCustomGrassVertexFactory(ERHIFeatureLevel::Type InFeatureLevel,
	const FCustomGrassRenderSystem* RenderSystem)
: FVertexFactory(InFeatureLevel), RenderSystem(RenderSystem)
{
	for (ECustomGrassLOD LOD : TEnumRange<ECustomGrassLOD>())
	{
		IndexBuffers[static_cast<int32>(LOD)] = MakeUnique<FCustomGrassIndexBuffer>(LOD);
	}
}

void FCustomGrassVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	for (ECustomGrassLOD LOD : TEnumRange<ECustomGrassLOD>())
	{
		IndexBuffers[static_cast<int32>(LOD)]->InitResource(RHICmdList);
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
	for (ECustomGrassLOD LOD : TEnumRange<ECustomGrassLOD>())
	{
		IndexBuffers[static_cast<int32>(LOD)]->ReleaseResource();
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
	TileBufferOffset.Bind(ParameterMap, TEXT("TileBufferOffset"));
	GrassBladeVertexCount.Bind(ParameterMap, TEXT("GrassBladeVertexCount"));
	LOD.Bind(ParameterMap, TEXT("LOD"));

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
	
	const CustomGrass::FVertexShaderParams DataAssetParams = BatchUserData->DataAssetParams;
	
	const CustomGrass::FProxyVertexShaderData* VSData = BatchUserData->VSData;
	
	checkf(VSData, TEXT("CustomGrass: vertex shader resources are NULL!"));
	
	checkf(VSData->TileBufferOffset != INDEX_NONE,
		TEXT("CustomGrass: vertex shader resources were not initialized by the render system!"));

	const auto* RenderSystem = static_cast<const FCustomGrassVertexFactory*>(VertexFactory)->RenderSystem;
	RenderSystem->CompareAndCheckVSResourcesValidity(VSData);
		
	ShaderBindings.Add(InstanceDataBuffer, VSData->RenderingResources.InstanceData);
	ShaderBindings.Add(TileBufferOffset, VSData->TileBufferOffset);
	ShaderBindings.Add(GrassBladeVertexCount, CustomGrass::GetGrassBladeVertexCount(VSData->LOD));
	ShaderBindings.Add(LOD, static_cast<int32>(VSData->LOD));

	ShaderBindings.Add(MaxGrassHeight, CustomGrass::MaxGrassBladeHeight);
	ShaderBindings.Add(MaxGrassWidth, CustomGrass::MaxGrassBladeWidth);
	ShaderBindings.Add(MaxGrassTilt, CustomGrass::MaxGrassBladeTilt);
	ShaderBindings.Add(MaxGrassBend, CustomGrass::MaxGrassBladeBend);

	ShaderBindings.Add(ViewSpaceCorrection, DataAssetParams.ViewSpaceCorrection);
	ShaderBindings.Add(NormalRoundnessStrength, DataAssetParams.NormalRoundnessStrength);
	ShaderBindings.Add(ShortHeightThreshold, DataAssetParams.ShortHeightThreshold);
}
