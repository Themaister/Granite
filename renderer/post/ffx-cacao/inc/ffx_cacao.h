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

/*! \file */

#pragma once

#include <stdint.h>

typedef uint8_t FFX_CACAO_Bool;
static const FFX_CACAO_Bool FFX_CACAO_TRUE  = 1;
static const FFX_CACAO_Bool FFX_CACAO_FALSE = 0;

/**
	The quality levels that FidelityFX CACAO can generate SSAO at. This affects the number of samples taken for generating SSAO.
*/
typedef enum FFX_CACAO_Quality {
	FFX_CACAO_QUALITY_LOWEST  = 0,
	FFX_CACAO_QUALITY_LOW     = 1,
	FFX_CACAO_QUALITY_MEDIUM  = 2,
	FFX_CACAO_QUALITY_HIGH    = 3,
	FFX_CACAO_QUALITY_HIGHEST = 4,
} FFX_CACAO_Quality;

/**
	A structure representing a 4x4 matrix of floats. The matrix is stored in row major order in memory.
*/
typedef struct FFX_CACAO_Matrix4x4 {
	float elements[4][4];
} FFX_CACAO_Matrix4x4;

/**
	A structure for the settings used by FidelityFX CACAO. These settings may be updated with each draw call.
*/
typedef struct FFX_CACAO_Settings {
	float           radius;                            ///< [0.0,  ~ ] World (view) space size of the occlusion sphere.
	float           shadowMultiplier;                  ///< [0.0, 5.0] Effect strength linear multiplier.
	float           shadowPower;                       ///< [0.5, 5.0] Effect strength pow modifier.
	float           shadowClamp;                       ///< [0.0, 1.0] Effect max limit (applied after multiplier but before blur).
	float           horizonAngleThreshold;             ///< [0.0, 0.2] Limits self-shadowing (makes the sampling area less of a hemisphere, more of a spherical cone, to avoid self-shadowing and various artifacts due to low tessellation and depth buffer imprecision, etc.).
	float           fadeOutFrom;                       ///< [0.0,  ~ ] Distance to start fading out the effect.
	float           fadeOutTo;                         ///< [0.0,  ~ ] Distance at which the effect is faded out.
	FFX_CACAO_Quality qualityLevel;                      ///<            Effect quality, affects number of taps etc.
	float           adaptiveQualityLimit;              ///< [0.0, 1.0] (only for quality level FFX_CACAO_QUALITY_HIGHEST).
	uint32_t        blurPassCount;                     ///< [  0,   8] Number of edge-sensitive smart blur passes to apply.
	float           sharpness;                         ///< [0.0, 1.0] (How much to bleed over edges; 1: not at all, 0.5: half-half; 0.0: completely ignore edges).
	float           temporalSupersamplingAngleOffset;  ///< [0.0,  PI] Used to rotate sampling kernel; If using temporal AA / supersampling, suggested to rotate by ( (frame%3)/3.0*PI ) or similar. Kernel is already symmetrical, which is why we use PI and not 2*PI.
	float           temporalSupersamplingRadiusOffset; ///< [0.0, 2.0] Used to scale sampling kernel; If using temporal AA / supersampling, suggested to scale by ( 1.0f + (((frame%3)-1.0)/3.0)*0.1 ) or similar.
	float           detailShadowStrength;              ///< [0.0, 5.0] Used for high-res detail AO using neighboring depth pixels: adds a lot of detail but also reduces temporal stability (adds aliasing).
	FFX_CACAO_Bool    generateNormals;                   ///< This option should be set to FFX_CACAO_TRUE if FidelityFX-CACAO should reconstruct a normal buffer from the depth buffer. It is required to be FFX_CACAO_TRUE if no normal buffer is provided.
	float           bilateralSigmaSquared;             ///< [0.0,  ~ ] Sigma squared value for use in bilateral upsampler giving Gaussian blur term. Should be greater than 0.0.
	float           bilateralSimilarityDistanceSigma;  ///< [0.0,  ~ ] Sigma squared value for use in bilateral upsampler giving similarity weighting for neighbouring pixels. Should be greater than 0.0.
} FFX_CACAO_Settings;

static const FFX_CACAO_Settings FFX_CACAO_DEFAULT_SETTINGS = {
	/* radius                            */ 1.2f,
	/* shadowMultiplier                  */ 1.0f,
	/* shadowPower                       */ 1.50f,
	/* shadowClamp                       */ 0.98f,
	/* horizonAngleThreshold             */ 0.06f,
	/* fadeOutFrom                       */ 50.0f,
	/* fadeOutTo                         */ 300.0f,
	/* qualityLevel                      */ FFX_CACAO_QUALITY_HIGHEST,
	/* adaptiveQualityLimit              */ 0.45f,
	/* blurPassCount                     */ 2,
	/* sharpness                         */ 0.98f,
	/* temporalSupersamplingAngleOffset  */ 0.0f,
	/* temporalSupersamplingRadiusOffset */ 0.0f,
	/* detailShadowStrength              */ 0.5f,
	/* generateNormals                   */ FFX_CACAO_FALSE,
	/* bilateralSigmaSquared             */ 5.0f,
	/* bilateralSimilarityDistanceSigma  */ 0.01f,
};

/**
	A C++ structure for the constant buffer used by FidelityFX CACAO.
*/
typedef struct FFX_CACAO_Constants {
	float                   DepthUnpackConsts[2];
	float                   CameraTanHalfFOV[2];

	float                   NDCToViewMul[2];
	float                   NDCToViewAdd[2];

	float                   DepthBufferUVToViewMul[2];
	float                   DepthBufferUVToViewAdd[2];

	float                   EffectRadius;
	float                   EffectShadowStrength;
	float                   EffectShadowPow;
	float                   EffectShadowClamp;

	float                   EffectFadeOutMul;
	float                   EffectFadeOutAdd;
	float                   EffectHorizonAngleThreshold;
	float                   EffectSamplingRadiusNearLimitRec;

	float                   DepthPrecisionOffsetMod;
	float                   NegRecEffectRadius;
	float                   LoadCounterAvgDiv;
	float                   AdaptiveSampleCountLimit;

	float                   InvSharpness;
	int                     PassIndex;
	float                   BilateralSigmaSquared;
	float                   BilateralSimilarityDistanceSigma;

	float                   PatternRotScaleMatrices[5][4];

	float                   NormalsUnpackMul;
	float                   NormalsUnpackAdd;
	float                   DetailAOStrength;
	float                   Dummy0;

	float                   SSAOBufferDimensions[2];
	float                   SSAOBufferInverseDimensions[2];

	float                   DepthBufferDimensions[2];
	float                   DepthBufferInverseDimensions[2];

	int                     DepthBufferOffset[2];
	float                   PerPassFullResUVOffset[2];

	float                   InputOutputBufferDimensions[2];
	float                   InputOutputBufferInverseDimensions[2];

	float                   ImportanceMapDimensions[2];
	float                   ImportanceMapInverseDimensions[2];

	float                   DeinterleavedDepthBufferDimensions[2];
	float                   DeinterleavedDepthBufferInverseDimensions[2];

	float                   DeinterleavedDepthBufferOffset[2];
	float                   DeinterleavedDepthBufferNormalisedOffset[2];

	FFX_CACAO_Matrix4x4       NormalsWorldToViewspaceMatrix;
} FFX_CACAO_Constants;

/**
	A structure containing sizes of each of the buffers used by FidelityFX CACAO.
 */
typedef struct FFX_CACAO_BufferSizeInfo {
	uint32_t inputOutputBufferWidth;
	uint32_t inputOutputBufferHeight;

	uint32_t ssaoBufferWidth;
	uint32_t ssaoBufferHeight;

	uint32_t depthBufferXOffset;
	uint32_t depthBufferYOffset;

	uint32_t depthBufferWidth;
	uint32_t depthBufferHeight;

	uint32_t deinterleavedDepthBufferXOffset;
	uint32_t deinterleavedDepthBufferYOffset;

	uint32_t deinterleavedDepthBufferWidth;
	uint32_t deinterleavedDepthBufferHeight;

	uint32_t importanceMapWidth;
	uint32_t importanceMapHeight;

	uint32_t downsampledSsaoBufferWidth;
	uint32_t downsampledSsaoBufferHeight;
} FFX_CACAO_BufferSizeInfo;

#ifdef __cplusplus
extern "C"
{
#endif

	/**
		Update buffer size info for resolution width x height.

		\code{.cpp}
		FFX_CACAO_BufferSizeInfo bufferSizeInfo = {};
		FFX_CACAO_UpdateBufferSizeInfo(width, height, useDownsampledSsao, &bufferSizeInfo);
		\endcode

		\param width Screen width.
		\param height Screen height.
		\param useDownsampledSsao Whether FFX CACAO should use downsampling.
	*/
	void FFX_CACAO_UpdateBufferSizeInfo(uint32_t width, uint32_t height, FFX_CACAO_Bool useDownsampledSsao, FFX_CACAO_BufferSizeInfo* bsi);

	/**
		Update the contents of the FFX CACAO constant buffer (an FFX_CACAO_Constants struct). Note, this function does not update
		per pass constants.

		\code{.cpp}
		FFX_CACAO_Matrix4x4 proj = ...;                // projection matrix for the frame
		FFX_CACAO_Matrix4x4 normalsToView = ...;       // normals world space to view space matrix for the frame
		FFX_CACAO_Settings settings = ...;             // settings
		FFX_CACAO_BufferSizeInfo bufferSizeInfo = ...; // buffer size info

		FFX_CACAO_Constants constants = {};
		FFX_CACAO_UpdateConstants(&constants, &settings, &bufferSizeInfo, &proj, &normalsToView);
		\endcode

		\param consts FFX_CACAO_Constants constant buffer.
		\param settings FFX_CACAO_Settings settings.
		\param bufferSizeInfo FFX_CACAO_BufferSizeInfo buffer size info.
		\param proj Projection matrix for the frame.
		\param normalsToView Normals world space to view space matrix for the frame.
	*/
	void FFX_CACAO_UpdateConstants(FFX_CACAO_Constants* consts, const FFX_CACAO_Settings* settings, const FFX_CACAO_BufferSizeInfo* bufferSizeInfo, const FFX_CACAO_Matrix4x4* proj, const FFX_CACAO_Matrix4x4* normalsToView);

	/**
		Update the contents of the FFX CACAO constant buffer (an FFX_CACAO_Constants struct) with per pass constants.
		FFX CACAO runs 4 passes which use different constants. It is recommended to have four separate FFX_CACAO_Constants structs
		each filled with constants for each of the 4 passes.

		\code{.cpp}
		FFX_CACAO_Settings settings = ...;             // settings
		FFX_CACAO_BufferSizeInfo bufferSizeInfo = ...; // buffer size info

		FFX_CACAO_Constants perPassConstants[4] = {};

		for (int i = 0; i < 4; ++i) {
			FFX_CACAO_UpdatePerPassConstants(&perPassConstants[i], &settings, &bufferSizeInfo, i);
		}
		\endcode

		\param consts FFX_CACAO_Constants constants buffer.
		\param settings FFX_CACAO_Settings settings.
		\param bufferSizeInfo FFX_CACAO_BufferSizeInfo buffer size info.
		\param pass pass number.
	*/
	void FFX_CACAO_UpdatePerPassConstants(FFX_CACAO_Constants* consts, const FFX_CACAO_Settings* settings, const FFX_CACAO_BufferSizeInfo* bufferSizeInfo, int pass);

#ifdef __cplusplus
}
#endif
