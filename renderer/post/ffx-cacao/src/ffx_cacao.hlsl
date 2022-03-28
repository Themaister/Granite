// Modifications Copyright  2021. Advanced Micro Devices, Inc. All Rights Reserved.

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


#include "ffx_cacao_defines.h"
#include "ffx_cacao_bindings.hlsl"

static const float4 g_FFX_CACAO_samplePatternMain[] =
{
	 0.78488064,  0.56661671,  1.500000, -0.126083,     0.26022232, -0.29575172,  1.500000, -1.064030,     0.10459357,  0.08372527,  1.110000, -2.730563,    -0.68286800,  0.04963045,  1.090000, -0.498827,
	-0.13570161, -0.64190155,  1.250000, -0.532765,    -0.26193795, -0.08205118,  0.670000, -1.783245,    -0.61177456,  0.66664219,  0.710000, -0.044234,     0.43675563,  0.25119025,  0.610000, -1.167283,
	 0.07884444,  0.86618668,  0.640000, -0.459002,    -0.12790935, -0.29869005,  0.600000, -1.729424,    -0.04031125,  0.02413622,  0.600000, -4.792042,     0.16201244, -0.52851415,  0.790000, -1.067055,
	-0.70991218,  0.47301072,  0.640000, -0.335236,     0.03277707, -0.22349690,  0.600000, -1.982384,     0.68921727,  0.36800742,  0.630000, -0.266718,     0.29251814,  0.37775412,  0.610000, -1.422520,
	-0.12224089,  0.96582592,  0.600000, -0.426142,     0.11071457, -0.16131058,  0.600000, -2.165947,     0.46562141, -0.59747696,  0.600000, -0.189760,    -0.51548797,  0.11804193,  0.600000, -1.246800,
	 0.89141309, -0.42090443,  0.600000,  0.028192,    -0.32402530, -0.01591529,  0.600000, -1.543018,     0.60771245,  0.41635221,  0.600000, -0.605411,     0.02379565, -0.08239821,  0.600000, -3.809046,
	 0.48951152, -0.23657045,  0.600000, -1.189011,    -0.17611565, -0.81696892,  0.600000, -0.513724,    -0.33930185, -0.20732205,  0.600000, -1.698047,    -0.91974425,  0.05403209,  0.600000,  0.062246,
	-0.15064627, -0.14949332,  0.600000, -1.896062,     0.53180975, -0.35210401,  0.600000, -0.758838,     0.41487166,  0.81442589,  0.600000, -0.505648,    -0.24106961, -0.32721516,  0.600000, -1.665244
};

#define FFX_CACAO_MAX_TAPS (32)
#define FFX_CACAO_ADAPTIVE_TAP_BASE_COUNT (5)
#define FFX_CACAO_ADAPTIVE_TAP_FLEXIBLE_COUNT (FFX_CACAO_MAX_TAPS - FFX_CACAO_ADAPTIVE_TAP_BASE_COUNT)

// these values can be changed (up to FFX_CACAO_MAX_TAPS) with no changes required elsewhere; values for 4th and 5th preset are ignored but array needed to avoid compilation errors
// the actual number of texture samples is two times this value (each "tap" has two symmetrical depth texture samples)
static const uint g_FFX_CACAO_numTaps[5] = { 3, 5, 12, 0, 0 };


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Optional parts that can be enabled for a required quality preset level and above (0 == Low, 1 == Medium, 2 == High, 3 == Highest/Adaptive, 4 == reference/unused )
// Each has its own cost. To disable just set to 5 or above.
//
// (experimental) tilts the disk (although only half of the samples!) towards surface normal; this helps with effect uniformity between objects but reduces effect distance and has other side-effects
#define FFX_CACAO_TILT_SAMPLES_ENABLE_AT_QUALITY_PRESET                      (99)        // to disable simply set to 99 or similar
#define FFX_CACAO_TILT_SAMPLES_AMOUNT                                        (0.4)
//
#define FFX_CACAO_HALOING_REDUCTION_ENABLE_AT_QUALITY_PRESET                 (1)         // to disable simply set to 99 or similar
#define FFX_CACAO_HALOING_REDUCTION_AMOUNT                                   (0.6)       // values from 0.0 - 1.0, 1.0 means max weighting (will cause artifacts, 0.8 is more reasonable)
//
#define FFX_CACAO_NORMAL_BASED_EDGES_ENABLE_AT_QUALITY_PRESET                (2) //2        // to disable simply set to 99 or similar
#define FFX_CACAO_NORMAL_BASED_EDGES_DOT_THRESHOLD                           (0.5)       // use 0-0.1 for super-sharp normal-based edges
//
#define FFX_CACAO_DETAIL_AO_ENABLE_AT_QUALITY_PRESET                         (1) //1         // whether to use DetailAOStrength; to disable simply set to 99 or similar
//
#define FFX_CACAO_DEPTH_MIPS_ENABLE_AT_QUALITY_PRESET                        (2)         // !!warning!! the MIP generation on the C++ side will be enabled on quality preset 2 regardless of this value, so if changing here, change the C++ side too
#define FFX_CACAO_DEPTH_MIPS_GLOBAL_OFFSET                                   (-4.3)      // best noise/quality/performance tradeoff, found empirically
//
// !!warning!! the edge handling is hard-coded to 'disabled' on quality level 0, and enabled above, on the C++ side; while toggling it here will work for
// testing purposes, it will not yield performance gains (or correct results)
#define FFX_CACAO_DEPTH_BASED_EDGES_ENABLE_AT_QUALITY_PRESET                 (1)
//
#define FFX_CACAO_REDUCE_RADIUS_NEAR_SCREEN_BORDER_ENABLE_AT_QUALITY_PRESET  (99)        // 99 means disabled; only helpful if artifacts at the edges caused by lack of out of screen depth data are not acceptable with the depth sampler in either clamp or mirror modes
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// packing/unpacking for edges; 2 bits per edge mean 4 gradient values (0, 0.33, 0.66, 1) for smoother transitions!
float FFX_CACAO_PackEdges(float4 edgesLRTB)
{
	edgesLRTB = round(saturate(edgesLRTB) * 3.05);
	return dot(edgesLRTB, float4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

float4 FFX_CACAO_UnpackEdges(float _packedVal)
{
	uint packedVal = (uint)(_packedVal * 255.5);
	float4 edgesLRTB;
	edgesLRTB.x = float((packedVal >> 6) & 0x03) / 3.0;          // there's really no need for mask (as it's an 8 bit input) but I'll leave it in so it doesn't cause any trouble in the future
	edgesLRTB.y = float((packedVal >> 4) & 0x03) / 3.0;
	edgesLRTB.z = float((packedVal >> 2) & 0x03) / 3.0;
	edgesLRTB.w = float((packedVal >> 0) & 0x03) / 3.0;

	return saturate(edgesLRTB + g_FFX_CACAO_Consts.InvSharpness);
}

float FFX_CACAO_ScreenSpaceToViewSpaceDepth(float screenDepth)
{
	float depthLinearizeMul = g_FFX_CACAO_Consts.DepthUnpackConsts.x;
	float depthLinearizeAdd = g_FFX_CACAO_Consts.DepthUnpackConsts.y;

	return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
}

float4 FFX_CACAO_ScreenSpaceToViewSpaceDepth(float4 screenDepth)
{
	float depthLinearizeMul = g_FFX_CACAO_Consts.DepthUnpackConsts.x;
	float depthLinearizeAdd = g_FFX_CACAO_Consts.DepthUnpackConsts.y;

	return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
}

float4 FFX_CACAO_CalculateEdges(const float centerZ, const float leftZ, const float rightZ, const float topZ, const float bottomZ)
{
	// slope-sensitive depth-based edge detection
	float4 edgesLRTB = float4(leftZ, rightZ, topZ, bottomZ) - centerZ;
	float4 edgesLRTBSlopeAdjusted = edgesLRTB + edgesLRTB.yxwz;
	edgesLRTB = min(abs(edgesLRTB), abs(edgesLRTBSlopeAdjusted));
	return saturate((1.3 - edgesLRTB / (centerZ * 0.040)));
}

float3 FFX_CACAO_NDCToViewSpace(float2 pos, float viewspaceDepth)
{
	float3 ret;

	ret.xy = (g_FFX_CACAO_Consts.NDCToViewMul * pos.xy + g_FFX_CACAO_Consts.NDCToViewAdd) * viewspaceDepth;

	ret.z = viewspaceDepth;

	return ret;
}

float3 FFX_CACAO_DepthBufferUVToViewSpace(float2 pos, float viewspaceDepth)
{
	float3 ret;
	ret.xy = (g_FFX_CACAO_Consts.DepthBufferUVToViewMul * pos.xy + g_FFX_CACAO_Consts.DepthBufferUVToViewAdd) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

float3 FFX_CACAO_CalculateNormal(const float4 edgesLRTB, float3 pixCenterPos, float3 pixLPos, float3 pixRPos, float3 pixTPos, float3 pixBPos)
{
	// Get this pixel's viewspace normal
	float4 acceptedNormals = float4(edgesLRTB.x*edgesLRTB.z, edgesLRTB.z*edgesLRTB.y, edgesLRTB.y*edgesLRTB.w, edgesLRTB.w*edgesLRTB.x);

	pixLPos = normalize(pixLPos - pixCenterPos);
	pixRPos = normalize(pixRPos - pixCenterPos);
	pixTPos = normalize(pixTPos - pixCenterPos);
	pixBPos = normalize(pixBPos - pixCenterPos);

	float3 pixelNormal = float3(0, 0, -0.0005);
	pixelNormal += (acceptedNormals.x) * cross(pixLPos, pixTPos);
	pixelNormal += (acceptedNormals.y) * cross(pixTPos, pixRPos);
	pixelNormal += (acceptedNormals.z) * cross(pixRPos, pixBPos);
	pixelNormal += (acceptedNormals.w) * cross(pixBPos, pixLPos);
	pixelNormal = normalize(pixelNormal);

	return pixelNormal;
}

// =============================================================================
// Clear Load Counter

[numthreads(1, 1, 1)]
void FFX_CACAO_ClearLoadCounter()
{
	FFX_CACAO_ClearLoadCounter_SetLoadCounter(0);
}

// =============================================================================
// Edge Sensitive Blur

uint FFX_CACAO_PackFloat16(min16float2 v)
{
	uint2 p = f32tof16(float2(v));
	return p.x | (p.y << 16);
}

min16float2 FFX_CACAO_UnpackFloat16(uint a)
{
	float2 tmp = f16tof32(uint2(a & 0xFFFF, a >> 16));
	return min16float2(tmp);
}

// all in one, SIMD in yo SIMD dawg, shader
#define FFX_CACAO_TILE_WIDTH  4
#define FFX_CACAO_TILE_HEIGHT 3
#define FFX_CACAO_HALF_TILE_WIDTH (FFX_CACAO_TILE_WIDTH / 2)
#define FFX_CACAO_QUARTER_TILE_WIDTH (FFX_CACAO_TILE_WIDTH / 4)

#define FFX_CACAO_ARRAY_WIDTH  (FFX_CACAO_HALF_TILE_WIDTH  * FFX_CACAO_BLUR_WIDTH  + 2)
#define FFX_CACAO_ARRAY_HEIGHT (FFX_CACAO_TILE_HEIGHT * FFX_CACAO_BLUR_HEIGHT + 2)

#define FFX_CACAO_ITERS 4

groupshared uint s_FFX_CACAO_BlurF16Front_4[FFX_CACAO_ARRAY_WIDTH][FFX_CACAO_ARRAY_HEIGHT];
groupshared uint s_FFX_CACAO_BlurF16Back_4[FFX_CACAO_ARRAY_WIDTH][FFX_CACAO_ARRAY_HEIGHT];

struct FFX_CACAO_Edges_4
{
	min16float4 left;
	min16float4 right;
	min16float4 top;
	min16float4 bottom;
};

FFX_CACAO_Edges_4 FFX_CACAO_UnpackEdgesFloat16_4(min16float4 _packedVal)
{
	uint4 packedVal = (uint4)(_packedVal * 255.5);
	FFX_CACAO_Edges_4 result;
	result.left   = min16float4(saturate(min16float4((packedVal >> 6) & 0x03) / 3.0 + g_FFX_CACAO_Consts.InvSharpness));
	result.right  = min16float4(saturate(min16float4((packedVal >> 4) & 0x03) / 3.0 + g_FFX_CACAO_Consts.InvSharpness));
	result.top    = min16float4(saturate(min16float4((packedVal >> 2) & 0x03) / 3.0 + g_FFX_CACAO_Consts.InvSharpness));
	result.bottom = min16float4(saturate(min16float4((packedVal >> 0) & 0x03) / 3.0 + g_FFX_CACAO_Consts.InvSharpness));

	return result;
}

min16float4 FFX_CACAO_CalcBlurredSampleF16_4(min16float4 packedEdges, min16float4 centre, min16float4 left, min16float4 right, min16float4 top, min16float4 bottom)
{
	min16float4 sum = centre * min16float(0.5f);
	min16float4 weight = min16float4(0.5f, 0.5f, 0.5f, 0.5f);
	FFX_CACAO_Edges_4 edges = FFX_CACAO_UnpackEdgesFloat16_4(packedEdges);

	sum += left * edges.left;
	weight += edges.left;
	sum += right * edges.right;
	weight += edges.right;
	sum += top * edges.top;
	weight += edges.top;
	sum += bottom * edges.bottom;
	weight += edges.bottom;

	return sum / weight;
}

void FFX_CACAO_LDSEdgeSensitiveBlur(const uint blurPasses, const uint2 tid, const uint2 gid)
{
	int2 imageCoord = gid * (int2(FFX_CACAO_TILE_WIDTH * FFX_CACAO_BLUR_WIDTH, FFX_CACAO_TILE_HEIGHT * FFX_CACAO_BLUR_HEIGHT) - (2*blurPasses)) + int2(FFX_CACAO_TILE_WIDTH, FFX_CACAO_TILE_HEIGHT) * tid - blurPasses;
	int2 bufferCoord = int2(FFX_CACAO_HALF_TILE_WIDTH, FFX_CACAO_TILE_HEIGHT) * tid + 1;

	min16float4 packedEdges[FFX_CACAO_QUARTER_TILE_WIDTH][FFX_CACAO_TILE_HEIGHT];
	{
		float2 input[FFX_CACAO_TILE_WIDTH][FFX_CACAO_TILE_HEIGHT];
		int y;
		[unroll]
		for (y = 0; y < FFX_CACAO_TILE_HEIGHT; ++y)
		{
			[unroll]
			for (int x = 0; x < FFX_CACAO_TILE_WIDTH; ++x)
			{
				float2 sampleUV = (float2(imageCoord + int2(x, y)) + 0.5f) * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
				input[x][y] = FFX_CACAO_EdgeSensitiveBlur_SampleInput(sampleUV);
			}
		}
		[unroll]
		for (y = 0; y < FFX_CACAO_TILE_HEIGHT; ++y)
		{
			[unroll]
			for (int x = 0; x < FFX_CACAO_QUARTER_TILE_WIDTH; ++x)
			{
				min16float2 ssaoVals = min16float2(input[4 * x + 0][y].x, input[4 * x + 1][y].x);
				s_FFX_CACAO_BlurF16Front_4[bufferCoord.x + 2*x + 0][bufferCoord.y + y] = FFX_CACAO_PackFloat16(ssaoVals);
				ssaoVals = min16float2(input[4 * x + 2][y].x, input[4 * x + 3][y].x);
				s_FFX_CACAO_BlurF16Front_4[bufferCoord.x + 2*x + 1][bufferCoord.y + y] = FFX_CACAO_PackFloat16(ssaoVals);
				packedEdges[x][y] = min16float4(input[4 * x + 0][y].y, input[4 * x + 1][y].y, input[4 * x + 2][y].y, input[4 * x + 3][y].y);
			}
		}
	}

	GroupMemoryBarrierWithGroupSync();

	[unroll]
	for (uint i = 0; i < (blurPasses + 1) / 2; ++i)
	{
		[unroll]
		for (int y = 0; y < FFX_CACAO_TILE_HEIGHT; ++y)
		{
			[unroll]
			for (int x = 0; x < FFX_CACAO_QUARTER_TILE_WIDTH; ++x)
			{
				int2 c = bufferCoord + int2(2*x, y);
				min16float4 centre = min16float4(FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Front_4[c.x + 0][c.y + 0]), FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Front_4[c.x + 1][c.y + 0]));
				min16float4 top    = min16float4(FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Front_4[c.x + 0][c.y - 1]), FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Front_4[c.x + 1][c.y - 1]));
				min16float4 bottom = min16float4(FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Front_4[c.x + 0][c.y + 1]), FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Front_4[c.x + 1][c.y + 1]));

				min16float2 tmp = FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Front_4[c.x - 1][c.y + 0]);
				min16float4 left = min16float4(tmp.y, centre.xyz);
				tmp = FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Front_4[c.x + 2][c.y + 0]);
				min16float4 right = min16float4(centre.yzw, tmp.x);

				min16float4 tmp_4 = FFX_CACAO_CalcBlurredSampleF16_4(packedEdges[x][y], centre, left, right, top, bottom);
				s_FFX_CACAO_BlurF16Back_4[c.x + 0][c.y] = FFX_CACAO_PackFloat16(tmp_4.xy);
				s_FFX_CACAO_BlurF16Back_4[c.x + 1][c.y] = FFX_CACAO_PackFloat16(tmp_4.zw);
			}
		}
		GroupMemoryBarrierWithGroupSync();

		if (2 * i + 1 < blurPasses)
		{
			[unroll]
			for (int y = 0; y < FFX_CACAO_TILE_HEIGHT; ++y)
			{
				[unroll]
				for (int x = 0; x < FFX_CACAO_QUARTER_TILE_WIDTH; ++x)
				{
					int2 c = bufferCoord + int2(2 * x, y);
					min16float4 centre = min16float4(FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Back_4[c.x + 0][c.y + 0]), FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Back_4[c.x + 1][c.y + 0]));
					min16float4 top    = min16float4(FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Back_4[c.x + 0][c.y - 1]), FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Back_4[c.x + 1][c.y - 1]));
					min16float4 bottom = min16float4(FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Back_4[c.x + 0][c.y + 1]), FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Back_4[c.x + 1][c.y + 1]));

					min16float2 tmp = FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Back_4[c.x - 1][c.y + 0]);
					min16float4 left = min16float4(tmp.y, centre.xyz);
					tmp = FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Back_4[c.x + 2][c.y + 0]);
					min16float4 right = min16float4(centre.yzw, tmp.x);

					min16float4 tmp_4 = FFX_CACAO_CalcBlurredSampleF16_4(packedEdges[x][y], centre, left, right, top, bottom);
					s_FFX_CACAO_BlurF16Front_4[c.x + 0][c.y] = FFX_CACAO_PackFloat16(tmp_4.xy);
					s_FFX_CACAO_BlurF16Front_4[c.x + 1][c.y] = FFX_CACAO_PackFloat16(tmp_4.zw);
				}
			}
			GroupMemoryBarrierWithGroupSync();
		}
	}

	[unroll]
	for (uint y = 0; y < FFX_CACAO_TILE_HEIGHT; ++y)
	{
		uint outputY = FFX_CACAO_TILE_HEIGHT * tid.y + y;
		if (blurPasses <= outputY && outputY < FFX_CACAO_TILE_HEIGHT * FFX_CACAO_BLUR_HEIGHT - blurPasses)
		{
			[unroll]
			for (int x = 0; x < FFX_CACAO_QUARTER_TILE_WIDTH; ++x)
			{
				uint outputX = FFX_CACAO_TILE_WIDTH * tid.x + 4 * x;

				min16float4 ssaoVal;
				if (blurPasses % 2 == 0)
				{
					ssaoVal = min16float4(FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Front_4[bufferCoord.x + x][bufferCoord.y + y]), FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Front_4[bufferCoord.x + x + 1][bufferCoord.y + y]));
				}
				else
				{
					ssaoVal = min16float4(FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Back_4[bufferCoord.x + x][bufferCoord.y + y]), FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BlurF16Back_4[bufferCoord.x + x + 1][bufferCoord.y + y]));
				}

				if (blurPasses <= outputX && outputX < FFX_CACAO_TILE_WIDTH * FFX_CACAO_BLUR_WIDTH - blurPasses)
				{
					FFX_CACAO_EdgeSensitiveBlur_StoreOutput(imageCoord + int2(4 * x, y), float2(ssaoVal.x, packedEdges[x][y].x));
				}
				outputX += 1;
				if (blurPasses <= outputX && outputX < FFX_CACAO_TILE_WIDTH * FFX_CACAO_BLUR_WIDTH - blurPasses)
				{
					FFX_CACAO_EdgeSensitiveBlur_StoreOutput(imageCoord + int2(4 * x + 1, y), float2(ssaoVal.y, packedEdges[x][y].y));
				}
				outputX += 1;
				if (blurPasses <= outputX && outputX < FFX_CACAO_TILE_WIDTH * FFX_CACAO_BLUR_WIDTH - blurPasses)
				{
					FFX_CACAO_EdgeSensitiveBlur_StoreOutput(imageCoord + int2(4 * x + 2, y), float2(ssaoVal.z, packedEdges[x][y].z));
				}
				outputX += 1;
				if (blurPasses <= outputX && outputX < FFX_CACAO_TILE_WIDTH * FFX_CACAO_BLUR_WIDTH - blurPasses)
				{
					FFX_CACAO_EdgeSensitiveBlur_StoreOutput(imageCoord + int2(4 * x + 3, y), float2(ssaoVal.w, packedEdges[x][y].w));
				}
			}
		}
	}
}

[numthreads(FFX_CACAO_BLUR_WIDTH, FFX_CACAO_BLUR_HEIGHT, 1)]
void FFX_CACAO_EdgeSensitiveBlur1(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	FFX_CACAO_LDSEdgeSensitiveBlur(1, tid, gid);
}

[numthreads(FFX_CACAO_BLUR_WIDTH, FFX_CACAO_BLUR_HEIGHT, 1)]
void FFX_CACAO_EdgeSensitiveBlur2(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	FFX_CACAO_LDSEdgeSensitiveBlur(2, tid, gid);
}

[numthreads(FFX_CACAO_BLUR_WIDTH, FFX_CACAO_BLUR_HEIGHT, 1)]
void FFX_CACAO_EdgeSensitiveBlur3(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	FFX_CACAO_LDSEdgeSensitiveBlur(3, tid, gid);
}

[numthreads(FFX_CACAO_BLUR_WIDTH, FFX_CACAO_BLUR_HEIGHT, 1)]
void FFX_CACAO_EdgeSensitiveBlur4(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	FFX_CACAO_LDSEdgeSensitiveBlur(4, tid, gid);
}

[numthreads(FFX_CACAO_BLUR_WIDTH, FFX_CACAO_BLUR_HEIGHT, 1)]
void FFX_CACAO_EdgeSensitiveBlur5(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	FFX_CACAO_LDSEdgeSensitiveBlur(5, tid, gid);
}

[numthreads(FFX_CACAO_BLUR_WIDTH, FFX_CACAO_BLUR_HEIGHT, 1)]
void FFX_CACAO_EdgeSensitiveBlur6(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	FFX_CACAO_LDSEdgeSensitiveBlur(6, tid, gid);
}

[numthreads(FFX_CACAO_BLUR_WIDTH, FFX_CACAO_BLUR_HEIGHT, 1)]
void FFX_CACAO_EdgeSensitiveBlur7(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	FFX_CACAO_LDSEdgeSensitiveBlur(7, tid, gid);
}


[numthreads(FFX_CACAO_BLUR_WIDTH, FFX_CACAO_BLUR_HEIGHT, 1)]
void FFX_CACAO_EdgeSensitiveBlur8(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	FFX_CACAO_LDSEdgeSensitiveBlur(8, tid, gid);
}


#undef FFX_CACAO_TILE_WIDTH
#undef FFX_CACAO_TILE_HEIGHT
#undef FFX_CACAO_HALF_TILE_WIDTH
#undef FFX_CACAO_QUARTER_TILE_WIDTH
#undef FFX_CACAO_ARRAY_WIDTH
#undef FFX_CACAO_ARRAY_HEIGHT
#undef FFX_CACAO_ITERS

// =======================================================================================================
// SSAO Generation

// calculate effect radius and fit our screen sampling pattern inside it
void FFX_CACAO_CalculateRadiusParameters(const float pixCenterLength, const float2 pixelDirRBViewspaceSizeAtCenterZ, out float pixLookupRadiusMod, out float effectRadius, out float falloffCalcMulSq)
{
	effectRadius = g_FFX_CACAO_Consts.EffectRadius;

	// leaving this out for performance reasons: use something similar if radius needs to scale based on distance
	//effectRadius *= pow( pixCenterLength, g_FFX_CACAO_Consts.RadiusDistanceScalingFunctionPow);

	// when too close, on-screen sampling disk will grow beyond screen size; limit this to avoid closeup temporal artifacts
	const float tooCloseLimitMod = saturate(pixCenterLength * g_FFX_CACAO_Consts.EffectSamplingRadiusNearLimitRec) * 0.8 + 0.2;

	effectRadius *= tooCloseLimitMod;

	// 0.85 is to reduce the radius to allow for more samples on a slope to still stay within influence
	pixLookupRadiusMod = (0.85 * effectRadius) / pixelDirRBViewspaceSizeAtCenterZ.x;

	// used to calculate falloff (both for AO samples and per-sample weights)
	falloffCalcMulSq = -1.0f / (effectRadius*effectRadius);
}

// all vectors in viewspace
float FFX_CACAO_CalculatePixelObscurance(float3 pixelNormal, float3 hitDelta, float falloffCalcMulSq)
{
	float lengthSq = dot(hitDelta, hitDelta);
	float NdotD = dot(pixelNormal, hitDelta) / sqrt(lengthSq);

	float falloffMult = max(0.0, lengthSq * falloffCalcMulSq + 1.0);

	return max(0, NdotD - g_FFX_CACAO_Consts.EffectHorizonAngleThreshold) * falloffMult;
}

void FFX_CACAO_SSAOTapInner(const int qualityLevel, inout float obscuranceSum, inout float weightSum, const float2 samplingUV, const float mipLevel, const float3 pixCenterPos, const float3 negViewspaceDir, float3 pixelNormal, const float falloffCalcMulSq, const float weightMod, const int dbgTapIndex)
{
	// get depth at sample
	float viewspaceSampleZ = FFX_CACAO_SSAOGeneration_SampleViewspaceDepthMip(samplingUV, mipLevel);

	// convert to viewspace
	float3 hitPos = FFX_CACAO_DepthBufferUVToViewSpace(samplingUV.xy, viewspaceSampleZ).xyz;
	float3 hitDelta = hitPos - pixCenterPos;

	float obscurance = FFX_CACAO_CalculatePixelObscurance(pixelNormal, hitDelta, falloffCalcMulSq);
	float weight = 1.0;

	if (qualityLevel >= FFX_CACAO_HALOING_REDUCTION_ENABLE_AT_QUALITY_PRESET)
	{
		//float reduct = max( 0, dot( hitDelta, negViewspaceDir ) );
		float reduct = max(0, -hitDelta.z); // cheaper, less correct version
		reduct = saturate(reduct * g_FFX_CACAO_Consts.NegRecEffectRadius + 2.0); // saturate( 2.0 - reduct / g_FFX_CACAO_Consts.EffectRadius );
		weight = FFX_CACAO_HALOING_REDUCTION_AMOUNT * reduct + (1.0 - FFX_CACAO_HALOING_REDUCTION_AMOUNT);
	}
	weight *= weightMod;
	obscuranceSum += obscurance * weight;
	weightSum += weight;
}

void FFX_CACAO_SSAOTap(const int qualityLevel, inout float obscuranceSum, inout float weightSum, const int tapIndex, const float2x2 rotScale, const float3 pixCenterPos, const float3 negViewspaceDir, float3 pixelNormal, const float2 normalizedScreenPos, const float2 depthBufferUV, const float mipOffset, const float falloffCalcMulSq, float weightMod, float2 normXY, float normXYLength)
{
	float2  sampleOffset;
	float   samplePow2Len;

	// patterns
	{
		float4 newSample = g_FFX_CACAO_samplePatternMain[tapIndex];
		sampleOffset = mul(rotScale, newSample.xy);
		samplePow2Len = newSample.w;                      // precalculated, same as: samplePow2Len = log2( length( newSample.xy ) );
		weightMod *= newSample.z;
	}

	// snap to pixel center (more correct obscurance math, avoids artifacts)
	sampleOffset = round(sampleOffset);

	// calculate MIP based on the sample distance from the centre, similar to as described
	// in http://graphics.cs.williams.edu/papers/SAOHPG12/.
	float mipLevel = (qualityLevel < FFX_CACAO_DEPTH_MIPS_ENABLE_AT_QUALITY_PRESET) ? (0) : (samplePow2Len + mipOffset);

	float2 samplingUV = sampleOffset * g_FFX_CACAO_Consts.DeinterleavedDepthBufferInverseDimensions + depthBufferUV;

	FFX_CACAO_SSAOTapInner(qualityLevel, obscuranceSum, weightSum, samplingUV, mipLevel, pixCenterPos, negViewspaceDir, pixelNormal, falloffCalcMulSq, weightMod, tapIndex * 2);

	// for the second tap, just use the mirrored offset
	float2 sampleOffsetMirroredUV = -sampleOffset;

	// tilt the second set of samples so that the disk is effectively rotated by the normal
	// effective at removing one set of artifacts, but too expensive for lower quality settings
	if (qualityLevel >= FFX_CACAO_TILT_SAMPLES_ENABLE_AT_QUALITY_PRESET)
	{
		float dotNorm = dot(sampleOffsetMirroredUV, normXY);
		sampleOffsetMirroredUV -= dotNorm * normXYLength * normXY;
		sampleOffsetMirroredUV = round(sampleOffsetMirroredUV);
	}

	// snap to pixel center (more correct obscurance math, avoids artifacts)
	float2 samplingMirroredUV = sampleOffsetMirroredUV * g_FFX_CACAO_Consts.DeinterleavedDepthBufferInverseDimensions + depthBufferUV;

	FFX_CACAO_SSAOTapInner(qualityLevel, obscuranceSum, weightSum, samplingMirroredUV, mipLevel, pixCenterPos, negViewspaceDir, pixelNormal, falloffCalcMulSq, weightMod, tapIndex * 2 + 1);
}

struct FFX_CACAO_SSAOHits
{
	float3 hits[2];
	float weightMod;
};

struct FFX_CACAO_SSAOSampleData
{
	float2 uvOffset;
	float mipLevel;
	float weightMod;
};

FFX_CACAO_SSAOSampleData FFX_CACAO_SSAOGetSampleData(const int qualityLevel, const float2x2 rotScale, const float4 newSample, const float mipOffset)
{
	float2  sampleOffset = mul(rotScale, newSample.xy);
	sampleOffset = round(sampleOffset) * g_FFX_CACAO_Consts.DeinterleavedDepthBufferInverseDimensions;

	float samplePow2Len = newSample.w;
	float mipLevel = (qualityLevel < FFX_CACAO_DEPTH_MIPS_ENABLE_AT_QUALITY_PRESET) ? (0) : (samplePow2Len + mipOffset);

	FFX_CACAO_SSAOSampleData result;

	result.uvOffset = sampleOffset;
	result.mipLevel = mipLevel;
	result.weightMod = newSample.z;

	return result;
}

FFX_CACAO_SSAOHits FFX_CACAO_SSAOGetHits2(FFX_CACAO_SSAOSampleData data, const float2 depthBufferUV)
{
	FFX_CACAO_SSAOHits result;
	result.weightMod = data.weightMod;
	float2 sampleUV = depthBufferUV + data.uvOffset;
	result.hits[0] = float3(sampleUV, FFX_CACAO_SSAOGeneration_SampleViewspaceDepthMip(sampleUV, data.mipLevel));
	sampleUV = depthBufferUV - data.uvOffset;
	result.hits[1] = float3(sampleUV, FFX_CACAO_SSAOGeneration_SampleViewspaceDepthMip(sampleUV, data.mipLevel));
	return result;
}

void FFX_CACAO_SSAOAddHits(const int qualityLevel, const float3 pixCenterPos, const float3 pixelNormal, const float falloffCalcMulSq, inout float weightSum, inout float obscuranceSum, FFX_CACAO_SSAOHits hits)
{
	float weight = hits.weightMod;
	[unroll]
	for (int hitIndex = 0; hitIndex < 2; ++hitIndex)
	{
		float3 hit = hits.hits[hitIndex];
		float3 hitPos = FFX_CACAO_DepthBufferUVToViewSpace(hit.xy, hit.z);
		float3 hitDelta = hitPos - pixCenterPos;

		float obscurance = FFX_CACAO_CalculatePixelObscurance(pixelNormal, hitDelta, falloffCalcMulSq);

		if (qualityLevel >= FFX_CACAO_HALOING_REDUCTION_ENABLE_AT_QUALITY_PRESET)
		{
			//float reduct = max( 0, dot( hitDelta, negViewspaceDir ) );
			float reduct = max(0, -hitDelta.z); // cheaper, less correct version
			reduct = saturate(reduct * g_FFX_CACAO_Consts.NegRecEffectRadius + 2.0); // saturate( 2.0 - reduct / g_FFX_CACAO_Consts.EffectRadius );
			weight = FFX_CACAO_HALOING_REDUCTION_AMOUNT * reduct + (1.0 - FFX_CACAO_HALOING_REDUCTION_AMOUNT);
		}
		obscuranceSum += obscurance * weight;
		weightSum += weight;
	}
}

void FFX_CACAO_GenerateSSAOShadowsInternal(out float outShadowTerm, out float4 outEdges, out float outWeight, const float2 SVPos/*, const float2 normalizedScreenPos*/, uniform int qualityLevel, bool adaptiveBase)
{
	float2 SVPosRounded = trunc(SVPos);
	uint2 SVPosui = uint2(SVPosRounded); //same as uint2( SVPos )

	const int numberOfTaps = (adaptiveBase) ? (FFX_CACAO_ADAPTIVE_TAP_BASE_COUNT) : (g_FFX_CACAO_numTaps[qualityLevel]);
	float pixZ, pixLZ, pixTZ, pixRZ, pixBZ;

	float2 depthBufferUV = (SVPos + 0.5f) * g_FFX_CACAO_Consts.DeinterleavedDepthBufferInverseDimensions + g_FFX_CACAO_Consts.DeinterleavedDepthBufferNormalisedOffset;
	float4 valuesUL = FFX_CACAO_SSAOGeneration_GatherViewspaceDepthOffset(depthBufferUV, int2(-1, -1));
	float4 valuesBR = FFX_CACAO_SSAOGeneration_GatherViewspaceDepthOffset(depthBufferUV, int2(0, 0));

	// get this pixel's viewspace depth
	pixZ = valuesUL.y;

	// get left right top bottom neighbouring pixels for edge detection (gets compiled out on qualityLevel == 0)
	pixLZ = valuesUL.x;
	pixTZ = valuesUL.z;
	pixRZ = valuesBR.z;
	pixBZ = valuesBR.x;

	// float2 normalizedScreenPos = SVPosRounded * g_FFX_CACAO_Consts.Viewport2xPixelSize + g_FFX_CACAO_Consts.Viewport2xPixelSize_x_025;
	float2 normalizedScreenPos = (SVPosRounded + 0.5f) * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
	float3 pixCenterPos = FFX_CACAO_NDCToViewSpace(normalizedScreenPos, pixZ); // g

	// Load this pixel's viewspace normal
	// uint2 fullResCoord = 2 * (SVPosui * 2 + g_FFX_CACAO_Consts.PerPassFullResCoordOffset.xy);
	float3 pixelNormal = FFX_CACAO_SSAOGeneration_GetNormalPass(SVPosui, g_FFX_CACAO_Consts.PassIndex);

	// optimized approximation of:  float2 pixelDirRBViewspaceSizeAtCenterZ = FFX_CACAO_NDCToViewSpace( normalizedScreenPos.xy + g_FFX_CACAO_Consts._ViewportPixelSize.xy, pixCenterPos.z ).xy - pixCenterPos.xy;
	// const float2 pixelDirRBViewspaceSizeAtCenterZ = pixCenterPos.z * g_FFX_CACAO_Consts.NDCToViewMul * g_FFX_CACAO_Consts.Viewport2xPixelSize;
	const float2 pixelDirRBViewspaceSizeAtCenterZ = pixCenterPos.z * g_FFX_CACAO_Consts.NDCToViewMul * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;

	float pixLookupRadiusMod;
	float falloffCalcMulSq;

	// calculate effect radius and fit our screen sampling pattern inside it
	float effectViewspaceRadius;
	FFX_CACAO_CalculateRadiusParameters(length(pixCenterPos), pixelDirRBViewspaceSizeAtCenterZ, pixLookupRadiusMod, effectViewspaceRadius, falloffCalcMulSq);

	// calculate samples rotation/scaling
	float2x2 rotScale;
	{
		// reduce effect radius near the screen edges slightly; ideally, one would render a larger depth buffer (5% on each side) instead
		if (!adaptiveBase && (qualityLevel >= FFX_CACAO_REDUCE_RADIUS_NEAR_SCREEN_BORDER_ENABLE_AT_QUALITY_PRESET))
		{
			float nearScreenBorder = min(min(depthBufferUV.x, 1.0 - depthBufferUV.x), min(depthBufferUV.y, 1.0 - depthBufferUV.y));
			nearScreenBorder = saturate(10.0 * nearScreenBorder + 0.6);
			pixLookupRadiusMod *= nearScreenBorder;
		}

		// load & update pseudo-random rotation matrix
		uint pseudoRandomIndex = uint(SVPosRounded.y * 2 + SVPosRounded.x) % 5;
		float4 rs = g_FFX_CACAO_Consts.PatternRotScaleMatrices[pseudoRandomIndex];
		rotScale = float2x2(rs.x * pixLookupRadiusMod, rs.y * pixLookupRadiusMod, rs.z * pixLookupRadiusMod, rs.w * pixLookupRadiusMod);
	}

	// the main obscurance & sample weight storage
	float obscuranceSum = 0.0;
	float weightSum = 0.0;

	// edge mask for between this and left/right/top/bottom neighbour pixels - not used in quality level 0 so initialize to "no edge" (1 is no edge, 0 is edge)
	float4 edgesLRTB = float4(1.0, 1.0, 1.0, 1.0);

	// Move center pixel slightly towards camera to avoid imprecision artifacts due to using of 16bit depth buffer; a lot smaller offsets needed when using 32bit floats
	pixCenterPos *= g_FFX_CACAO_Consts.DepthPrecisionOffsetMod;

	if (!adaptiveBase && (qualityLevel >= FFX_CACAO_DEPTH_BASED_EDGES_ENABLE_AT_QUALITY_PRESET))
	{
		edgesLRTB = FFX_CACAO_CalculateEdges(pixZ, pixLZ, pixRZ, pixTZ, pixBZ);
	}

	// adds a more high definition sharp effect, which gets blurred out (reuses left/right/top/bottom samples that we used for edge detection)
	if (!adaptiveBase && (qualityLevel >= FFX_CACAO_DETAIL_AO_ENABLE_AT_QUALITY_PRESET))
	{
		// disable in case of quality level 4 (reference)
		if (qualityLevel != 4)
		{
			//approximate neighbouring pixels positions (actually just deltas or "positions - pixCenterPos" )
			float3 viewspaceDirZNormalized = float3(pixCenterPos.xy / pixCenterPos.zz, 1.0);

			// very close approximation of: float3 pixLPos  = FFX_CACAO_NDCToViewSpace( normalizedScreenPos + float2( -g_FFX_CACAO_Consts.HalfViewportPixelSize.x, 0.0 ), pixLZ ).xyz - pixCenterPos.xyz;
			float3 pixLDelta = float3(-pixelDirRBViewspaceSizeAtCenterZ.x, 0.0, 0.0) + viewspaceDirZNormalized * (pixLZ - pixCenterPos.z);
			// very close approximation of: float3 pixRPos  = FFX_CACAO_NDCToViewSpace( normalizedScreenPos + float2( +g_FFX_CACAO_Consts.HalfViewportPixelSize.x, 0.0 ), pixRZ ).xyz - pixCenterPos.xyz;
			float3 pixRDelta = float3(+pixelDirRBViewspaceSizeAtCenterZ.x, 0.0, 0.0) + viewspaceDirZNormalized * (pixRZ - pixCenterPos.z);
			// very close approximation of: float3 pixTPos  = FFX_CACAO_NDCToViewSpace( normalizedScreenPos + float2( 0.0, -g_FFX_CACAO_Consts.HalfViewportPixelSize.y ), pixTZ ).xyz - pixCenterPos.xyz;
			float3 pixTDelta = float3(0.0, -pixelDirRBViewspaceSizeAtCenterZ.y, 0.0) + viewspaceDirZNormalized * (pixTZ - pixCenterPos.z);
			// very close approximation of: float3 pixBPos  = FFX_CACAO_NDCToViewSpace( normalizedScreenPos + float2( 0.0, +g_FFX_CACAO_Consts.HalfViewportPixelSize.y ), pixBZ ).xyz - pixCenterPos.xyz;
			float3 pixBDelta = float3(0.0, +pixelDirRBViewspaceSizeAtCenterZ.y, 0.0) + viewspaceDirZNormalized * (pixBZ - pixCenterPos.z);

			const float rangeReductionConst = 4.0f;                         // this is to avoid various artifacts
			const float modifiedFalloffCalcMulSq = rangeReductionConst * falloffCalcMulSq;

			float4 additionalObscurance;
			additionalObscurance.x = FFX_CACAO_CalculatePixelObscurance(pixelNormal, pixLDelta, modifiedFalloffCalcMulSq);
			additionalObscurance.y = FFX_CACAO_CalculatePixelObscurance(pixelNormal, pixRDelta, modifiedFalloffCalcMulSq);
			additionalObscurance.z = FFX_CACAO_CalculatePixelObscurance(pixelNormal, pixTDelta, modifiedFalloffCalcMulSq);
			additionalObscurance.w = FFX_CACAO_CalculatePixelObscurance(pixelNormal, pixBDelta, modifiedFalloffCalcMulSq);

			obscuranceSum += g_FFX_CACAO_Consts.DetailAOStrength * dot(additionalObscurance, edgesLRTB);
		}
	}

	// Sharp normals also create edges - but this adds to the cost as well
	if (!adaptiveBase && (qualityLevel >= FFX_CACAO_NORMAL_BASED_EDGES_ENABLE_AT_QUALITY_PRESET))
	{
		float3 neighbourNormalL = FFX_CACAO_SSAOGeneration_GetNormalPass(SVPosui + int2(-1, +0), g_FFX_CACAO_Consts.PassIndex);
		float3 neighbourNormalR = FFX_CACAO_SSAOGeneration_GetNormalPass(SVPosui + int2(+1, +0), g_FFX_CACAO_Consts.PassIndex);
		float3 neighbourNormalT = FFX_CACAO_SSAOGeneration_GetNormalPass(SVPosui + int2(+0, -1), g_FFX_CACAO_Consts.PassIndex);
		float3 neighbourNormalB = FFX_CACAO_SSAOGeneration_GetNormalPass(SVPosui + int2(+0, +1), g_FFX_CACAO_Consts.PassIndex);

		const float dotThreshold = FFX_CACAO_NORMAL_BASED_EDGES_DOT_THRESHOLD;

		float4 normalEdgesLRTB;
		normalEdgesLRTB.x = saturate((dot(pixelNormal, neighbourNormalL) + dotThreshold));
		normalEdgesLRTB.y = saturate((dot(pixelNormal, neighbourNormalR) + dotThreshold));
		normalEdgesLRTB.z = saturate((dot(pixelNormal, neighbourNormalT) + dotThreshold));
		normalEdgesLRTB.w = saturate((dot(pixelNormal, neighbourNormalB) + dotThreshold));

		//#define FFX_CACAO_SMOOTHEN_NORMALS // fixes some aliasing artifacts but kills a lot of high detail and adds to the cost - not worth it probably but feel free to play with it
#ifdef FFX_CACAO_SMOOTHEN_NORMALS
		//neighbourNormalL  = LoadNormal( fullResCoord, int2( -1,  0 ) );
		//neighbourNormalR  = LoadNormal( fullResCoord, int2(  1,  0 ) );
		//neighbourNormalT  = LoadNormal( fullResCoord, int2(  0, -1 ) );
		//neighbourNormalB  = LoadNormal( fullResCoord, int2(  0,  1 ) );
		pixelNormal += neighbourNormalL * edgesLRTB.x + neighbourNormalR * edgesLRTB.y + neighbourNormalT * edgesLRTB.z + neighbourNormalB * edgesLRTB.w;
		pixelNormal = normalize(pixelNormal);
#endif

		edgesLRTB *= normalEdgesLRTB;
	}



	const float globalMipOffset = FFX_CACAO_DEPTH_MIPS_GLOBAL_OFFSET;
	float mipOffset = (qualityLevel < FFX_CACAO_DEPTH_MIPS_ENABLE_AT_QUALITY_PRESET) ? (0) : (log2(pixLookupRadiusMod) + globalMipOffset);

	// Used to tilt the second set of samples so that the disk is effectively rotated by the normal
	// effective at removing one set of artifacts, but too expensive for lower quality settings
	float2 normXY = float2(pixelNormal.x, pixelNormal.y);
	float normXYLength = length(normXY);
	normXY /= float2(normXYLength, -normXYLength);
	normXYLength *= FFX_CACAO_TILT_SAMPLES_AMOUNT;

	const float3 negViewspaceDir = -normalize(pixCenterPos);

	// standard, non-adaptive approach
	if ((qualityLevel != 3) || adaptiveBase)
	{
		[unroll]
		for (int i = 0; i < numberOfTaps; i++)
		{
			FFX_CACAO_SSAOTap(qualityLevel, obscuranceSum, weightSum, i, rotScale, pixCenterPos, negViewspaceDir, pixelNormal, normalizedScreenPos, depthBufferUV, mipOffset, falloffCalcMulSq, 1.0, normXY, normXYLength);
		}
	}
	else // if( qualityLevel == 3 ) adaptive approach
	{
		// add new ones if needed
		float2 fullResUV = normalizedScreenPos + g_FFX_CACAO_Consts.PerPassFullResUVOffset.xy;
		float importance = FFX_CACAO_SSAOGeneration_SampleImportance(fullResUV);

		// this is to normalize FFX_CACAO_DETAIL_AO_AMOUNT across all pixel regardless of importance
		obscuranceSum *= (FFX_CACAO_ADAPTIVE_TAP_BASE_COUNT / (float)FFX_CACAO_MAX_TAPS) + (importance * FFX_CACAO_ADAPTIVE_TAP_FLEXIBLE_COUNT / (float)FFX_CACAO_MAX_TAPS);

		// load existing base values
		float2 baseValues = FFX_CACAO_SSAOGeneration_LoadBasePassSSAOPass(SVPosui, g_FFX_CACAO_Consts.PassIndex);
		weightSum += baseValues.y * (float)(FFX_CACAO_ADAPTIVE_TAP_BASE_COUNT * 4.0);
		obscuranceSum += (baseValues.x) * weightSum;

		// increase importance around edges
		float edgeCount = dot(1.0 - edgesLRTB, float4(1.0, 1.0, 1.0, 1.0));

		float avgTotalImportance = (float)FFX_CACAO_SSAOGeneration_GetLoadCounter() * g_FFX_CACAO_Consts.LoadCounterAvgDiv;

		float importanceLimiter = saturate(g_FFX_CACAO_Consts.AdaptiveSampleCountLimit / avgTotalImportance);
		importance *= importanceLimiter;

		float additionalSampleCountFlt = FFX_CACAO_ADAPTIVE_TAP_FLEXIBLE_COUNT * importance;

		additionalSampleCountFlt += 1.5;
		uint additionalSamples = uint(additionalSampleCountFlt);
		uint additionalSamplesTo = min(FFX_CACAO_MAX_TAPS, additionalSamples + FFX_CACAO_ADAPTIVE_TAP_BASE_COUNT);

		// sample loop
		{
			float4 newSample = g_FFX_CACAO_samplePatternMain[FFX_CACAO_ADAPTIVE_TAP_BASE_COUNT];
			FFX_CACAO_SSAOSampleData data = FFX_CACAO_SSAOGetSampleData(qualityLevel, rotScale, newSample, mipOffset);
			FFX_CACAO_SSAOHits hits = FFX_CACAO_SSAOGetHits2(data, depthBufferUV);
			newSample = g_FFX_CACAO_samplePatternMain[FFX_CACAO_ADAPTIVE_TAP_BASE_COUNT + 1];

			for (uint i = FFX_CACAO_ADAPTIVE_TAP_BASE_COUNT; i < additionalSamplesTo - 1; i++)
			{
				data = FFX_CACAO_SSAOGetSampleData(qualityLevel, rotScale, newSample, mipOffset);
				newSample = g_FFX_CACAO_samplePatternMain[i + 2];
				FFX_CACAO_SSAOHits nextHits = FFX_CACAO_SSAOGetHits2(data, depthBufferUV);

				FFX_CACAO_SSAOAddHits(qualityLevel, pixCenterPos, pixelNormal, falloffCalcMulSq, weightSum, obscuranceSum, hits);
				hits = nextHits;
			}

			// last loop iteration
			{
				FFX_CACAO_SSAOAddHits(qualityLevel, pixCenterPos, pixelNormal, falloffCalcMulSq, weightSum, obscuranceSum, hits);
			}
		}
	}

	// early out for adaptive base - just output weight (used for the next pass)
	if (adaptiveBase)
	{
		float obscurance = obscuranceSum / weightSum;

		outShadowTerm = obscurance;
		outEdges = 0;
		outWeight = weightSum;
		return;
	}

	// calculate weighted average
	float obscurance = obscuranceSum / weightSum;

	// calculate fadeout (1 close, gradient, 0 far)
	float fadeOut = saturate(pixCenterPos.z * g_FFX_CACAO_Consts.EffectFadeOutMul + g_FFX_CACAO_Consts.EffectFadeOutAdd);

	// Reduce the SSAO shadowing if we're on the edge to remove artifacts on edges (we don't care for the lower quality one)
	if (!adaptiveBase && (qualityLevel >= FFX_CACAO_DEPTH_BASED_EDGES_ENABLE_AT_QUALITY_PRESET))
	{
		// float edgeCount = dot( 1.0-edgesLRTB, float4( 1.0, 1.0, 1.0, 1.0 ) );

		// when there's more than 2 opposite edges, start fading out the occlusion to reduce aliasing artifacts
		float edgeFadeoutFactor = saturate((1.0 - edgesLRTB.x - edgesLRTB.y) * 0.35) + saturate((1.0 - edgesLRTB.z - edgesLRTB.w) * 0.35);

		// (experimental) if you want to reduce the effect next to any edge
		// edgeFadeoutFactor += 0.1 * saturate( dot( 1 - edgesLRTB, float4( 1, 1, 1, 1 ) ) );

		fadeOut *= saturate(1.0 - edgeFadeoutFactor);
	}

	// same as a bove, but a lot more conservative version
	// fadeOut *= saturate( dot( edgesLRTB, float4( 0.9, 0.9, 0.9, 0.9 ) ) - 2.6 );

	// strength
	obscurance = g_FFX_CACAO_Consts.EffectShadowStrength * obscurance;

	// clamp
	obscurance = min(obscurance, g_FFX_CACAO_Consts.EffectShadowClamp);

	// fadeout
	obscurance *= fadeOut;

	// conceptually switch to occlusion with the meaning being visibility (grows with visibility, occlusion == 1 implies full visibility),
	// to be in line with what is more commonly used.
	float occlusion = 1.0 - obscurance;

	// modify the gradient
	// note: this cannot be moved to a later pass because of loss of precision after storing in the render target
	occlusion = pow(saturate(occlusion), g_FFX_CACAO_Consts.EffectShadowPow);

	// outputs!
	outShadowTerm = occlusion;    // Our final 'occlusion' term (0 means fully occluded, 1 means fully lit)
	outEdges = edgesLRTB;    // These are used to prevent blurring across edges, 1 means no edge, 0 means edge, 0.5 means half way there, etc.
	outWeight = weightSum;
}

[numthreads(FFX_CACAO_GENERATE_SPARSE_WIDTH, FFX_CACAO_GENERATE_SPARSE_HEIGHT, 1)]
void FFX_CACAO_GenerateQ0(uint3 tid : SV_DispatchThreadID)
{
	uint xOffset = (tid.y * 3 + tid.z) % 5;
	uint2 coord = uint2(5 * tid.x + xOffset, tid.y);
	float2 inPos = (float2)coord;
	float   outShadowTerm;
	float   outWeight;
	float4  outEdges;
	FFX_CACAO_GenerateSSAOShadowsInternal(outShadowTerm, outEdges, outWeight, inPos.xy, 0, false);
	float2 out0;
	out0.x = outShadowTerm;
	out0.y = FFX_CACAO_PackEdges(float4(1, 1, 1, 1)); // no edges in low quality
	FFX_CACAO_SSAOGeneration_StoreOutput(coord, out0);
}

[numthreads(FFX_CACAO_GENERATE_SPARSE_WIDTH, FFX_CACAO_GENERATE_SPARSE_HEIGHT, 1)]
void FFX_CACAO_GenerateQ1(uint3 tid : SV_DispatchThreadID)
{
	uint xOffset = (tid.y * 3 + tid.z) % 5;
	uint2 coord = uint2(5 * tid.x + xOffset, tid.y);
	float2 inPos = (float2)coord;
	float   outShadowTerm;
	float   outWeight;
	float4  outEdges;
	FFX_CACAO_GenerateSSAOShadowsInternal(outShadowTerm, outEdges, outWeight, inPos.xy, 1, false);
	float2 out0;
	out0.x = outShadowTerm;
	out0.y = FFX_CACAO_PackEdges(outEdges);
	FFX_CACAO_SSAOGeneration_StoreOutput(coord, out0);
}

[numthreads(FFX_CACAO_GENERATE_WIDTH, FFX_CACAO_GENERATE_HEIGHT, 1)]
void FFX_CACAO_GenerateQ2(uint2 coord : SV_DispatchThreadID)
{
	float2 inPos = (float2)coord;
	float   outShadowTerm;
	float   outWeight;
	float4  outEdges;
	FFX_CACAO_GenerateSSAOShadowsInternal(outShadowTerm, outEdges, outWeight, inPos.xy, 2, false);
	float2 out0;
	out0.x = outShadowTerm;
	out0.y = FFX_CACAO_PackEdges(outEdges);
	FFX_CACAO_SSAOGeneration_StoreOutput(coord, out0);
}

[numthreads(FFX_CACAO_GENERATE_WIDTH, FFX_CACAO_GENERATE_HEIGHT, 1)]
void FFX_CACAO_GenerateQ3Base(uint2 coord : SV_DispatchThreadID)
{
	float2 inPos = (float2)coord;
	float   outShadowTerm;
	float   outWeight;
	float4  outEdges;
	FFX_CACAO_GenerateSSAOShadowsInternal(outShadowTerm, outEdges, outWeight, inPos.xy, 3, true);
	float2 out0;
	out0.x = outShadowTerm;
	out0.y = outWeight / ((float)FFX_CACAO_ADAPTIVE_TAP_BASE_COUNT * 4.0); //0.0; //frac(outWeight / 6.0);// / (float)(FFX_CACAO_MAX_TAPS * 4.0);
	FFX_CACAO_SSAOGeneration_StoreOutput(coord, out0);
}

[numthreads(FFX_CACAO_GENERATE_WIDTH, FFX_CACAO_GENERATE_HEIGHT, 1)]
void FFX_CACAO_GenerateQ3(uint2 coord : SV_DispatchThreadID)
{

	float2 inPos = (float2)coord;
	float   outShadowTerm;
	float   outWeight;
	float4  outEdges;
	FFX_CACAO_GenerateSSAOShadowsInternal(outShadowTerm, outEdges, outWeight, inPos.xy, 3, false);
	float2 out0;
	out0.x = outShadowTerm;
	out0.y = FFX_CACAO_PackEdges(outEdges);
	FFX_CACAO_SSAOGeneration_StoreOutput(coord, out0);
}

// =======================================================
// Apply

[numthreads(FFX_CACAO_APPLY_WIDTH, FFX_CACAO_APPLY_HEIGHT, 1)]
void FFX_CACAO_Apply(uint2 coord : SV_DispatchThreadID)
{
	float ao;
	float2 inPos = coord;
	uint2 pixPos = coord;
	uint2 pixPosHalf = pixPos / uint2(2, 2);

	// calculate index in the four deinterleaved source array texture
	int mx = (pixPos.x % 2);
	int my = (pixPos.y % 2);
	int ic = mx + my * 2;       // center index
	int ih = (1 - mx) + my * 2;   // neighbouring, horizontal
	int iv = mx + (1 - my) * 2;   // neighbouring, vertical
	int id = (1 - mx) + (1 - my) * 2; // diagonal

	float2 centerVal = FFX_CACAO_Apply_LoadSSAOPass(pixPosHalf, ic);

	ao = centerVal.x;

#if 1   // change to 0 if you want to disable last pass high-res blur (for debugging purposes, etc.)
	float4 edgesLRTB = FFX_CACAO_UnpackEdges(centerVal.y);

	// return 1.0 - float4( edgesLRTB.x, edgesLRTB.y * 0.5 + edgesLRTB.w * 0.5, edgesLRTB.z, 0.0 ); // debug show edges

	// convert index shifts to sampling offsets
	float fmx = (float)mx;
	float fmy = (float)my;

	// in case of an edge, push sampling offsets away from the edge (towards pixel center)
	float fmxe = (edgesLRTB.y - edgesLRTB.x);
	float fmye = (edgesLRTB.w - edgesLRTB.z);

	// calculate final sampling offsets and sample using bilinear filter
	float2  uvH = (inPos.xy + float2(fmx + fmxe - 0.5, 0.5 - fmy)) * 0.5 * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
	float   aoH = FFX_CACAO_Apply_SampleSSAOUVPass(uvH, ih);
	float2  uvV = (inPos.xy + float2(0.5 - fmx, fmy - 0.5 + fmye)) * 0.5 * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
	float   aoV = FFX_CACAO_Apply_SampleSSAOUVPass(uvV, iv);
	float2  uvD = (inPos.xy + float2(fmx - 0.5 + fmxe, fmy - 0.5 + fmye)) * 0.5 * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
	float   aoD = FFX_CACAO_Apply_SampleSSAOUVPass(uvD, id);

	// reduce weight for samples near edge - if the edge is on both sides, weight goes to 0
	float4 blendWeights;
	blendWeights.x = 1.0;
	blendWeights.y = (edgesLRTB.x + edgesLRTB.y) * 0.5;
	blendWeights.z = (edgesLRTB.z + edgesLRTB.w) * 0.5;
	blendWeights.w = (blendWeights.y + blendWeights.z) * 0.5;

	// calculate weighted average
	float blendWeightsSum = dot(blendWeights, float4(1.0, 1.0, 1.0, 1.0));
	ao = dot(float4(ao, aoH, aoV, aoD), blendWeights) / blendWeightsSum;
#endif

	FFX_CACAO_Apply_StoreOutput(coord, ao.x);
}


// edge-ignorant blur & apply (for the lowest quality level 0)
[numthreads(FFX_CACAO_APPLY_WIDTH, FFX_CACAO_APPLY_HEIGHT, 1)]
void FFX_CACAO_NonSmartApply(uint2 tid : SV_DispatchThreadID)
{
	float2 inUV = float2(tid) * g_FFX_CACAO_Consts.OutputBufferInverseDimensions;
	float a = FFX_CACAO_Apply_SampleSSAOUVPass(inUV.xy, 0);
	float b = FFX_CACAO_Apply_SampleSSAOUVPass(inUV.xy, 1);
	float c = FFX_CACAO_Apply_SampleSSAOUVPass(inUV.xy, 2);
	float d = FFX_CACAO_Apply_SampleSSAOUVPass(inUV.xy, 3);
	float avg = (a + b + c + d) * 0.25f;

	FFX_CACAO_Apply_StoreOutput(tid, avg);
}

// edge-ignorant blur & apply, skipping half pixels in checkerboard pattern (for the Lowest quality level 0 and Settings::SkipHalfPixelsOnLowQualityLevel == true )
[numthreads(FFX_CACAO_APPLY_WIDTH, FFX_CACAO_APPLY_HEIGHT, 1)]
void FFX_CACAO_NonSmartHalfApply(uint2 tid : SV_DispatchThreadID)
{
	float2 inUV = float2(tid) * g_FFX_CACAO_Consts.OutputBufferInverseDimensions;
	float a = FFX_CACAO_Apply_SampleSSAOUVPass(inUV.xy, 0);
	float d = FFX_CACAO_Apply_SampleSSAOUVPass(inUV.xy, 3);
	float avg = (a + d) * 0.5f;

	FFX_CACAO_Apply_StoreOutput(tid, avg);
}

// =============================================================================
// Prepare

groupshared float s_FFX_CACAO_PrepareDepthsAndMipsBuffer[4][8][8];

float FFX_CACAO_MipSmartAverage(float4 depths)
{
	float closest = min(min(depths.x, depths.y), min(depths.z, depths.w));
	float falloffCalcMulSq = -1.0f / g_FFX_CACAO_Consts.EffectRadius * g_FFX_CACAO_Consts.EffectRadius;
	float4 dists = depths - closest.xxxx;
	float4 weights = saturate(dists * dists * falloffCalcMulSq + 1.0);
	return dot(weights, depths) / dot(weights, float4(1.0, 1.0, 1.0, 1.0));
}

void FFX_CACAO_PrepareDepthsAndMips(float4 samples, uint2 outputCoord, uint2 gtid)
{
	samples = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples);

	s_FFX_CACAO_PrepareDepthsAndMipsBuffer[0][gtid.x][gtid.y] = samples.w;
	s_FFX_CACAO_PrepareDepthsAndMipsBuffer[1][gtid.x][gtid.y] = samples.z;
	s_FFX_CACAO_PrepareDepthsAndMipsBuffer[2][gtid.x][gtid.y] = samples.x;
	s_FFX_CACAO_PrepareDepthsAndMipsBuffer[3][gtid.x][gtid.y] = samples.y;

	FFX_CACAO_Prepare_StoreDepthMip0(outputCoord, 0, samples.w);
	FFX_CACAO_Prepare_StoreDepthMip0(outputCoord, 1, samples.z);
	FFX_CACAO_Prepare_StoreDepthMip0(outputCoord, 2, samples.x);
	FFX_CACAO_Prepare_StoreDepthMip0(outputCoord, 3, samples.y);

	uint depthArrayIndex = 2 * (gtid.y % 2) + (gtid.x % 2);
	uint2 depthArrayOffset = int2(gtid.x % 2, gtid.y % 2);
	int2 bufferCoord = int2(gtid) - int2(depthArrayOffset);

	outputCoord /= 2;
	GroupMemoryBarrierWithGroupSync();

	// if (stillAlive) <-- all threads alive here
	{
		float sample_00 = s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 0][bufferCoord.y + 0];
		float sample_01 = s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 0][bufferCoord.y + 1];
		float sample_10 = s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 1][bufferCoord.y + 0];
		float sample_11 = s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 1][bufferCoord.y + 1];

		float avg = FFX_CACAO_MipSmartAverage(float4(sample_00, sample_01, sample_10, sample_11));
		FFX_CACAO_Prepare_StoreDepthMip1(outputCoord, depthArrayIndex, avg);
		s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x][bufferCoord.y] = avg;
	}

	bool stillAlive = gtid.x % 4 == depthArrayOffset.x && gtid.y % 4 == depthArrayOffset.y;

	outputCoord /= 2;
	GroupMemoryBarrierWithGroupSync();

	if (stillAlive)
	{
		float sample_00 = s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 0][bufferCoord.y + 0];
		float sample_01 = s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 0][bufferCoord.y + 2];
		float sample_10 = s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 2][bufferCoord.y + 0];
		float sample_11 = s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 2][bufferCoord.y + 2];

		float avg = FFX_CACAO_MipSmartAverage(float4(sample_00, sample_01, sample_10, sample_11));
		FFX_CACAO_Prepare_StoreDepthMip2(outputCoord, depthArrayIndex, avg);
		s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x][bufferCoord.y] = avg;
	}

	stillAlive = gtid.x % 8 == depthArrayOffset.x && gtid.y % 8 == depthArrayOffset.y;

	outputCoord /= 2;
	GroupMemoryBarrierWithGroupSync();

	if (stillAlive)
	{
		float sample_00 = s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 0][bufferCoord.y + 0];
		float sample_01 = s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 0][bufferCoord.y + 4];
		float sample_10 = s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 4][bufferCoord.y + 0];
		float sample_11 = s_FFX_CACAO_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 4][bufferCoord.y + 4];

		float avg = FFX_CACAO_MipSmartAverage(float4(sample_00, sample_01, sample_10, sample_11));
		FFX_CACAO_Prepare_StoreDepthMip3(outputCoord, depthArrayIndex, avg);
	}
}

[numthreads(FFX_CACAO_PREPARE_DEPTHS_AND_MIPS_WIDTH, FFX_CACAO_PREPARE_DEPTHS_AND_MIPS_HEIGHT, 1)]
void FFX_CACAO_PrepareDownsampledDepthsAndMips(uint2 tid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID)
{
	int2 depthBufferCoord = 4 * tid.xy;
	int2 outputCoord = tid;

	float2 uv = (float2(depthBufferCoord)+0.5f) * g_FFX_CACAO_Consts.DepthBufferInverseDimensions;
	float4 samples;

	samples.x = FFX_CACAO_Prepare_SampleDepthOffset(uv, int2(0, 2));
	samples.y = FFX_CACAO_Prepare_SampleDepthOffset(uv, int2(2, 2));
	samples.z = FFX_CACAO_Prepare_SampleDepthOffset(uv, int2(2, 0));
	samples.w = FFX_CACAO_Prepare_SampleDepthOffset(uv, int2(0, 0));

	FFX_CACAO_PrepareDepthsAndMips(samples, outputCoord, gtid);
}

[numthreads(FFX_CACAO_PREPARE_DEPTHS_AND_MIPS_WIDTH, FFX_CACAO_PREPARE_DEPTHS_AND_MIPS_HEIGHT, 1)]
void FFX_CACAO_PrepareNativeDepthsAndMips(uint2 tid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID)
{
	int2 depthBufferCoord = 2 * tid.xy;
	int2 outputCoord = tid;

	float2 uv = (float2(depthBufferCoord)+0.5f) * g_FFX_CACAO_Consts.DepthBufferInverseDimensions;
	float4 samples = FFX_CACAO_Prepare_GatherDepth(uv);

	FFX_CACAO_PrepareDepthsAndMips(samples, outputCoord, gtid);
}

void FFX_CACAO_PrepareDepths(float4 samples, uint2 tid)
{
	samples = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples);
	FFX_CACAO_Prepare_StoreDepth(tid, 0, samples.w);
	FFX_CACAO_Prepare_StoreDepth(tid, 1, samples.z);
	FFX_CACAO_Prepare_StoreDepth(tid, 2, samples.x);
	FFX_CACAO_Prepare_StoreDepth(tid, 3, samples.y);
}

[numthreads(FFX_CACAO_PREPARE_DEPTHS_WIDTH, FFX_CACAO_PREPARE_DEPTHS_HEIGHT, 1)]
void FFX_CACAO_PrepareDownsampledDepths(uint2 tid : SV_DispatchThreadID)
{
	int2 depthBufferCoord = 4 * tid.xy;

	float2 uv = (float2(depthBufferCoord)+0.5f) * g_FFX_CACAO_Consts.DepthBufferInverseDimensions;
	float4 samples;

	samples.x = FFX_CACAO_Prepare_SampleDepthOffset(uv, int2(0, 2));
	samples.y = FFX_CACAO_Prepare_SampleDepthOffset(uv, int2(2, 2));
	samples.z = FFX_CACAO_Prepare_SampleDepthOffset(uv, int2(2, 0));
	samples.w = FFX_CACAO_Prepare_SampleDepthOffset(uv, int2(0, 0));

	FFX_CACAO_PrepareDepths(samples, tid);
}

[numthreads(FFX_CACAO_PREPARE_DEPTHS_WIDTH, FFX_CACAO_PREPARE_DEPTHS_HEIGHT, 1)]
void FFX_CACAO_PrepareNativeDepths(uint2 tid : SV_DispatchThreadID)
{
	int2 depthBufferCoord = 2 * tid.xy;

	float2 uv = (float2(depthBufferCoord)+0.5f) * g_FFX_CACAO_Consts.DepthBufferInverseDimensions;
	float4 samples = FFX_CACAO_Prepare_GatherDepth(uv);

	FFX_CACAO_PrepareDepths(samples, tid);
}

[numthreads(FFX_CACAO_PREPARE_DEPTHS_HALF_WIDTH, FFX_CACAO_PREPARE_DEPTHS_HALF_HEIGHT, 1)]
void FFX_CACAO_PrepareDownsampledDepthsHalf(uint2 tid : SV_DispatchThreadID)
{
	float sample_00 = FFX_CACAO_Prepare_LoadDepth(int2(4 * tid.x + 0, 4 * tid.y + 0));
	float sample_11 = FFX_CACAO_Prepare_LoadDepth(int2(4 * tid.x + 2, 4 * tid.y + 2));
	sample_00 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(sample_00);
	sample_11 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(sample_11);
	FFX_CACAO_Prepare_StoreDepth(tid, 0, sample_00);
	FFX_CACAO_Prepare_StoreDepth(tid, 3, sample_11);
}

[numthreads(FFX_CACAO_PREPARE_DEPTHS_HALF_WIDTH, FFX_CACAO_PREPARE_DEPTHS_HALF_HEIGHT, 1)]
void FFX_CACAO_PrepareNativeDepthsHalf(uint2 tid : SV_DispatchThreadID)
{
	float sample_00 = FFX_CACAO_Prepare_LoadDepth(int2(2 * tid.x + 0, 2 * tid.y + 0));
	float sample_11 = FFX_CACAO_Prepare_LoadDepth(int2(2 * tid.x + 1, 2 * tid.y + 1));
	sample_00 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(sample_00);
	sample_11 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(sample_11);
	FFX_CACAO_Prepare_StoreDepth(tid, 0, sample_00);
	FFX_CACAO_Prepare_StoreDepth(tid, 3, sample_11);
}

struct FFX_CACAO_PrepareNormalsInputDepths
{
	float depth_10;
	float depth_20;

	float depth_01;
	float depth_11;
	float depth_21;
	float depth_31;

	float depth_02;
	float depth_12;
	float depth_22;
	float depth_32;

	float depth_13;
	float depth_23;
};

void FFX_CACAO_PrepareNormals(FFX_CACAO_PrepareNormalsInputDepths depths, float2 uv, float2 pixelSize, int2 normalCoord)
{
	float3 p_10 = FFX_CACAO_NDCToViewSpace(uv + float2(+0.0f, -1.0f) * pixelSize, depths.depth_10);
	float3 p_20 = FFX_CACAO_NDCToViewSpace(uv + float2(+1.0f, -1.0f) * pixelSize, depths.depth_20);

	float3 p_01 = FFX_CACAO_NDCToViewSpace(uv + float2(-1.0f, +0.0f) * pixelSize, depths.depth_01);
	float3 p_11 = FFX_CACAO_NDCToViewSpace(uv + float2(+0.0f, +0.0f) * pixelSize, depths.depth_11);
	float3 p_21 = FFX_CACAO_NDCToViewSpace(uv + float2(+1.0f, +0.0f) * pixelSize, depths.depth_21);
	float3 p_31 = FFX_CACAO_NDCToViewSpace(uv + float2(+2.0f, +0.0f) * pixelSize, depths.depth_31);

	float3 p_02 = FFX_CACAO_NDCToViewSpace(uv + float2(-1.0f, +1.0f) * pixelSize, depths.depth_02);
	float3 p_12 = FFX_CACAO_NDCToViewSpace(uv + float2(+0.0f, +1.0f) * pixelSize, depths.depth_12);
	float3 p_22 = FFX_CACAO_NDCToViewSpace(uv + float2(+1.0f, +1.0f) * pixelSize, depths.depth_22);
	float3 p_32 = FFX_CACAO_NDCToViewSpace(uv + float2(+2.0f, +1.0f) * pixelSize, depths.depth_32);

	float3 p_13 = FFX_CACAO_NDCToViewSpace(uv + float2(+0.0f, +2.0f) * pixelSize, depths.depth_13);
	float3 p_23 = FFX_CACAO_NDCToViewSpace(uv + float2(+1.0f, +2.0f) * pixelSize, depths.depth_23);

	float4 edges_11 = FFX_CACAO_CalculateEdges(p_11.z, p_01.z, p_21.z, p_10.z, p_12.z);
	float4 edges_21 = FFX_CACAO_CalculateEdges(p_21.z, p_11.z, p_31.z, p_20.z, p_22.z);
	float4 edges_12 = FFX_CACAO_CalculateEdges(p_12.z, p_02.z, p_22.z, p_11.z, p_13.z);
	float4 edges_22 = FFX_CACAO_CalculateEdges(p_22.z, p_12.z, p_32.z, p_21.z, p_23.z);

	float3 norm_11 = FFX_CACAO_CalculateNormal(edges_11, p_11, p_01, p_21, p_10, p_12);
	float3 norm_21 = FFX_CACAO_CalculateNormal(edges_21, p_21, p_11, p_31, p_20, p_22);
	float3 norm_12 = FFX_CACAO_CalculateNormal(edges_12, p_12, p_02, p_22, p_11, p_13);
	float3 norm_22 = FFX_CACAO_CalculateNormal(edges_22, p_22, p_12, p_32, p_21, p_23);

	FFX_CACAO_Prepare_StoreNormal(normalCoord, 0, norm_11);
	FFX_CACAO_Prepare_StoreNormal(normalCoord, 1, norm_21);
	FFX_CACAO_Prepare_StoreNormal(normalCoord, 2, norm_12);
	FFX_CACAO_Prepare_StoreNormal(normalCoord, 3, norm_22);
}

[numthreads(FFX_CACAO_PREPARE_NORMALS_WIDTH, FFX_CACAO_PREPARE_NORMALS_HEIGHT, 1)]
void FFX_CACAO_PrepareDownsampledNormals(int2 tid : SV_DispatchThreadID)
{
	int2 depthCoord = 4 * tid + g_FFX_CACAO_Consts.DepthBufferOffset;

	FFX_CACAO_PrepareNormalsInputDepths depths;

	depths.depth_10 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_Prepare_LoadDepthOffset(depthCoord, int2(+0, -2)));
	depths.depth_20 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_Prepare_LoadDepthOffset(depthCoord, int2(+2, -2)));

	depths.depth_01 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_Prepare_LoadDepthOffset(depthCoord, int2(-2, +0)));
	depths.depth_11 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_Prepare_LoadDepthOffset(depthCoord, int2(+0, +0)));
	depths.depth_21 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_Prepare_LoadDepthOffset(depthCoord, int2(+2, +0)));
	depths.depth_31 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_Prepare_LoadDepthOffset(depthCoord, int2(+4, +0)));

	depths.depth_02 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_Prepare_LoadDepthOffset(depthCoord, int2(-2, +2)));
	depths.depth_12 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_Prepare_LoadDepthOffset(depthCoord, int2(+0, +2)));
	depths.depth_22 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_Prepare_LoadDepthOffset(depthCoord, int2(+2, +2)));
	depths.depth_32 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_Prepare_LoadDepthOffset(depthCoord, int2(+4, +2)));

	depths.depth_13 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_Prepare_LoadDepthOffset(depthCoord, int2(+0, +4)));
	depths.depth_23 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_Prepare_LoadDepthOffset(depthCoord, int2(+2, +4)));

	float2 pixelSize = 2.0f * g_FFX_CACAO_Consts.OutputBufferInverseDimensions; // 2.0f * g_FFX_CACAO_Consts.DepthBufferInverseDimensions;
	float2 uv = (float2(4 * tid) + 0.5f) * g_FFX_CACAO_Consts.OutputBufferInverseDimensions; // * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;

	FFX_CACAO_PrepareNormals(depths, uv, pixelSize, tid);
}

[numthreads(FFX_CACAO_PREPARE_NORMALS_WIDTH, FFX_CACAO_PREPARE_NORMALS_HEIGHT, 1)]
void FFX_CACAO_PrepareNativeNormals(int2 tid : SV_DispatchThreadID)
{
	int2 depthCoord = 2 * tid + g_FFX_CACAO_Consts.DepthBufferOffset;
	float2 depthBufferUV = (float2(depthCoord)-0.5f) * g_FFX_CACAO_Consts.DepthBufferInverseDimensions;
	float4 samples_00 = FFX_CACAO_Prepare_GatherDepthOffset(depthBufferUV, int2(0, 0));
	float4 samples_10 = FFX_CACAO_Prepare_GatherDepthOffset(depthBufferUV, int2(2, 0));
	float4 samples_01 = FFX_CACAO_Prepare_GatherDepthOffset(depthBufferUV, int2(0, 2));
	float4 samples_11 = FFX_CACAO_Prepare_GatherDepthOffset(depthBufferUV, int2(2, 2));

	FFX_CACAO_PrepareNormalsInputDepths depths;

	depths.depth_10 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples_00.z);
	depths.depth_20 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples_10.w);

	depths.depth_01 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples_00.x);
	depths.depth_11 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples_00.y);
	depths.depth_21 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples_10.x);
	depths.depth_31 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples_10.y);

	depths.depth_02 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples_01.w);
	depths.depth_12 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples_01.z);
	depths.depth_22 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples_11.w);
	depths.depth_32 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples_11.z);

	depths.depth_13 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples_01.y);
	depths.depth_23 = FFX_CACAO_ScreenSpaceToViewSpaceDepth(samples_11.x);

	// use unused samples to make sure compiler doesn't overlap memory and put a sync
	// between loads
	float epsilon = (samples_00.w + samples_10.z + samples_01.x + samples_11.y) * 1e-20f;

	float2 pixelSize = g_FFX_CACAO_Consts.OutputBufferInverseDimensions;
	float2 uv = (float2(2 * tid) + 0.5f + epsilon) * g_FFX_CACAO_Consts.OutputBufferInverseDimensions;

	FFX_CACAO_PrepareNormals(depths, uv, pixelSize, tid);
}

[numthreads(PREPARE_NORMALS_FROM_INPUT_NORMALS_WIDTH, PREPARE_NORMALS_FROM_INPUT_NORMALS_HEIGHT, 1)]
void FFX_CACAO_PrepareDownsampledNormalsFromInputNormals(int2 tid : SV_DispatchThreadID)
{
	int2 baseCoord = 4 * tid;
	FFX_CACAO_Prepare_StoreNormal(tid, 0, FFX_CACAO_Prepare_LoadNormal(baseCoord + int2(0, 0)));
	FFX_CACAO_Prepare_StoreNormal(tid, 1, FFX_CACAO_Prepare_LoadNormal(baseCoord + int2(2, 0)));
	FFX_CACAO_Prepare_StoreNormal(tid, 2, FFX_CACAO_Prepare_LoadNormal(baseCoord + int2(0, 2)));
	FFX_CACAO_Prepare_StoreNormal(tid, 3, FFX_CACAO_Prepare_LoadNormal(baseCoord + int2(2, 2)));
}

[numthreads(PREPARE_NORMALS_FROM_INPUT_NORMALS_WIDTH, PREPARE_NORMALS_FROM_INPUT_NORMALS_HEIGHT, 1)]
void FFX_CACAO_PrepareNativeNormalsFromInputNormals(int2 tid : SV_DispatchThreadID)
{
	int2 baseCoord = 2 * tid;
	FFX_CACAO_Prepare_StoreNormal(tid, 0, FFX_CACAO_Prepare_LoadNormal(baseCoord + int2(0, 0)));
	FFX_CACAO_Prepare_StoreNormal(tid, 1, FFX_CACAO_Prepare_LoadNormal(baseCoord + int2(1, 0)));
	FFX_CACAO_Prepare_StoreNormal(tid, 2, FFX_CACAO_Prepare_LoadNormal(baseCoord + int2(0, 1)));
	FFX_CACAO_Prepare_StoreNormal(tid, 3, FFX_CACAO_Prepare_LoadNormal(baseCoord + int2(1, 1)));
}

// =============================================================================
// Importance Map

[numthreads(IMPORTANCE_MAP_WIDTH, IMPORTANCE_MAP_HEIGHT, 1)]
void FFX_CACAO_GenerateImportanceMap(uint2 tid : SV_DispatchThreadID)
{
	uint2 basePos = tid * 2;

	float2 baseUV = (float2(basePos)+float2(0.5f, 0.5f)) * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;

	float avg = 0.0;
	float minV = 1.0;
	float maxV = 0.0;
	[unroll]
	for (int i = 0; i < 4; i++)
	{
		float4 vals = FFX_CACAO_Importance_GatherSSAO(baseUV, i);

		// apply the same modifications that would have been applied in the main shader
		vals = g_FFX_CACAO_Consts.EffectShadowStrength * vals;

		vals = 1 - vals;

		vals = pow(saturate(vals), g_FFX_CACAO_Consts.EffectShadowPow);

		avg += dot(float4(vals.x, vals.y, vals.z, vals.w), float4(1.0 / 16.0, 1.0 / 16.0, 1.0 / 16.0, 1.0 / 16.0));

		maxV = max(maxV, max(max(vals.x, vals.y), max(vals.z, vals.w)));
		minV = min(minV, min(min(vals.x, vals.y), min(vals.z, vals.w)));
	}

	float minMaxDiff = maxV - minV;

	FFX_CACAO_Importance_StoreImportance(tid, pow(saturate(minMaxDiff * 2.0), 0.8));
}

static const float c_FFX_CACAO_SmoothenImportance = 1.0f;

[numthreads(IMPORTANCE_MAP_A_WIDTH, IMPORTANCE_MAP_A_HEIGHT, 1)]
void FFX_CACAO_PostprocessImportanceMapA(uint2 tid : SV_DispatchThreadID)
{
	float2 uv = (float2(tid)+0.5f) * g_FFX_CACAO_Consts.ImportanceMapInverseDimensions;

	float centre = FFX_CACAO_Importance_SampleImportanceA(uv);
	//return centre;

	float2 halfPixel = 0.5f * g_FFX_CACAO_Consts.ImportanceMapInverseDimensions;

	float4 vals;
	vals.x = FFX_CACAO_Importance_SampleImportanceA(uv + float2(-halfPixel.x * 3, -halfPixel.y));
	vals.y = FFX_CACAO_Importance_SampleImportanceA(uv + float2(+halfPixel.x, -halfPixel.y * 3));
	vals.z = FFX_CACAO_Importance_SampleImportanceA(uv + float2(+halfPixel.x * 3, +halfPixel.y));
	vals.w = FFX_CACAO_Importance_SampleImportanceA(uv + float2(-halfPixel.x, +halfPixel.y * 3));

	float avgVal = dot(vals, float4(0.25, 0.25, 0.25, 0.25));
	vals.xy = max(vals.xy, vals.zw);
	float maxVal = max(centre, max(vals.x, vals.y));

	FFX_CACAO_Importance_StoreImportanceA(tid, lerp(maxVal, avgVal, c_FFX_CACAO_SmoothenImportance));
}

[numthreads(IMPORTANCE_MAP_B_WIDTH, IMPORTANCE_MAP_B_HEIGHT, 1)]
void FFX_CACAO_PostprocessImportanceMapB(uint2 tid : SV_DispatchThreadID)
{
	float2 uv = (float2(tid)+0.5f) * g_FFX_CACAO_Consts.ImportanceMapInverseDimensions;

	float centre = FFX_CACAO_Importance_SampleImportanceB(uv);
	//return centre;

	float2 halfPixel = 0.5f * g_FFX_CACAO_Consts.ImportanceMapInverseDimensions;

	float4 vals;
	vals.x = FFX_CACAO_Importance_SampleImportanceB(uv + float2(-halfPixel.x, -halfPixel.y * 3));
	vals.y = FFX_CACAO_Importance_SampleImportanceB(uv + float2(+halfPixel.x * 3, -halfPixel.y));
	vals.z = FFX_CACAO_Importance_SampleImportanceB(uv + float2(+halfPixel.x, +halfPixel.y * 3));
	vals.w = FFX_CACAO_Importance_SampleImportanceB(uv + float2(-halfPixel.x * 3, +halfPixel.y));

	float avgVal = dot(vals, float4(0.25, 0.25, 0.25, 0.25));
	vals.xy = max(vals.xy, vals.zw);
	float maxVal = max(centre, max(vals.x, vals.y));

	float retVal = lerp(maxVal, avgVal, c_FFX_CACAO_SmoothenImportance);
	FFX_CACAO_Importance_StoreImportanceB(tid, retVal);

	// sum the average; to avoid overflowing we assume max AO resolution is not bigger than 16384x16384; so quarter res (used here) will be 4096x4096, which leaves us with 8 bits per pixel
	uint sum = (uint)(saturate(retVal) * 255.0 + 0.5);

	// save every 9th to avoid InterlockedAdd congestion - since we're blurring, this is good enough; compensated by multiplying LoadCounterAvgDiv by 9
	if (((tid.x % 3) + (tid.y % 3)) == 0)
	{
		FFX_CACAO_Importance_LoadCounterInterlockedAdd(sum);
	}
}

// =============================================================================
// Bilateral Upscale

uint FFX_CACAO_DoublePackFloat16(float v)
{
	uint2 p = f32tof16(float2(v, v));
	return p.x | (p.y << 16);
}

#define FFX_CACAO_BILATERAL_UPSCALE_BUFFER_WIDTH  (FFX_CACAO_BILATERAL_UPSCALE_WIDTH  + 4)
#define FFX_CACAO_BILATERAL_UPSCALE_BUFFER_HEIGHT (FFX_CACAO_BILATERAL_UPSCALE_HEIGHT + 4 + 4)

struct FFX_CACAO_BilateralBufferVal
{
	uint packedDepths;
	uint packedSsaoVals;
};

groupshared FFX_CACAO_BilateralBufferVal s_FFX_CACAO_BilateralUpscaleBuffer[FFX_CACAO_BILATERAL_UPSCALE_BUFFER_WIDTH][FFX_CACAO_BILATERAL_UPSCALE_BUFFER_HEIGHT];

void FFX_CACAO_BilateralUpscaleNxN(int2 tid, uint2 gtid, uint2 gid, const int width, const int height, const bool useEdges)
{
	// fill in group shared buffer
	{
		uint threadNum = (gtid.y * FFX_CACAO_BILATERAL_UPSCALE_WIDTH + gtid.x) * 3;
		uint2 bufferCoord = uint2(threadNum % FFX_CACAO_BILATERAL_UPSCALE_BUFFER_WIDTH, threadNum / FFX_CACAO_BILATERAL_UPSCALE_BUFFER_WIDTH);
		uint2 imageCoord = (gid * uint2(FFX_CACAO_BILATERAL_UPSCALE_WIDTH, FFX_CACAO_BILATERAL_UPSCALE_HEIGHT)) + bufferCoord - 2;

		if (useEdges)
		{
			float2 inputs[3];
			for (int j = 0; j < 3; ++j)
			{
				int2 p = int2(imageCoord.x + j, imageCoord.y);
				int2 pos = p / 2;
				int index = (p.x % 2) + 2 * (p.y % 2);
				inputs[j] = FFX_CACAO_BilateralUpscale_LoadSSAO(pos, index);
			}

			for (int i = 0; i < 3; ++i)
			{
				int mx = (imageCoord.x % 2);
				int my = (imageCoord.y % 2);

				int ic = mx + my * 2;       // center index
				int ih = (1 - mx) + my * 2;   // neighbouring, horizontal
				int iv = mx + (1 - my) * 2;   // neighbouring, vertical
				int id = (1 - mx) + (1 - my) * 2; // diagonal

				float2 centerVal = inputs[i];

				float ao = centerVal.x;

				float4 edgesLRTB = FFX_CACAO_UnpackEdges(centerVal.y);

				// convert index shifts to sampling offsets
				float fmx = (float)mx;
				float fmy = (float)my;

				// in case of an edge, push sampling offsets away from the edge (towards pixel center)
				float fmxe = (edgesLRTB.y - edgesLRTB.x);
				float fmye = (edgesLRTB.w - edgesLRTB.z);

				// calculate final sampling offsets and sample using bilinear filter
				float2 p = imageCoord;
				float2  uvH = (p + float2(fmx + fmxe - 0.5, 0.5 - fmy)) * 0.5 * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
				float   aoH = FFX_CACAO_BilateralUpscale_SampleSSAOLinear(uvH, ih);
				float2  uvV = (p + float2(0.5 - fmx, fmy - 0.5 + fmye)) * 0.5 * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
				float   aoV = FFX_CACAO_BilateralUpscale_SampleSSAOLinear(uvV, iv);
				float2  uvD = (p + float2(fmx - 0.5 + fmxe, fmy - 0.5 + fmye)) * 0.5 * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
				float   aoD = FFX_CACAO_BilateralUpscale_SampleSSAOLinear(uvD, id);

				// reduce weight for samples near edge - if the edge is on both sides, weight goes to 0
				float4 blendWeights;
				blendWeights.x = 1.0;
				blendWeights.y = (edgesLRTB.x + edgesLRTB.y) * 0.5;
				blendWeights.z = (edgesLRTB.z + edgesLRTB.w) * 0.5;
				blendWeights.w = (blendWeights.y + blendWeights.z) * 0.5;

				// calculate weighted average
				float blendWeightsSum = dot(blendWeights, float4(1.0, 1.0, 1.0, 1.0));
				ao = dot(float4(ao, aoH, aoV, aoD), blendWeights) / blendWeightsSum;

				++imageCoord.x;

				FFX_CACAO_BilateralBufferVal bufferVal;

				uint2 depthArrayBufferCoord = (imageCoord / 2) + g_FFX_CACAO_Consts.DeinterleavedDepthBufferOffset;
				uint depthArrayBufferIndex = ic;
				float depth = FFX_CACAO_BilateralUpscale_LoadDownscaledDepth(depthArrayBufferCoord, depthArrayBufferIndex);

				bufferVal.packedDepths = FFX_CACAO_DoublePackFloat16(depth);
				bufferVal.packedSsaoVals = FFX_CACAO_DoublePackFloat16(ao);

				s_FFX_CACAO_BilateralUpscaleBuffer[bufferCoord.x + i][bufferCoord.y] = bufferVal;
			}
		}
		else
		{
			for (int i = 0; i < 3; ++i)
			{
				float2 sampleLoc0 = (float2(imageCoord / 2) + 0.5f) * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
				float2 sampleLoc1 = sampleLoc0;
				float2 sampleLoc2 = sampleLoc0;
				float2 sampleLoc3 = sampleLoc0;
				switch ((imageCoord.y % 2) * 2 + (imageCoord.x % 2)) {
				case 0:
					sampleLoc1.x -= 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions.x;
					sampleLoc2.y -= 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions.y;
					sampleLoc3 -= 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
					break;
				case 1:
					sampleLoc0.x += 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions.x;
					sampleLoc2 += float2(0.5f, -0.5f) * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
					sampleLoc3.y -= 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions.y;
					break;
				case 2:
					sampleLoc0.y += 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions.y;
					sampleLoc1 += float2(-0.5f, 0.5f) * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
					sampleLoc3.x -= 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions.x;
					break;
				case 3:
					sampleLoc0 += 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
					sampleLoc1.y += 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions.y;
					sampleLoc2.x += 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions.x;
					break;
				}

				float ssaoVal0 = FFX_CACAO_BilateralUpscale_SampleSSAOPoint(sampleLoc0, 0);
				float ssaoVal1 = FFX_CACAO_BilateralUpscale_SampleSSAOPoint(sampleLoc1, 1);
				float ssaoVal2 = FFX_CACAO_BilateralUpscale_SampleSSAOPoint(sampleLoc2, 2);
				float ssaoVal3 = FFX_CACAO_BilateralUpscale_SampleSSAOPoint(sampleLoc3, 3);

				uint3 ssaoArrayBufferCoord = uint3(imageCoord / 2, 2 * (imageCoord.y % 2) + imageCoord.x % 2);
				uint2 depthArrayBufferCoord = ssaoArrayBufferCoord.xy + g_FFX_CACAO_Consts.DeinterleavedDepthBufferOffset;
				uint depthArrayBufferIndex = ssaoArrayBufferCoord.z;
				++imageCoord.x;

				FFX_CACAO_BilateralBufferVal bufferVal;

				float depth = FFX_CACAO_BilateralUpscale_LoadDownscaledDepth(depthArrayBufferCoord, depthArrayBufferIndex);
				float ssaoVal = (ssaoVal0 + ssaoVal1 + ssaoVal2 + ssaoVal3) * 0.25f;

				bufferVal.packedDepths = FFX_CACAO_DoublePackFloat16(depth);
				bufferVal.packedSsaoVals = FFX_CACAO_DoublePackFloat16(ssaoVal);

				s_FFX_CACAO_BilateralUpscaleBuffer[bufferCoord.x + i][bufferCoord.y] = bufferVal;
			}
		}
	}

	GroupMemoryBarrierWithGroupSync();

	float depths[4];
	// load depths
	{
		int2 fullBufferCoord = 2 * tid;
		int2 fullDepthBufferCoord = fullBufferCoord + g_FFX_CACAO_Consts.DepthBufferOffset;

		depths[0] = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_BilateralUpscale_LoadDepth(fullDepthBufferCoord, int2(0, 0)));
		depths[1] = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_BilateralUpscale_LoadDepth(fullDepthBufferCoord, int2(1, 0)));
		depths[2] = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_BilateralUpscale_LoadDepth(fullDepthBufferCoord, int2(0, 1)));
		depths[3] = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_BilateralUpscale_LoadDepth(fullDepthBufferCoord, int2(1, 1)));
	}
	min16float4 packedDepths = min16float4(depths[0], depths[1], depths[2], depths[3]);

	int2 baseBufferCoord = gtid + int2(width, height);

	min16float epsilonWeight = 1e-3f;
	min16float2 nearestSsaoVals = FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BilateralUpscaleBuffer[baseBufferCoord.x][baseBufferCoord.y].packedSsaoVals);
	min16float4 packedTotals = epsilonWeight * min16float4(1.0f, 1.0f, 1.0f, 1.0f);
	packedTotals.xy *= nearestSsaoVals;
	packedTotals.zw *= nearestSsaoVals;
	min16float4 packedTotalWeights = epsilonWeight * min16float4(1.0f, 1.0f, 1.0f, 1.0f);

	float distanceSigma = g_FFX_CACAO_Consts.BilateralSimilarityDistanceSigma;
	min16float2 packedDistSigma = min16float2(1.0f / distanceSigma, 1.0f / distanceSigma);
	float sigma = g_FFX_CACAO_Consts.BilateralSigmaSquared;
	min16float2 packedSigma = min16float2(1.0f / sigma, 1.0f / sigma);

	for (int x = -width; x <= width; ++x)
	{
		for (int y = -height; y <= height; ++y)
		{
			int2 bufferCoord = baseBufferCoord + int2(x, y);

			FFX_CACAO_BilateralBufferVal bufferVal = s_FFX_CACAO_BilateralUpscaleBuffer[bufferCoord.x][bufferCoord.y];

			min16float2 u = min16float2(x, x) - min16float2(0.0f, 0.5f);
			min16float2 v1 = min16float2(y, y) - min16float2(0.0f, 0.0f);
			min16float2 v2 = min16float2(y, y) - min16float2(0.5f, 0.5f);
			u = u * u;
			v1 = v1 * v1;
			v2 = v2 * v2;

			min16float2 dist1 = u + v1;
			min16float2 dist2 = u + v2;

			min16float2 wx1 = exp(-dist1 * packedSigma);
			min16float2 wx2 = exp(-dist2 * packedSigma);

			min16float2 bufferPackedDepths = FFX_CACAO_UnpackFloat16(bufferVal.packedDepths);

#if 0
			min16float2 diff1 = abs(packedDepths.xy - bufferPackedDepths);
			min16float2 diff2 = abs(packedDepths.zw - bufferPackedDepths);
#else
			min16float2 diff1 = packedDepths.xy - bufferPackedDepths;
			min16float2 diff2 = packedDepths.zw - bufferPackedDepths;
			diff1 *= diff1;
			diff2 *= diff2;
#endif

			min16float2 wy1 = exp(-diff1 * packedDistSigma);
			min16float2 wy2 = exp(-diff2 * packedDistSigma);

			min16float2 weight1 = wx1 * wy1;
			min16float2 weight2 = wx2 * wy2;

			min16float2 packedSsaoVals = FFX_CACAO_UnpackFloat16(bufferVal.packedSsaoVals);
			packedTotals.xy += packedSsaoVals * weight1;
			packedTotals.zw += packedSsaoVals * weight2;
			packedTotalWeights.xy += weight1;
			packedTotalWeights.zw += weight2;
		}
	}

	uint2 outputCoord = 2 * tid;
	min16float4 outputValues = packedTotals / packedTotalWeights;
	FFX_CACAO_BilateralUpscale_StoreOutput(outputCoord, int2(0, 0), outputValues.x); // totals[0] / totalWeights[0];
	FFX_CACAO_BilateralUpscale_StoreOutput(outputCoord, int2(1, 0), outputValues.y); // totals[1] / totalWeights[1];
	FFX_CACAO_BilateralUpscale_StoreOutput(outputCoord, int2(0, 1), outputValues.z); // totals[2] / totalWeights[2];
	FFX_CACAO_BilateralUpscale_StoreOutput(outputCoord, int2(1, 1), outputValues.w); // totals[3] / totalWeights[3];
}

[numthreads(FFX_CACAO_BILATERAL_UPSCALE_WIDTH, FFX_CACAO_BILATERAL_UPSCALE_HEIGHT, 1)]
void FFX_CACAO_UpscaleBilateral5x5Smart(int2 tid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	FFX_CACAO_BilateralUpscaleNxN(tid, gtid, gid, 2, 2, true);
}

[numthreads(FFX_CACAO_BILATERAL_UPSCALE_WIDTH, FFX_CACAO_BILATERAL_UPSCALE_HEIGHT, 1)]
void FFX_CACAO_UpscaleBilateral5x5NonSmart(int2 tid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	FFX_CACAO_BilateralUpscaleNxN(tid, gtid, gid, 2, 2, false);
}

[numthreads(FFX_CACAO_BILATERAL_UPSCALE_WIDTH, FFX_CACAO_BILATERAL_UPSCALE_HEIGHT, 1)]
void FFX_CACAO_UpscaleBilateral7x7(int2 tid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	FFX_CACAO_BilateralUpscaleNxN(tid, gtid, gid, 3, 3, true);
}

[numthreads(FFX_CACAO_BILATERAL_UPSCALE_WIDTH, FFX_CACAO_BILATERAL_UPSCALE_HEIGHT, 1)]
void FFX_CACAO_UpscaleBilateral5x5Half(int2 tid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	const int width = 2, height = 2;

	// fill in group shared buffer
	{
		uint threadNum = (gtid.y * FFX_CACAO_BILATERAL_UPSCALE_WIDTH + gtid.x) * 3;
		uint2 bufferCoord = uint2(threadNum % FFX_CACAO_BILATERAL_UPSCALE_BUFFER_WIDTH, threadNum / FFX_CACAO_BILATERAL_UPSCALE_BUFFER_WIDTH);
		uint2 imageCoord = (gid * uint2(FFX_CACAO_BILATERAL_UPSCALE_WIDTH, FFX_CACAO_BILATERAL_UPSCALE_HEIGHT)) + bufferCoord - 2;

		for (int i = 0; i < 3; ++i)
		{
			float2 sampleLoc0 = (float2(imageCoord / 2) + 0.5f) * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
			float2 sampleLoc1 = sampleLoc0;
			switch ((imageCoord.y % 2) * 2 + (imageCoord.x % 2)) {
			case 0:
				sampleLoc1 -= 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
				break;
			case 1:
				sampleLoc0.x += 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions.x;
				sampleLoc1.y -= 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions.y;
				break;
			case 2:
				sampleLoc0.y += 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions.y;
				sampleLoc1.x -= 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions.x;
				break;
			case 3:
				sampleLoc0 += 0.5f * g_FFX_CACAO_Consts.SSAOBufferInverseDimensions;
				break;
			}

			float ssaoVal0 = FFX_CACAO_BilateralUpscale_SampleSSAOPoint(sampleLoc0, 0);
			float ssaoVal1 = FFX_CACAO_BilateralUpscale_SampleSSAOPoint(sampleLoc1, 3);

			uint2 depthArrayBufferCoord = (imageCoord / 2) + g_FFX_CACAO_Consts.DeinterleavedDepthBufferOffset;
			uint depthArrayBufferIndex = (imageCoord.y % 2) * 3;
			++imageCoord.x;

			FFX_CACAO_BilateralBufferVal bufferVal;

			float depth = FFX_CACAO_BilateralUpscale_LoadDownscaledDepth(depthArrayBufferCoord, depthArrayBufferIndex);
			float ssaoVal = (ssaoVal0 + ssaoVal1) * 0.5f;

			bufferVal.packedDepths = FFX_CACAO_DoublePackFloat16(depth);
			bufferVal.packedSsaoVals = FFX_CACAO_DoublePackFloat16(ssaoVal);

			s_FFX_CACAO_BilateralUpscaleBuffer[bufferCoord.x + i][bufferCoord.y] = bufferVal;
		}
	}

	GroupMemoryBarrierWithGroupSync();

	float depths[4];
	// load depths
	{
		int2 fullBufferCoord = 2 * tid;
		int2 fullDepthBufferCoord = fullBufferCoord + g_FFX_CACAO_Consts.DepthBufferOffset;

		depths[0] = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_BilateralUpscale_LoadDepth(fullDepthBufferCoord, int2(0, 0)));
		depths[1] = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_BilateralUpscale_LoadDepth(fullDepthBufferCoord, int2(1, 0)));
		depths[2] = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_BilateralUpscale_LoadDepth(fullDepthBufferCoord, int2(0, 1)));
		depths[3] = FFX_CACAO_ScreenSpaceToViewSpaceDepth(FFX_CACAO_BilateralUpscale_LoadDepth(fullDepthBufferCoord, int2(1, 1)));
	}
	min16float4 packedDepths = min16float4(depths[0], depths[1], depths[2], depths[3]);

	int2 baseBufferCoord = gtid + int2(width, height);

	min16float epsilonWeight = 1e-3f;
	min16float2 nearestSsaoVals = FFX_CACAO_UnpackFloat16(s_FFX_CACAO_BilateralUpscaleBuffer[baseBufferCoord.x][baseBufferCoord.y].packedSsaoVals);
	min16float4 packedTotals = epsilonWeight * min16float4(1.0f, 1.0f, 1.0f, 1.0f);
	packedTotals.xy *= nearestSsaoVals;
	packedTotals.zw *= nearestSsaoVals;
	min16float4 packedTotalWeights = epsilonWeight * min16float4(1.0f, 1.0f, 1.0f, 1.0f);

	float distanceSigma = g_FFX_CACAO_Consts.BilateralSimilarityDistanceSigma;
	min16float2 packedDistSigma = min16float2(1.0f / distanceSigma, 1.0f / distanceSigma);
	float sigma = g_FFX_CACAO_Consts.BilateralSigmaSquared;
	min16float2 packedSigma = min16float2(1.0f / sigma, 1.0f / sigma);

	for (int x = -width; x <= width; ++x)
	{
		for (int y = -height; y <= height; ++y)
		{
			int2 bufferCoord = baseBufferCoord + int2(x, y);

			FFX_CACAO_BilateralBufferVal bufferVal = s_FFX_CACAO_BilateralUpscaleBuffer[bufferCoord.x][bufferCoord.y];

			min16float2 u = min16float2(x, x) - min16float2(0.0f, 0.5f);
			min16float2 v1 = min16float2(y, y) - min16float2(0.0f, 0.0f);
			min16float2 v2 = min16float2(y, y) - min16float2(0.5f, 0.5f);
			u = u * u;
			v1 = v1 * v1;
			v2 = v2 * v2;

			min16float2 dist1 = u + v1;
			min16float2 dist2 = u + v2;

			min16float2 wx1 = exp(-dist1 * packedSigma);
			min16float2 wx2 = exp(-dist2 * packedSigma);

			min16float2 bufferPackedDepths = FFX_CACAO_UnpackFloat16(bufferVal.packedDepths);

#if 0
			min16float2 diff1 = abs(packedDepths.xy - bufferPackedDepths);
			min16float2 diff2 = abs(packedDepths.zw - bufferPackedDepths);
#else
			min16float2 diff1 = packedDepths.xy - bufferPackedDepths;
			min16float2 diff2 = packedDepths.zw - bufferPackedDepths;
			diff1 *= diff1;
			diff2 *= diff2;
#endif

			min16float2 wy1 = exp(-diff1 * packedDistSigma);
			min16float2 wy2 = exp(-diff2 * packedDistSigma);

			min16float2 weight1 = wx1 * wy1;
			min16float2 weight2 = wx2 * wy2;

			min16float2 packedSsaoVals = FFX_CACAO_UnpackFloat16(bufferVal.packedSsaoVals);
			packedTotals.xy += packedSsaoVals * weight1;
			packedTotals.zw += packedSsaoVals * weight2;
			packedTotalWeights.xy += weight1;
			packedTotalWeights.zw += weight2;
		}
	}

	uint2 outputCoord = 2 * tid;
	min16float4 outputValues = packedTotals / packedTotalWeights;
	FFX_CACAO_BilateralUpscale_StoreOutput(outputCoord, int2(0, 0), outputValues.x); // totals[0] / totalWeights[0];
	FFX_CACAO_BilateralUpscale_StoreOutput(outputCoord, int2(1, 0), outputValues.y); // totals[1] / totalWeights[1];
	FFX_CACAO_BilateralUpscale_StoreOutput(outputCoord, int2(0, 1), outputValues.z); // totals[2] / totalWeights[2];
	FFX_CACAO_BilateralUpscale_StoreOutput(outputCoord, int2(1, 1), outputValues.w); // totals[3] / totalWeights[3];
}


#undef FFX_CACAO_BILATERAL_UPSCALE_BUFFER_WIDTH
#undef FFX_CACAO_BILATERAL_UPSCALE_BUFFER_HEIGHT
