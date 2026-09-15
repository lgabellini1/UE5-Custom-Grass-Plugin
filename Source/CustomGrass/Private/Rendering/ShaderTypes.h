#pragma once

namespace CustomGrass
{
	// Must match the same type in shader code!
	struct FGrassBladeDataPacked
	{
		FVector3f Position;
		uint32 Hash;
		// Facing=uint8[2], Normal=int8[2]
		uint32 FacingAndNormal;
		// Height=uint16, Width=uint16
		uint32 HeightAndWidth;
		// Tilt=uint16, Bend=uint16
		uint32 TiltAndBend;
	};

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

	BEGIN_SHADER_PARAMETER_STRUCT(FLandscapeParams,)
		SHADER_PARAMETER(int32, TileSizeInQuads)
		SHADER_PARAMETER(int32, QuadOffsetFromOriginX)
		SHADER_PARAMETER(int32, QuadOffsetFromOriginY)	
		SHADER_PARAMETER(int32, LandscapeSizeInQuadsX)
		SHADER_PARAMETER(int32, LandscapeSizeInQuadsY)
		SHADER_PARAMETER(FMatrix44f, LandscapeLocalToWorld)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, HeightmapTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HeightmapSampler)
		SHADER_PARAMETER(FVector4f, HeightmapScaleBias)
	END_SHADER_PARAMETER_STRUCT()

	BEGIN_SHADER_PARAMETER_STRUCT(FShadowParams,)
		SHADER_PARAMETER(int32, bShadowsOn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutShadowWPOTextureAtlas)
		SHADER_PARAMETER(int32, AtlasOffsetX)
		SHADER_PARAMETER(int32, AtlasOffsetY)
		SHADER_PARAMETER(int32, AtlasSlotSize)
		SHADER_PARAMETER(int32, AtlasGridSize)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTileAtlasMapping>, TileAtlasMapping)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutDensityAccum)
		SHADER_PARAMETER(float, ProxyZOffset)
	END_SHADER_PARAMETER_STRUCT()

	const FIntVector GroupThreadCount = FIntVector(8, 8, 1);
}
