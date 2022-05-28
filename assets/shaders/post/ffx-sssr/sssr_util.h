#ifndef SSSR_UTIL_H_
#define SSSR_UTIL_H_

#extension GL_EXT_samplerless_texture_functions : require

layout(set = 3, binding = 0) uniform SSSRUBO
{
	mat4 view_projection;
	mat4 inv_view_projection;
	vec2 float_resolution;
	vec2 inv_resolution;
	uvec2 resolution;
	int max_lod;
	int frame;
	vec3 camera_position;
	uint resolution_1d;
} sssr;

float FFX_SSSR_LoadDepth(texture2D uDepth, ivec2 coord, int lod)
{
	return texelFetch(uDepth, coord, lod).x;
}

mediump vec3 FFX_SSSR_LoadWorldSpaceNormal(mediump texture2D uNormal, ivec2 coord)
{
	return texelFetch(uNormal, coord, 0).xyz * 2.0 - 1.0;
}

vec3 FFX_SSSR_ScreenSpaceToWorldSpace(vec3 ndc)
{
	vec4 world_clip = sssr.inv_view_projection * vec4(ndc, 1.0);
	return world_clip.xyz / world_clip.w;
}

const float M_PI = 3.1415628;

// From FidelityFX-SSSR.
/**********************************************************************
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/

// http://jcgt.org/published/0007/04/01/paper.pdf by Eric Heitz
// Input Ve: view direction
// Input alpha_x, alpha_y: roughness parameters
// Input U1, U2: uniform random numbers
// Output Ne: normal sampled with PDF D_Ve(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z
mediump vec3 SampleGGXVNDF(mediump vec3 Ve, mediump float alpha_x, mediump float alpha_y, mediump float U1, mediump float U2)
{
	// Section 3.2: transforming the view direction to the hemisphere configuration
	mediump vec3 Vh = normalize(vec3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));
	// Section 4.1: orthonormal basis (with special case if cross product is zero)
	mediump float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	mediump vec3 T1 = lensq > 0 ? vec3(-Vh.y, Vh.x, 0) * inversesqrt(lensq) : vec3(1, 0, 0);
	mediump vec3 T2 = cross(Vh, T1);
	// Section 4.2: parameterization of the projected area
	mediump float r = sqrt(U1);
	mediump float phi = 2.0 * M_PI * U2;
	mediump float t1 = r * cos(phi);
	mediump float t2 = r * sin(phi);
	mediump float s = 0.5 * (1.0 + Vh.z);
	t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;
	// Section 4.3: reprojection onto hemisphere
	mediump vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
	// Section 3.4: transforming the normal back to the ellipsoid configuration
	mediump vec3 Ne = normalize(vec3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0, Nh.z)));
	return Ne;
}

mediump vec3 Sample_GGX_VNDF_Ellipsoid(mediump vec3 Ve, mediump float alpha_x, mediump float alpha_y, mediump float U1, mediump float U2)
{
	return SampleGGXVNDF(Ve, alpha_x, alpha_y, U1, U2);
}

mediump vec3 Sample_GGX_VNDF_Hemisphere(mediump vec3 Ve, mediump float alpha, mediump float U1, mediump float U2)
{
	return Sample_GGX_VNDF_Ellipsoid(Ve, alpha, alpha, U1, U2);
}

mediump mat3 CreateTBN(vec3 N)
{
	mediump vec3 U;

	if (abs(N.z) > 0.0)
	{
		mediump float k = sqrt(N.y * N.y + N.z * N.z);
		U.x = 0.0; U.y = -N.z / k; U.z = N.y / k;
	}
	else
	{
		mediump float k = sqrt(N.x * N.x + N.y * N.y);
		U.x = N.y / k; U.y = -N.x / k; U.z = 0.0;
	}

	return mat3(U, cross(N, U), N);
}

mediump vec2 SampleRandomVector2D(mediump texture2DArray uNoise, ivec2 pixel)
{
	return texelFetch(uNoise, ivec3(pixel.xy & 127, sssr.frame), 0).xy;
}

mediump vec3 SampleReflectionVector(mediump texture2DArray uNoise,
		mediump vec3 view_direction, mediump vec3 normal,
		mediump float roughness, ivec2 dispatch_thread_id)
{
	mediump mat3 tbn_transform = CreateTBN(normal);
	mediump vec3 view_direction_tbn = -view_direction * tbn_transform;
	mediump vec2 u = SampleRandomVector2D(uNoise, dispatch_thread_id);
	mediump vec3 sampled_normal_tbn = Sample_GGX_VNDF_Hemisphere(view_direction_tbn, roughness, u.x, u.y);
	mediump vec3 reflected_direction_tbn = reflect(-view_direction_tbn, sampled_normal_tbn);
	// Transform reflected_direction back to the initial space.
	return tbn_transform * reflected_direction_tbn;
}

bool IsMirrorReflection(mediump float roughness)
{
	return roughness < 0.0001;
}

bool IsReflective(texture2D uDepth, uvec2 coord)
{
	float d = texelFetch(uDepth, ivec2(coord), 0).x;
	return d < 1.0;
}

bool IsGlossy(float roughness)
{
	const float roughness_threshold = 0.2;
	return roughness < roughness_threshold;
}

uint PackRay(uvec2 coord, bvec3 copies)
{
	return coord.x | (coord.y << 14u) |
			(uint(copies.x) << 28u) |
			(uint(copies.y) << 29u) |
			(uint(copies.z) << 30u);
}

uvec2 RayUnpack(uint word, out bvec3 copies)
{
	uvec2 coord;
	coord.x = bitfieldExtract(word, 0, 14);
	coord.y = bitfieldExtract(word, 14, 14);
	copies.x = bitfieldExtract(word, 28, 1) != 0;
	copies.y = bitfieldExtract(word, 29, 1) != 0;
	copies.z = bitfieldExtract(word, 30, 1) != 0;
	return coord;
}

uvec2 UnpackZOrder(uint local64)
{
	uvec2 local_2d = uvec2(0);
	local_2d.x += bitfieldExtract(local64, 0, 1) << 0;
	local_2d.x += bitfieldExtract(local64, 2, 1) << 1;
	local_2d.x += bitfieldExtract(local64, 4, 1) << 2;
	local_2d.y += bitfieldExtract(local64, 1, 1) << 0;
	local_2d.y += bitfieldExtract(local64, 3, 1) << 1;
	local_2d.y += bitfieldExtract(local64, 5, 1) << 2;
	return local_2d;
}

#endif