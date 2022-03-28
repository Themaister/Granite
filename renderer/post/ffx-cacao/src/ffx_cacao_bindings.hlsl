// Modifications Copyright © 2021. Advanced Micro Devices, Inc. All Rights Reserved.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2016, Intel Corporation
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File changes (yyyy-mm-dd)
// 2016-09-07: filip.strugar@intel.com: first commit
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef FFX_CACAO_BINDINGS_HLSL
#define FFX_CACAO_BINDINGS_HLSL

// =============================================================================
// Constants

struct FFX_CACAO_Constants
{
	float2                  DepthUnpackConsts;
	float2                  CameraTanHalfFOV;

	float2                  NDCToViewMul;
	float2                  NDCToViewAdd;

	float2                  DepthBufferUVToViewMul;
	float2                  DepthBufferUVToViewAdd;

	float                   EffectRadius;                           // world (viewspace) maximum size of the shadow
	float                   EffectShadowStrength;                   // global strength of the effect (0 - 5)
	float                   EffectShadowPow;
	float                   EffectShadowClamp;

	float                   EffectFadeOutMul;                       // effect fade out from distance (ex. 25)
	float                   EffectFadeOutAdd;                       // effect fade out to distance   (ex. 100)
	float                   EffectHorizonAngleThreshold;            // limit errors on slopes and caused by insufficient geometry tessellation (0.05 to 0.5)
	float                   EffectSamplingRadiusNearLimitRec;          // if viewspace pixel closer than this, don't enlarge shadow sampling radius anymore (makes no sense to grow beyond some distance, not enough samples to cover everything, so just limit the shadow growth; could be SSAOSettingsFadeOutFrom * 0.1 or less)

	float                   DepthPrecisionOffsetMod;
	float                   NegRecEffectRadius;                     // -1.0 / EffectRadius
	float                   LoadCounterAvgDiv;                      // 1.0 / ( halfDepthMip[SSAO_DEPTH_MIP_LEVELS-1].sizeX * halfDepthMip[SSAO_DEPTH_MIP_LEVELS-1].sizeY )
	float                   AdaptiveSampleCountLimit;

	float                   InvSharpness;
	int                     PassIndex;
	float                   BilateralSigmaSquared;
	float                   BilateralSimilarityDistanceSigma;

	float4                  PatternRotScaleMatrices[5];

	float                   NormalsUnpackMul;
	float                   NormalsUnpackAdd;
	float                   DetailAOStrength;
	float                   Dummy0;

	float2                  SSAOBufferDimensions;
	float2                  SSAOBufferInverseDimensions;

	float2                  DepthBufferDimensions;
	float2                  DepthBufferInverseDimensions;

	int2                    DepthBufferOffset;
	float2                  PerPassFullResUVOffset;

	float2                  OutputBufferDimensions;
	float2                  OutputBufferInverseDimensions;

	float2                  ImportanceMapDimensions;
	float2                  ImportanceMapInverseDimensions;

	float2                  DeinterleavedDepthBufferDimensions;
	float2                  DeinterleavedDepthBufferInverseDimensions;

	float2                  DeinterleavedDepthBufferOffset;
	float2                  DeinterleavedDepthBufferNormalisedOffset;

	float4x4                NormalsWorldToViewspaceMatrix;
};

cbuffer SSAOConstantsBuffer : register(b0)
{
	FFX_CACAO_Constants        g_FFX_CACAO_Consts;
}

// =============================================================================
// Samplers

SamplerState              g_PointClampSampler        : register(s0);
SamplerState              g_PointMirrorSampler       : register(s1);
SamplerState              g_LinearClampSampler       : register(s2);
SamplerState              g_ViewspaceDepthTapSampler : register(s3);
SamplerState              g_RealPointClampSampler    : register(s4);

// =============================================================================
// Clear Load Counter

RWTexture1D<uint> g_ClearLoadCounter_LoadCounter : register(u0);

void FFX_CACAO_ClearLoadCounter_SetLoadCounter(uint val)
{
	g_ClearLoadCounter_LoadCounter[0] = val;
}

// =============================================================================
// Edge Sensitive Blur

Texture2DArray<float2>    g_EdgeSensitiveBlur_Input  : register(t0);
RWTexture2DArray<float2>  g_EdgeSensitiveBlur_Output : register(u0);

float2 FFX_CACAO_EdgeSensitiveBlur_SampleInputOffset(float2 uv, int2 offset)
{
	return g_EdgeSensitiveBlur_Input.SampleLevel(g_PointMirrorSampler, float3(uv, 0.0f), 0.0f, offset);
}

float2 FFX_CACAO_EdgeSensitiveBlur_SampleInput(float2 uv)
{
	return g_EdgeSensitiveBlur_Input.SampleLevel(g_PointMirrorSampler, float3(uv, 0.0f), 0.0f);
}

void FFX_CACAO_EdgeSensitiveBlur_StoreOutput(int2 coord, float2 value)
{
	g_EdgeSensitiveBlur_Output[int3(coord, 0)] = value;
}

// =============================================================================
// SSAO Generation

Texture2DArray<float>    g_ViewspaceDepthSource      : register(t0);
Texture2DArray<float4>   g_DeinterleavedNormals      : register(t1);
Texture1D<uint>          g_LoadCounter               : register(t2);
Texture2D<float>         g_ImportanceMap             : register(t3);
Texture2DArray<float2>   g_FinalSSAO                 : register(t4);

RWTexture2DArray<float2> g_SSAOOutput                : register(u0);

float FFX_CACAO_SSAOGeneration_SampleViewspaceDepthMip(float2 uv, float mip)
{
	return g_ViewspaceDepthSource.SampleLevel(g_ViewspaceDepthTapSampler, float3(uv, 0.0f), mip);
}

float4 FFX_CACAO_SSAOGeneration_GatherViewspaceDepthOffset(float2 uv, int2 offset)
{
	return g_ViewspaceDepthSource.GatherRed(g_PointMirrorSampler, float3(uv, 0.0f), offset);
}

uint FFX_CACAO_SSAOGeneration_GetLoadCounter()
{
	return g_LoadCounter[0];
}

float FFX_CACAO_SSAOGeneration_SampleImportance(float2 uv)
{
	return g_ImportanceMap.SampleLevel(g_LinearClampSampler, uv, 0.0f);
}

float2 FFX_CACAO_SSAOGeneration_LoadBasePassSSAOPass(int2 coord, int pass)
{
	return g_FinalSSAO.Load(int4(coord, pass, 0)).xy;
}

float3 FFX_CACAO_SSAOGeneration_GetNormalPass(int2 coord, int pass)
{
	return g_DeinterleavedNormals[int3(coord, pass)].xyz;
}

void FFX_CACAO_SSAOGeneration_StoreOutput(int2 coord, float2 val)
{
	g_SSAOOutput[int3(coord, 0)] = val;
}

// ============================================================================
// Apply

Texture2DArray<float2> g_ApplyFinalSSAO : register(t0);
RWTexture2D<float>     g_ApplyOutput    : register(u0);

float FFX_CACAO_Apply_SampleSSAOUVPass(float2 uv, int pass)
{
	return g_ApplyFinalSSAO.SampleLevel(g_LinearClampSampler, float3(uv, pass), 0.0f).x;
}

float2 FFX_CACAO_Apply_LoadSSAOPass(int2 coord, int pass)
{
	return g_ApplyFinalSSAO.Load(int4(coord, pass, 0));
}

void FFX_CACAO_Apply_StoreOutput(int2 coord, float val)
{
	g_ApplyOutput[coord] = val;
}

// =============================================================================
// Prepare

Texture2D<float>         g_DepthIn                        : register(t0);
Texture2D<float4>        g_PrepareNormalsFromNormalsInput : register(t0);

RWTexture2DArray<float>  g_PrepareDepthsAndMips_OutMip0 : register(u0);
RWTexture2DArray<float>  g_PrepareDepthsAndMips_OutMip1 : register(u1);
RWTexture2DArray<float>  g_PrepareDepthsAndMips_OutMip2 : register(u2);
RWTexture2DArray<float>  g_PrepareDepthsAndMips_OutMip3 : register(u3);

RWTexture2DArray<float>  g_PrepareDepthsOut : register(u0);

RWTexture2DArray<float4> g_PrepareNormals_NormalOut : register(u0);

float FFX_CACAO_Prepare_SampleDepthOffset(float2 uv, int2 offset)
{
	return g_DepthIn.SampleLevel(g_PointClampSampler, uv, 0.0f, offset);
}

float4 FFX_CACAO_Prepare_GatherDepth(float2 uv)
{
	return g_DepthIn.GatherRed(g_PointClampSampler, uv);
}

float FFX_CACAO_Prepare_LoadDepth(int2 coord)
{
	return g_DepthIn.Load(int3(coord, 0));
}

float FFX_CACAO_Prepare_LoadDepthOffset(int2 coord, int2 offset)
{
	return g_DepthIn.Load(int3(coord, 0), offset);
}

float4 FFX_CACAO_Prepare_GatherDepthOffset(float2 uv, int2 offset)
{
	return g_DepthIn.GatherRed(g_PointClampSampler, uv, offset);
}

float3 FFX_CACAO_Prepare_LoadNormal(int2 coord)
{
	float3 normal = g_PrepareNormalsFromNormalsInput.Load(int3(coord, 0)).xyz;
	normal = normal * g_FFX_CACAO_Consts.NormalsUnpackMul.xxx + g_FFX_CACAO_Consts.NormalsUnpackAdd.xxx;

	// maister: Use column major style.
	normal = mul((float3x3)g_FFX_CACAO_Consts.NormalsWorldToViewspaceMatrix, normal);

	// maister: Translate to typical DX style with +Z into screen.
	normal.z = -normal.z;

	normal = normalize(normal);
	return normal;
}

void FFX_CACAO_Prepare_StoreDepthMip0(int2 coord, int index, float val)
{
	g_PrepareDepthsAndMips_OutMip0[int3(coord, index)] = val;
}

void FFX_CACAO_Prepare_StoreDepthMip1(int2 coord, int index, float val)
{
	g_PrepareDepthsAndMips_OutMip1[int3(coord, index)] = val;
}

void FFX_CACAO_Prepare_StoreDepthMip2(int2 coord, int index, float val)
{
	g_PrepareDepthsAndMips_OutMip2[int3(coord, index)] = val;
}

void FFX_CACAO_Prepare_StoreDepthMip3(int2 coord, int index, float val)
{
	g_PrepareDepthsAndMips_OutMip3[int3(coord, index)] = val;
}

void FFX_CACAO_Prepare_StoreDepth(int2 coord, int index, float val)
{
	g_PrepareDepthsOut[int3(coord, index)] = val;
}

void FFX_CACAO_Prepare_StoreNormal(int2 coord, int index, float3 normal)
{
	g_PrepareNormals_NormalOut[int3(coord, index)] = float4(normal, 1.0f);
}

// =============================================================================
// Importance Map

Texture2DArray<float2> g_ImportanceFinalSSAO : register(t0);
RWTexture2D<float>     g_ImportanceOut       : register(u0);

Texture2D<float>   g_ImportanceAIn  : register(t0);
RWTexture2D<float> g_ImportanceAOut : register(u0);

Texture2D<float>   g_ImportanceBIn          : register(t0);
RWTexture2D<float> g_ImportanceBOut         : register(u0);
RWTexture1D<uint>  g_ImportanceBLoadCounter : register(u1);

float4 FFX_CACAO_Importance_GatherSSAO(float2 uv, int index)
{
	return g_ImportanceFinalSSAO.GatherRed(g_PointClampSampler, float3(uv, index));
}

void FFX_CACAO_Importance_StoreImportance(int2 coord, float val)
{
	g_ImportanceOut[coord] = val;
}

float FFX_CACAO_Importance_SampleImportanceA(float2 uv)
{
	return g_ImportanceAIn.SampleLevel(g_LinearClampSampler, uv, 0.0f);
}

void FFX_CACAO_Importance_StoreImportanceA(int2 coord, float val)
{
	g_ImportanceAOut[coord] = val;
}

float FFX_CACAO_Importance_SampleImportanceB(float2 uv)
{
	return g_ImportanceBIn.SampleLevel(g_LinearClampSampler, uv, 0.0f);
}

void FFX_CACAO_Importance_StoreImportanceB(int2 coord, float val)
{
	g_ImportanceBOut[coord] = val;
}

void FFX_CACAO_Importance_LoadCounterInterlockedAdd(uint val)
{
	InterlockedAdd(g_ImportanceBLoadCounter[0], val);
}

// =============================================================================
// Bilateral Upscale

RWTexture2D<float>     g_BilateralUpscaleOutput            : register(u0);

Texture2DArray<float2> g_BilateralUpscaleInput             : register(t0);
Texture2D<float>       g_BilateralUpscaleDepth             : register(t1);
Texture2DArray<float>  g_BilateralUpscaleDownscaledDepth   : register(t2);

void FFX_CACAO_BilateralUpscale_StoreOutput(int2 coord, int2 offset, float val)
{
	g_BilateralUpscaleOutput[coord + offset] = val;
}

float FFX_CACAO_BilateralUpscale_SampleSSAOLinear(float2 uv, int index)
{
	return g_BilateralUpscaleInput.SampleLevel(g_LinearClampSampler, float3(uv, index), 0).x;
}

float FFX_CACAO_BilateralUpscale_SampleSSAOPoint(float2 uv, int index)
{
	return g_BilateralUpscaleInput.SampleLevel(g_PointClampSampler, float3(uv, index), 0).x;
}

float2 FFX_CACAO_BilateralUpscale_LoadSSAO(int2 coord, int index)
{
	return g_BilateralUpscaleInput.Load(int4(coord, index, 0));
}

float FFX_CACAO_BilateralUpscale_LoadDepth(int2 coord, int2 offset)
{
	return g_BilateralUpscaleDepth.Load(int3(coord, 0), offset);
}

float FFX_CACAO_BilateralUpscale_LoadDownscaledDepth(int2 coord, int index)
{
	return g_BilateralUpscaleDownscaledDepth.Load(int4(coord, index, 0));
}

#endif
