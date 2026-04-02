// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassSceneViewExtension.h"
#include "ShaderParameterStruct.h"
#include "CustomGrassWorldSubsystem.generated.h"

struct FVolatileBuffers;
struct FRenderingResourceHandles;
struct FProxyLandscapeData;
class UCustomGrassDataAsset;
class ULandscapeComponent;
class UCustomGrassPrimitiveComponent;

static constexpr int32 GGrassBladeVertexCount = 15;

static constexpr int32 GIndexedIndirectDrawArgsNum = 5;

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

/** Artist controlled grass parameters. */
BEGIN_SHADER_PARAMETER_STRUCT(FGrassParams,)
	SHADER_PARAMETER(float, Height)
	SHADER_PARAMETER(float, RandHeight)
	SHADER_PARAMETER(float, Width)
	SHADER_PARAMETER(float, RandWidth)
	SHADER_PARAMETER(float, Tilt)
	SHADER_PARAMETER(float, RandTilt)
	SHADER_PARAMETER(float, Bend)
	SHADER_PARAMETER(float, RandBend)
	SHADER_PARAMETER(float, ClumpStrength)
	SHADER_PARAMETER(float, RandClumpStrength)
	SHADER_PARAMETER(uint32, ClumpFacingType)
	SHADER_PARAMETER(float, ClumpFacingStrength)
	SHADER_PARAMETER(int, ClumpGridSize)
END_SHADER_PARAMETER_STRUCT()


class FInstanceGrassBladeCS : public FGlobalShader
{
	BEGIN_SHADER_PARAMETER_STRUCT(FInstanceGrassBladeCSParams,)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FGrassBladeData>, OutInstanceDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutInstanceCounter)
		SHADER_PARAMETER(int32, TileIndex)
		SHADER_PARAMETER(int32, InstanceCountPerTileX)
		SHADER_PARAMETER(int32, InstanceCountPerTileY)
		SHADER_PARAMETER(FMatrix44f, ViewProjectionMatrix)
		SHADER_PARAMETER(FVector4f, ViewOrigin)
		SHADER_PARAMETER(float, MaxRenderDistance)
		SHADER_PARAMETER(FVector4f, HeightmapScaleBias)
		SHADER_PARAMETER(int32, ComponentSizeQuads)
		SHADER_PARAMETER(int32, SectionBaseX)
		SHADER_PARAMETER(int32, SectionBaseY)
		SHADER_PARAMETER(FMatrix44f, LandscapeLocalToWorld)
		SHADER_PARAMETER(FMatrix44f, LandscapeWorldToLocal)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, HeightmapTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HeightmapSampler)
		SHADER_PARAMETER_STRUCT(FGrassParams, GrassParams)
	END_SHADER_PARAMETER_STRUCT()
	
	DECLARE_EXPORTED_GLOBAL_SHADER(FInstanceGrassBladeCS, );
	using FParameters = FInstanceGrassBladeCSParams;
	SHADER_USE_PARAMETER_STRUCT(FInstanceGrassBladeCS, FGlobalShader)

public:
	static inline const FIntVector GroupThreadCount = FIntVector(8, 8, 1);
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
		// SM6 required for wave intrinsics
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& Environment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		
		SET_SHADER_DEFINE(Environment, THREADS_X, GroupThreadCount.X);
		SET_SHADER_DEFINE(Environment, THREADS_Y, GroupThreadCount.Y);
	}
};


class FInitIndirectDrawArgsCS : public FGlobalShader
{
	BEGIN_SHADER_PARAMETER_STRUCT(FInitIndirectDrawArgsCSParams,)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, InInstanceCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutIndirectDrawArgsBuffer)
		SHADER_PARAMETER(int, TileIndex)
	END_SHADER_PARAMETER_STRUCT()
	
	DECLARE_EXPORTED_GLOBAL_SHADER(FInitIndirectDrawArgsCS, );
	using FParameters = FInitIndirectDrawArgsCSParams;
	SHADER_USE_PARAMETER_STRUCT(FInitIndirectDrawArgsCS, FGlobalShader)

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
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

class FCustomGrassRenderSystem
{
	friend class UCustomGrassWorldSubsystem;

	using FRDGPooledBufferRef = TRefCountPtr<FRDGPooledBuffer>;

	/** Rendering work uploaded by a proxy. */
	struct FWorkDesc
	{
		const FSceneView* View;
		float CameraDistanceSq;
		const FProxyLandscapeData* LandscapeData;

		TSharedRef<FRenderingResourceHandles> ResourceHandles;
	};

	/** RT-copy of grass parameters from the data asset. */
	struct FDataAssetProxy
	{
		float Height;
		float RandHeight;
		float Width;
		float RandWidth;
		float Tilt;
		float RandTilt;
		float Bend;
		float RandBend;
		
		float ClumpStrength;
		float RandClumpStrength;
		int ClumpGridSize;
		EClumpFacingType ClumpFacingType;
		float ClumpFacingStrength;
		
		float ShortHeightThreshold;
		
		FWindParams WindParams;
		
		float ViewSpaceCorrection;
		
		float NormalRoundnessStrength;
		
		float MaxRenderDistance;

		FDataAssetProxy() = default;

		explicit FDataAssetProxy(const UCustomGrassDataAsset* const DataAsset)
			: Height(DataAsset->Height), RandHeight(DataAsset->RandomizeHeight),
			Width(DataAsset->Width), RandWidth(DataAsset->RandomizeWidth),
			Tilt(DataAsset->Tilt), RandTilt(DataAsset->RandomizeTilt),
			Bend(DataAsset->Bend), RandBend(DataAsset->RandomizeBend),
			ClumpStrength(DataAsset->ClumpStrength), RandClumpStrength(DataAsset->RandomizeClumpStrength),
			ClumpGridSize(DataAsset->ClumpGridSize), ClumpFacingType(DataAsset->ClumpFacingType),
			ClumpFacingStrength(DataAsset->ClumpFacingStrength), ShortHeightThreshold(DataAsset->ShortHeightThreshold),
			WindParams(
				DataAsset->NoiseTexture
					? DataAsset->NoiseTexture->GetResource()->GetTextureRHI()
					: GBlackTexture->GetTextureRHI(),
				TStaticSamplerState<SF_Point>::GetRHI(),
				DataAsset->WindDirection.GetSafeNormal(),
				DataAsset->WindStrength,
				0.f),
			ViewSpaceCorrection(DataAsset->ViewSpaceCorrection),
			NormalRoundnessStrength(DataAsset->NormalRoundnessStrength),
			MaxRenderDistance(DataAsset->MaxRenderDistance)
		{}
	};

public:
	FCustomGrassRenderSystem();

	~FCustomGrassRenderSystem();
	
	/** Called by renderer before rendering frame: submits accumulated rendering work. */ 
	void BeginFrame(FRDGBuilder& GraphBuilder);

	/** Called by renderer after rendering frame: cleanup of rendering resources. */ 
	void EndFrame(FRDGBuilder& GraphBuilder);

	void AddRenderingWork(const FSceneView* View, float CameraDistanceSq,
		const FProxyLandscapeData* LandscapeData, const TSharedRef<FRenderingResourceHandles>& ResourceHandles);

	FRenderingResourceHandles GetBufferHandles_RenderThread() const;


	/**
	 * A ceil on the number of rendered tiles. This lets us avoid unexpected memory
	 * blow-ups while avoiding costly dynamic resizing of the buffers on each frame.
	 */
	static constexpr int32 MaxRenderedTiles = 8;

	static inline const FIntPoint InstanceCountPerTile = FIntPoint(512, 512);	

protected:

	bool bIsActive;
	
	/** Scheduled rendering work for the current frame. */
	TArray<FWorkDesc> QueuedWork;

	void SubmitWork(FRDGBuilder& GraphBuilder, FVolatileBuffers& InBuffers);

	void InitPerFrameResources(FRDGBuilder& GraphBuilder, FVolatileBuffers& OutBuffers) const;
	

	
	/**
	 * Each of these buffers is made up of several "partitions", one
	 *  for each visible grass tile, in range [(N * i)... (N * i) + N - 1]
	 *  where N represents a known value:
	 *  - for instance data, grass blade number per tile;
	 *  - for indirect draw args, the number of args which is 5.
	 *  
	 *  The index i is then per-frame and dynamic, meaning it's not necessarily
	 *  associated to the same tile each frame.
	 */
	FRDGPooledBufferRef InstanceDataBuffer;

	TStaticArray<FRDGPooledBufferRef, MaxRenderedTiles> IndirectDrawArgsBuffer;

	const FRDGBufferDesc InstanceDataBufferDesc = FRDGBufferDesc::CreateStructuredDesc(
		sizeof(FGrassBladeData), MaxRenderedTiles * InstanceCountPerTile.X * InstanceCountPerTile.Y);

	const FRDGBufferDesc IndirectDrawArgsDesc = FRDGBufferDesc::CreateIndirectDesc(sizeof(uint32),
		GIndexedIndirectDrawArgsNum);
	

	FDataAssetProxy DataAssetProxy;


	/**
	 * Dispatches a compute shader for instancing grass blade data
	 * in a whole landscape tile.\n
	 * Writes to the instance data buffer.
	 */
	void AddComputePass_InstanceGrassBlades(
		FRDGBuilder& GraphBuilder,
		const FWorkDesc& Work,
		const FVolatileBuffers& InBuffers,
		int32 TileIndex
	) const;

	/**
	 * Dispatches a compute shader for initializing the indirect draw args buffer.\n
	 * Dependencies: InstanceGrassBlades compute pass, for the instance count.
	 */
	void AddComputePass_InitIndirectDrawArgs(
		FRDGBuilder& GraphBuilder,
		const FWorkDesc& Work,
		const FVolatileBuffers& InBuffers,
		int32 TileIndex
	) const;

	
	/** Small utility for constructing the view frustum planes array. */
	static TStaticArray<FVector4f, 4> BuildFrustumPlanes_RenderThread(const FSceneView* const View);
};


UCLASS()
class UCustomGrassWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;


	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Editor-only */
	virtual void Tick(float DeltaTime) override;

	virtual bool IsTickable() const override
	{
	#if WITH_EDITOR
		return true;
	#else
		return false;
	#endif
	}

	virtual TStatId GetStatId() const override { return TStatId(); }
	
	FCustomGrassRenderSystem* GetRenderSystem_GameThread() const;

protected:
	TUniquePtr<FCustomGrassRenderSystem> RenderSystem;

	
	UPROPERTY()
	TArray<TObjectPtr<ULandscapeComponent>> LandscapeTiles;
	
	UPROPERTY()
	TArray<TObjectPtr<UCustomGrassPrimitiveComponent>> GrassTileComponents;

	void SpawnComponents();

	void DespawnComponents();
	

	/** Allows changing grass aspect dynamically. */
	UPROPERTY()
	UCustomGrassDataAsset* GrassDataAsset;

	// Delegates: respond to state change -> RecomputeRunningState()
	
	void OnCVarChanged(bool bNewValue);
	void OnDataAssetChanged();

	/** Handles state change that causes system activation / deactivation. */
	void RecomputeRunningState();


	bool bIsActive = false;
};
