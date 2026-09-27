#pragma once

template <class T>
struct TRandomVariationValue
{
	static_assert(TIsArithmetic<T>::Value,
		"T must be of arithmetic type");
	
	T Value;
	float VariationPercentage;
};

namespace CustomGrass
{
	struct FTextureRenderTargetsGT
	{
		UTextureRenderTarget2D* ShadowMapTextureAtlas;
	};
}
