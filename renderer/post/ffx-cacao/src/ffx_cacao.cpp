// Modifications Copyright (c) 2021. Advanced Micro Devices, Inc. All Rights Reserved.

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

#include "ffx_cacao.h"

#include <assert.h>
#include <math.h>   // cos, sin
#include <string.h> // memcpy
#include <stdio.h>  // snprintf

// Granite implementation: Copyright (c) 2022-2024 Hans-Kristian Arntzen

// Define symbol to enable DirectX debug markers created using Cauldron
#define FFX_CACAO_ENABLE_CAULDRON_DEBUG

#define FFX_CACAO_ASSERT(exp) assert(exp)
#define FFX_CACAO_ARRAY_SIZE(xs) (sizeof(xs)/sizeof(xs[0]))
#define FFX_CACAO_COS(x) cosf(x)
#define FFX_CACAO_SIN(x) sinf(x)
#define FFX_CACAO_MIN(x, y) (((x) < (y)) ? (x) : (y))
#define FFX_CACAO_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define FFX_CACAO_CLAMP(value, lower, upper) FFX_CACAO_MIN(FFX_CACAO_MAX(value, lower), upper)
#define FFX_CACAO_OFFSET_OF(T, member) (size_t)(&(((T*)0)->member))

#define MATRIX_ROW_MAJOR_ORDER 1
static const FFX_CACAO_Matrix4x4 FFX_CACAO_IDENTITY_MATRIX = {
	{ { 1.0f, 0.0f, 0.0f, 0.0f },
	  { 0.0f, 1.0f, 0.0f, 0.0f },
	  { 0.0f, 0.0f, 1.0f, 0.0f },
	  { 0.0f, 0.0f, 0.0f, 1.0f } }
};

void FFX_CACAO_UpdateBufferSizeInfo(uint32_t width, uint32_t height, FFX_CACAO_Bool useDownsampledSsao, FFX_CACAO_BufferSizeInfo* bsi)
{
	uint32_t halfWidth = (width + 1) / 2;
	uint32_t halfHeight = (height + 1) / 2;
	uint32_t quarterWidth = (halfWidth + 1) / 2;
	uint32_t quarterHeight = (halfHeight + 1) / 2;
	uint32_t eighthWidth = (quarterWidth + 1) / 2;
	uint32_t eighthHeight = (quarterHeight + 1) / 2;

	uint32_t depthBufferWidth = width;
	uint32_t depthBufferHeight = height;
	uint32_t depthBufferHalfWidth = halfWidth;
	uint32_t depthBufferHalfHeight = halfHeight;
	uint32_t depthBufferQuarterWidth = quarterWidth;
	uint32_t depthBufferQuarterHeight = quarterHeight;

	uint32_t depthBufferXOffset = 0;
	uint32_t depthBufferYOffset = 0;
	uint32_t depthBufferHalfXOffset = 0;
	uint32_t depthBufferHalfYOffset = 0;
	uint32_t depthBufferQuarterXOffset = 0;
	uint32_t depthBufferQuarterYOffset = 0;

	bsi->inputOutputBufferWidth = width;
	bsi->inputOutputBufferHeight = height;
	bsi->depthBufferXOffset = depthBufferXOffset;
	bsi->depthBufferYOffset = depthBufferYOffset;
	bsi->depthBufferWidth = depthBufferWidth;
	bsi->depthBufferHeight = depthBufferHeight;

	if (useDownsampledSsao)
	{
		bsi->ssaoBufferWidth = quarterWidth;
		bsi->ssaoBufferHeight = quarterHeight;
		bsi->deinterleavedDepthBufferXOffset = depthBufferQuarterXOffset;
		bsi->deinterleavedDepthBufferYOffset = depthBufferQuarterYOffset;
		bsi->deinterleavedDepthBufferWidth = depthBufferQuarterWidth;
		bsi->deinterleavedDepthBufferHeight = depthBufferQuarterHeight;
		bsi->importanceMapWidth = eighthWidth;
		bsi->importanceMapHeight = eighthHeight;
		bsi->downsampledSsaoBufferWidth = halfWidth;
		bsi->downsampledSsaoBufferHeight = halfHeight;
	}
	else
	{
		bsi->ssaoBufferWidth = halfWidth;
		bsi->ssaoBufferHeight = halfHeight;
		bsi->deinterleavedDepthBufferXOffset = depthBufferHalfXOffset;
		bsi->deinterleavedDepthBufferYOffset = depthBufferHalfYOffset;
		bsi->deinterleavedDepthBufferWidth = depthBufferHalfWidth;
		bsi->deinterleavedDepthBufferHeight = depthBufferHalfHeight;
		bsi->importanceMapWidth = quarterWidth;
		bsi->importanceMapHeight = quarterHeight;
		bsi->downsampledSsaoBufferWidth = 1;
		bsi->downsampledSsaoBufferHeight = 1;
	}
}

void FFX_CACAO_UpdateConstants(FFX_CACAO_Constants* consts, const FFX_CACAO_Settings* settings, const FFX_CACAO_BufferSizeInfo* bufferSizeInfo, const FFX_CACAO_Matrix4x4* proj, const FFX_CACAO_Matrix4x4* normalsToView)
{
	consts->BilateralSigmaSquared = settings->bilateralSigmaSquared;
	consts->BilateralSimilarityDistanceSigma = settings->bilateralSimilarityDistanceSigma;

	if (settings->generateNormals)
	{
		consts->NormalsWorldToViewspaceMatrix = FFX_CACAO_IDENTITY_MATRIX;
	}
	else
	{
		consts->NormalsWorldToViewspaceMatrix = *normalsToView;
	}

	// used to get average load per pixel; 9.0 is there to compensate for only doing every 9th InterlockedAdd in PSPostprocessImportanceMapB for performance reasons
	consts->LoadCounterAvgDiv = 9.0f / (float)(bufferSizeInfo->importanceMapWidth * bufferSizeInfo->importanceMapHeight * 255.0);

	// maister: Row-major here is kinda a lie. This code is actually intended for column major (?!?!).
	float depthLinearizeMul = (MATRIX_ROW_MAJOR_ORDER) ? (-proj->elements[3][2]) : (-proj->elements[2][3]);           // float depthLinearizeMul = ( clipFar * clipNear ) / ( clipFar - clipNear );
	float depthLinearizeAdd = (MATRIX_ROW_MAJOR_ORDER) ? (proj->elements[2][2]) : (proj->elements[2][2]);           // float depthLinearizeAdd = clipFar / ( clipFar - clipNear );
	// correct the handedness issue. need to make sure this below is correct, but I think it is.
	if (depthLinearizeMul * depthLinearizeAdd < 0)
		depthLinearizeAdd = -depthLinearizeAdd;
	consts->DepthUnpackConsts[0] = depthLinearizeMul;
	consts->DepthUnpackConsts[1] = depthLinearizeAdd;

	// maister: Flip Y here since this code does not expect that to be a thing. It expects DX style clip space.
	float tanHalfFOVY = 1.0f / -proj->elements[1][1];    // = tanf( drawContext.Camera.GetYFOV( ) * 0.5f );
	float tanHalfFOVX = 1.0F / proj->elements[0][0];    // = tanHalfFOVY * drawContext.Camera.GetAspect( );
	consts->CameraTanHalfFOV[0] = tanHalfFOVX;
	consts->CameraTanHalfFOV[1] = tanHalfFOVY;

	consts->NDCToViewMul[0] = consts->CameraTanHalfFOV[0] * 2.0f;
	consts->NDCToViewMul[1] = consts->CameraTanHalfFOV[1] * -2.0f;
	consts->NDCToViewAdd[0] = consts->CameraTanHalfFOV[0] * -1.0f;
	consts->NDCToViewAdd[1] = consts->CameraTanHalfFOV[1] * 1.0f;

	float ratio = ((float)bufferSizeInfo->inputOutputBufferWidth) / ((float)bufferSizeInfo->depthBufferWidth);
	float border = (1.0f - ratio) / 2.0f;
	for (int i = 0; i < 2; ++i)
	{
		consts->DepthBufferUVToViewMul[i] = consts->NDCToViewMul[i] / ratio;
		consts->DepthBufferUVToViewAdd[i] = consts->NDCToViewAdd[i] - consts->NDCToViewMul[i] * border / ratio;
	}

	consts->EffectRadius = FFX_CACAO_CLAMP(settings->radius, 0.0f, 100000.0f);
	consts->EffectShadowStrength = FFX_CACAO_CLAMP(settings->shadowMultiplier * 4.3f, 0.0f, 10.0f);
	consts->EffectShadowPow = FFX_CACAO_CLAMP(settings->shadowPower, 0.0f, 10.0f);
	consts->EffectShadowClamp = FFX_CACAO_CLAMP(settings->shadowClamp, 0.0f, 1.0f);
	consts->EffectFadeOutMul = -1.0f / (settings->fadeOutTo - settings->fadeOutFrom);
	consts->EffectFadeOutAdd = settings->fadeOutFrom / (settings->fadeOutTo - settings->fadeOutFrom) + 1.0f;
	consts->EffectHorizonAngleThreshold = FFX_CACAO_CLAMP(settings->horizonAngleThreshold, 0.0f, 1.0f);

	// 1.2 seems to be around the best trade off - 1.0 means on-screen radius will stop/slow growing when the camera is at 1.0 distance, so, depending on FOV, basically filling up most of the screen
	// This setting is viewspace-dependent and not screen size dependent intentionally, so that when you change FOV the effect stays (relatively) similar.
	float effectSamplingRadiusNearLimit = (settings->radius * 1.2f);

	// if the depth precision is switched to 32bit float, this can be set to something closer to 1 (0.9999 is fine)
	consts->DepthPrecisionOffsetMod = 0.9992f;

	// Special settings for lowest quality level - just nerf the effect a tiny bit
	if (settings->qualityLevel <= FFX_CACAO_QUALITY_LOW)
	{
		//consts.EffectShadowStrength     *= 0.9f;
		effectSamplingRadiusNearLimit *= 1.50f;

		if (settings->qualityLevel == FFX_CACAO_QUALITY_LOWEST)
			consts->EffectRadius *= 0.8f;
	}

	effectSamplingRadiusNearLimit /= tanHalfFOVY; // to keep the effect same regardless of FOV

	consts->EffectSamplingRadiusNearLimitRec = 1.0f / effectSamplingRadiusNearLimit;

	consts->AdaptiveSampleCountLimit = settings->adaptiveQualityLimit;

	consts->NegRecEffectRadius = -1.0f / consts->EffectRadius;

	consts->InvSharpness = FFX_CACAO_CLAMP(1.0f - settings->sharpness, 0.0f, 1.0f);

	consts->DetailAOStrength = settings->detailShadowStrength;

	// set buffer size constants.
	consts->SSAOBufferDimensions[0] = (float)bufferSizeInfo->ssaoBufferWidth;
	consts->SSAOBufferDimensions[1] = (float)bufferSizeInfo->ssaoBufferHeight;
	consts->SSAOBufferInverseDimensions[0] = 1.0f / ((float)bufferSizeInfo->ssaoBufferWidth);
	consts->SSAOBufferInverseDimensions[1] = 1.0f / ((float)bufferSizeInfo->ssaoBufferHeight);

	consts->DepthBufferDimensions[0] = (float)bufferSizeInfo->depthBufferWidth;
	consts->DepthBufferDimensions[1] = (float)bufferSizeInfo->depthBufferHeight;
	consts->DepthBufferInverseDimensions[0] = 1.0f / ((float)bufferSizeInfo->depthBufferWidth);
	consts->DepthBufferInverseDimensions[1] = 1.0f / ((float)bufferSizeInfo->depthBufferHeight);

	consts->DepthBufferOffset[0] = bufferSizeInfo->depthBufferXOffset;
	consts->DepthBufferOffset[1] = bufferSizeInfo->depthBufferYOffset;

	consts->InputOutputBufferDimensions[0] = (float)bufferSizeInfo->inputOutputBufferWidth;
	consts->InputOutputBufferDimensions[1] = (float)bufferSizeInfo->inputOutputBufferHeight;
	consts->InputOutputBufferInverseDimensions[0] = 1.0f / ((float)bufferSizeInfo->inputOutputBufferWidth);
	consts->InputOutputBufferInverseDimensions[1] = 1.0f / ((float)bufferSizeInfo->inputOutputBufferHeight);

	consts->ImportanceMapDimensions[0] = (float)bufferSizeInfo->importanceMapWidth;
	consts->ImportanceMapDimensions[1] = (float)bufferSizeInfo->importanceMapHeight;
	consts->ImportanceMapInverseDimensions[0] = 1.0f / ((float)bufferSizeInfo->importanceMapWidth);
	consts->ImportanceMapInverseDimensions[1] = 1.0f / ((float)bufferSizeInfo->importanceMapHeight);

	consts->DeinterleavedDepthBufferDimensions[0] = (float)bufferSizeInfo->deinterleavedDepthBufferWidth;
	consts->DeinterleavedDepthBufferDimensions[1] = (float)bufferSizeInfo->deinterleavedDepthBufferHeight;
	consts->DeinterleavedDepthBufferInverseDimensions[0] = 1.0f / ((float)bufferSizeInfo->deinterleavedDepthBufferWidth);
	consts->DeinterleavedDepthBufferInverseDimensions[1] = 1.0f / ((float)bufferSizeInfo->deinterleavedDepthBufferHeight);

	consts->DeinterleavedDepthBufferOffset[0] = (float)bufferSizeInfo->deinterleavedDepthBufferXOffset;
	consts->DeinterleavedDepthBufferOffset[1] = (float)bufferSizeInfo->deinterleavedDepthBufferYOffset;
	consts->DeinterleavedDepthBufferNormalisedOffset[0] = ((float)bufferSizeInfo->deinterleavedDepthBufferXOffset) / ((float)bufferSizeInfo->deinterleavedDepthBufferWidth);
	consts->DeinterleavedDepthBufferNormalisedOffset[1] = ((float)bufferSizeInfo->deinterleavedDepthBufferYOffset) / ((float)bufferSizeInfo->deinterleavedDepthBufferHeight);

	if (!settings->generateNormals)
	{
		consts->NormalsUnpackMul = 2.0f;  // inputs->NormalsUnpackMul;
		consts->NormalsUnpackAdd = -1.0f; // inputs->NormalsUnpackAdd;
	}
	else
	{
		consts->NormalsUnpackMul = 2.0f;
		consts->NormalsUnpackAdd = -1.0f;
	}
}

void FFX_CACAO_UpdatePerPassConstants(FFX_CACAO_Constants* consts, const FFX_CACAO_Settings* settings, const FFX_CACAO_BufferSizeInfo* bufferSizeInfo, int pass)
{
	consts->PerPassFullResUVOffset[0] = ((float)(pass % 2)) / (float)bufferSizeInfo->ssaoBufferWidth;
	consts->PerPassFullResUVOffset[1] = ((float)(pass / 2)) / (float)bufferSizeInfo->ssaoBufferHeight;

	consts->PassIndex = pass;

	//float additionalAngleOffset = settings->temporalSupersamplingAngleOffset;  // if using temporal supersampling approach (like "Progressive Rendering Using Multi-frame Sampling" from GPU Pro 7, etc.)
	//float additionalRadiusScale = settings->temporalSupersamplingRadiusOffset; // if using temporal supersampling approach (like "Progressive Rendering Using Multi-frame Sampling" from GPU Pro 7, etc.)
	(void)settings;

	const int subPassCount = 5;
	for (int subPass = 0; subPass < subPassCount; subPass++)
	{
		int a = pass;
		int b = subPass;

		int spmap[5]{ 0, 1, 4, 3, 2 };
		b = spmap[subPass];

		float ca, sa;
		float angle0 = ((float)a + (float)b / (float)subPassCount) * (3.1415926535897932384626433832795f) * 0.5f;

		ca = FFX_CACAO_COS(angle0);
		sa = FFX_CACAO_SIN(angle0);

		float scale = 1.0f + (a - 1.5f + (b - (subPassCount - 1.0f) * 0.5f) / (float)subPassCount) * 0.07f;

		consts->PatternRotScaleMatrices[subPass][0] = scale * ca;
		consts->PatternRotScaleMatrices[subPass][1] = scale * -sa;
		consts->PatternRotScaleMatrices[subPass][2] = -scale * sa;
		consts->PatternRotScaleMatrices[subPass][3] = -scale * ca;
	}
}
