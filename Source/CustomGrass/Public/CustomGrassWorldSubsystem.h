// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shared.h"
#include "CustomGrassDataAsset.h"
#include "ShaderParameterStruct.h"
#include "CustomGrassWorldSubsystem.generated.h"

class ALandscape;
struct FVolatileBuffers;
struct FRenderingResourceHandles;
struct FProxyLandscapeData;
class UCustomGrassDataAsset;
class ULandscapeComponent;
class UCustomGrassPrimitiveComponent;
class UCustomGrassShadowProxyComponent;
class FCustomGrassSceneProxy;

#define DEBUG_DRAW_TILE_BOUNDS false
#define DEBUG_LOG_TILE_LOD false
#define DEBUG_RENDERED_TILES true

/* Globals */

constexpr int32 GNumLODs = static_cast<int32>(EGrassLOD::NumLODs);

const auto LOD0Settings = FLODSettings(15, FIntPoint(1024, 1024), 100.f);
const auto LOD1Settings = FLODSettings(7, FIntPoint(1024, 1024), 200.f);
const auto LOD2Settings = FLODSettings(7, FIntPoint(512, 512), FLT_MAX);

const auto GLODSettingsMap = TMap<EGrassLOD, FLODSettings>({
	{EGrassLOD::LOD0, LOD0Settings},
	{EGrassLOD::LOD1, LOD1Settings},
	{EGrassLOD::LOD2, LOD2Settings}}
);

inline int32 GetGrassBladeVertexCount(EGrassLOD LOD) { return GLODSettingsMap[LOD].VertexCount; }
inline int32 GetGrassBladeTriangleCount(EGrassLOD LOD) { return GetGrassBladeVertexCount(LOD) - 2; }
inline int32 GetGrassBladeIndicesCount(EGrassLOD LOD) { return GetGrassBladeTriangleCount(LOD) + 2; }

inline FIntPoint GetInstanceCount(EGrassLOD LOD) { return GLODSettingsMap[LOD].InstanceCount; }

inline float GetDistanceThreshold(EGrassLOD LOD) { return GLODSettingsMap[LOD].DistanceThreshold; }

static constexpr int32 GIndexedIndirectDrawArgsNum = 5;

/**
 * A ceil on the number of rendered tiles. This lets us avoid unexpected memory
 * blow-ups while avoiding costly dynamic resizing of the buffers on each frame.
 */
static constexpr int32 GMaxRenderedTiles = 4;

static const FIntPoint GShadowWPOTextureSlotRes = FIntPoint(512, 512);

static constexpr int32 GShadowWPOAtlasGridSize = GMaxRenderedTiles / 2;
static const FIntPoint GShadowWPOAtlasRes = GShadowWPOTextureSlotRes * GShadowWPOAtlasGridSize;


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

/** Memory-efficient version of FGrassBladeData. */
struct FGrassBladeDataPacked
{
	FVector3f Position;
	uint32 Hash;
	uint32 FacingAndNormal; // Facing=uint8[2], Normal=int8[2]
	uint32 HeightWidth; // Height=uint16, Width=uint16
	uint32 TiltBend;// Tilt=uint16, Bend=uint16
};

/** Artist-controlled grass parameters. */
BEGIN_SHADER_PARAMETER_STRUCT(FGrassParams,)
	SHADER_PARAMETER(float, Height)
	SHADER_PARAMETER(float, RandHeight)
	SHADER_PARAMETER(float, MaxHeight)
	SHADER_PARAMETER(float, Width)
	SHADER_PARAMETER(float, RandWidth)
	SHADER_PARAMETER(float, MaxWidth)
	SHADER_PARAMETER(float, Tilt)
	SHADER_PARAMETER(float, RandTilt)
	SHADER_PARAMETER(float, MaxTilt)
	SHADER_PARAMETER(float, Bend)
	SHADER_PARAMETER(float, RandBend)
	SHADER_PARAMETER(float, MaxBend)
	SHADER_PARAMETER(float, ClumpStrength)
	SHADER_PARAMETER(float, RandClumpStrength)
	SHADER_PARAMETER(uint32, ClumpFacingType)
	SHADER_PARAMETER(float, ClumpFacingStrength)
	SHADER_PARAMETER(int, ClumpGridSize)
END_SHADER_PARAMETER_STRUCT()

/* Compute shaders */

struct FTileAtlasMapping
{
	int32 LandscapeCoordX;
	int32 LandscapeCoordY;
	int32 AtlasTileX;
	int32 AtlasTileY;
};

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
		SHADER_PARAMETER(int32, TileIndex)
		SHADER_PARAMETER(int32, GrassBladeVertexCount)
	END_SHADER_PARAMETER_STRUCT()
	
	DECLARE_EXPORTED_GLOBAL_SHADER(FInitIndirectDrawArgsCS, );
	using FParameters = FInitIndirectDrawArgsCSParams;
	SHADER_USE_PARAMETER_STRUCT(FInitIndirectDrawArgsCS, FGlobalShader);

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};


/* Render system */

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

	using FRDGPooledBufferRef  = TRefCountPtr<FRDGPooledBuffer>;
	using FRDGPooledTextureRef = TRefCountPtr<IPooledRenderTarget>;

	/** Rendering work uploaded by a proxy. */
	struct FWorkDesc
	{
		const FVector ViewOrigin;
		const FMatrix ViewMatrix;
		const FProxyLandscapeData LandscapeData;
		const FCustomGrassSceneProxy* GrassProxy;
		const TSharedRef<FRenderingResourceHandles> ResourceHandles;
		EGrassLOD LOD;
		float SortingScore;
		
		int32 TileIndex; // for debug
		
		bool operator==(const FWorkDesc& Other) const
		{
			return (LandscapeData.SectionBase == Other.LandscapeData.SectionBase)
				&& (LOD == Other.LOD);
		}
	};

	/** RT-copy of grass parameters from the data asset. */
	struct FDataAssetProxy
	{
		template<class T = float>
		struct TRandomValue { T Val; float Random; };
		
		TRandomValue<> Height;
		TRandomValue<> Width;
		TRandomValue<> Tilt;
		TRandomValue<> Bend;
		
		TRandomValue<> ClumpStrength;
		int ClumpGridSize;
		EClumpFacingType ClumpFacingType;
		float ClumpFacingStrength;
		
		float ShortHeightThreshold;
		
		float ViewSpaceCorrection;
		
		float NormalRoundnessStrength;
		
		float MaxRenderDistance;

		bool bShadowsOn;
		float ShadowProxyZOffset;

		FWindParams WindParams;

		bool bManualLOD;
		EGrassLOD GlobalLOD;

		FDataAssetProxy() = default;
		explicit FDataAssetProxy(const UCustomGrassDataAsset* const DataAsset);
	};

public:
	FCustomGrassRenderSystem();

	~FCustomGrassRenderSystem();
	
	/** Called by renderer before rendering frame: submits accumulated rendering work. */ 
	void BeginFrame(FRDGBuilder& GraphBuilder);

	/** Called by renderer after rendering frame: cleanup of rendering resources. */ 
	void EndFrame(FRDGBuilder& GraphBuilder);

	/**
	 * Called by proxies to register themselves for rendering work.
	 */ 
	void AddRenderingWork(const FSceneView* View, 
		const FProxyLandscapeData* LandscapeData,
		const TSharedRef<FRenderingResourceHandles>& ResourceHandles,
		const FCustomGrassSceneProxy* Proxy,
		EGrassLOD& InLOD);

	FRenderingResourceHandles GetBufferHandles_RenderThread() const;

	/*void SetGrassDensityRTResource_RenderThread(const FTextureRenderTargetResource* RTResource);*/

	void SetShadowWPOResource_RenderThread(const FTextureRenderTargetResource* RTResource);

	/*void SetMaxDisplacement_RenderThread(float NewVal) { MaxDisplacement = NewVal; }*/

protected:

	bool bIsActive = false;
	bool bResourcesInitialized = false;
	bool bHasActiveSelection = false;
	
	FCriticalSection AddRenderingWorkCS;
	
	/** Scheduled rendering work for the current frame. */
	TArray<FWorkDesc> QueuedWork;

	TArray<FWorkDesc> PreviousFrameWork;
	FMatrix PreviousFrameViewMatrix;

	static bool IsPreviousFrameView(const FMatrix& ThisFrameView, const FMatrix& PrevFrameView);

	void SubmitWork(FRDGBuilder& GraphBuilder, FVolatileBuffers& InBuffers, const TArray<FWorkDesc>& Work);

	void InitPerFrameResources(FRDGBuilder& GraphBuilder, FVolatileBuffers& OutBuffers) const;

	void InitGrassParams();
	
	static float CalcTileSortingScore(const FSceneView* View,
		const FProxyLandscapeData& LandscapeData);
	
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

	TStaticArray<FRDGPooledBufferRef, GMaxRenderedTiles> IndirectDrawArgsBuffer;

	const FRDGBufferDesc InstanceDataBufferDesc = FRDGBufferDesc::CreateStructuredDesc(
		sizeof(FGrassBladeDataPacked),
		GMaxRenderedTiles * GetInstanceCount(EGrassLOD::LOD0).X * GetInstanceCount(EGrassLOD::LOD0).Y);

	const FRDGBufferDesc IndirectDrawArgsDesc = FRDGBufferDesc::CreateIndirectDesc(
		sizeof(uint32), GIndexedIndirectDrawArgsNum);
	
	const FRDGTextureDesc DensityAccumAtlasDesc = FRDGTextureDesc::Create2D(
		GShadowWPOTextureSlotRes * GShadowWPOAtlasGridSize,
		PF_R16_UINT,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV
	);

	FRDGPooledTextureRef ShadowWPOTextureAtlas;

	FRDGBufferRef TileAtlasMappingBuffer;
	void CreateTileAtlasMapping(FRDGBuilder& GraphBuilder, const TArray<FWorkDesc>& Work);
	
	/**
	 * Representation of the data asset as cached on the render-thread.
	 */
	FDataAssetProxy DataAssetProxy;

	FGrassParams GrassParams;
	
	/** Cached heightmap SRVs for this frame. */
	TStaticArray<FRDGTextureSRVRef, GMaxRenderedTiles> TileHeightmaps;

	float MaxDisplacement;
	
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

	virtual bool IsTickable() const override { return WITH_EDITOR; }

	virtual TStatId GetStatId() const override { return TStatId(); }
	
	FCustomGrassRenderSystem* GetRenderSystem() const { return RenderSystem.Get(); }

protected:
	TUniquePtr<FCustomGrassRenderSystem> RenderSystem;

/* Landscape */
	
	UPROPERTY()
	TObjectPtr<ALandscape> LandscapeActor;
	
	UPROPERTY()
	TArray<TObjectPtr<ULandscapeComponent>> LandscapeTiles;

/* System components */
	
	UPROPERTY()
	TArray<TObjectPtr<UCustomGrassPrimitiveComponent>> GrassTileComponents;

	UPROPERTY()
	TArray<TObjectPtr<UCustomGrassShadowProxyComponent>> ShadowProxyComponents;

/* Textures & materials */
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ShadowProxyMID;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> ShadowWPOTextureAtlas;
	
	
	void SpawnComponents();
	void DespawnComponents();

	/** Allows changing grass aspect dynamically. */
	UPROPERTY()
	const UCustomGrassDataAsset* GrassDataAsset;

	// Delegates: respond to state change -> call RecomputeRunningState()
	
	void OnCVarChanged(bool bNewValue);
	void OnDataAssetChanged();
	
	void SetupShadowProxyMaterial();

	/** Handles state change that causes system activation / deactivation. */
	void RecomputeRunningState();

	bool bIsActive = false;
};

FVector2D GetLandscapeExtentInWorldUnits(const ALandscape* Landscape);
