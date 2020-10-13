/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "math.hpp"
#include "image.hpp"
#include "lights/light_info.hpp"
#include "limits.hpp"

namespace Granite
{
class LightClusterer;
class VolumetricFog;
enum { NumShadowCascades = 4 };

struct RenderParameters
{
	mat4 projection;
	mat4 view;
	mat4 view_projection;
	mat4 inv_projection;
	mat4 inv_view;
	mat4 inv_view_projection;
	mat4 local_view_projection;
	mat4 inv_local_view_projection;

	mat4 multiview_view_projection[NumShadowCascades];

	alignas(16) vec3 camera_position;
	alignas(16) vec3 camera_front;
	alignas(16) vec3 camera_right;
	alignas(16) vec3 camera_up;

	float z_near;
	float z_far;
};

struct ResolutionParameters
{
	alignas(8) vec2 resolution;
	alignas(8) vec2 inv_resolution;
};

struct VolumetricFogParameters
{
	float slice_z_log2_scale;
};

struct FogParameters
{
	alignas(16) vec3 color;
	float falloff;
};

struct DirectionalParameters
{
	alignas(16) vec3 color;
	alignas(16) vec3 direction;
};

struct ShadowParameters
{
	alignas(16) mat4 transforms[NumShadowCascades];
	float cascade_log_bias;
};

struct EnvironmentParameters
{
	float intensity;
	float mipscale;
};

struct RefractionParameters
{
	alignas(16) vec3 falloff;
};

#define CLUSTERER_MAX_LIGHTS 32
struct ClustererParametersLegacy
{
	mat4 transform;
	PositionalFragmentInfo spots[CLUSTERER_MAX_LIGHTS];
	PositionalFragmentInfo points[CLUSTERER_MAX_LIGHTS];
	mat4 spot_shadow_transforms[CLUSTERER_MAX_LIGHTS];
	PointTransform point_shadow[CLUSTERER_MAX_LIGHTS];
};

struct ClustererParametersBindless
{
	alignas(16) mat4 transform;
	alignas(16) vec4 clip_scale;
	alignas(16) vec3 camera_base;
	alignas(16) vec3 camera_front;

	alignas(8) vec2 xy_scale;
	alignas(8) ivec2 resolution_xy;
	alignas(8) vec2 inv_resolution_xy;

	uint32_t num_lights;
	uint32_t num_lights_32;
	uint32_t z_max_index;
	float z_scale;
};

#define CLUSTERER_MAX_LIGHTS_BINDLESS 4096
struct ClustererBindlessTransforms
{
	PositionalFragmentInfo lights[CLUSTERER_MAX_LIGHTS_BINDLESS];
	mat4 shadow[CLUSTERER_MAX_LIGHTS_BINDLESS];
	mat4 model[CLUSTERER_MAX_LIGHTS_BINDLESS];
	uint32_t type_mask[CLUSTERER_MAX_LIGHTS_BINDLESS / 32];
};

struct CombinedRenderParameters
{
	alignas(16) FogParameters fog;
	alignas(16) EnvironmentParameters environment;
	alignas(16) ShadowParameters shadow;
	alignas(16) VolumetricFogParameters volumetric_fog;
	alignas(16) DirectionalParameters directional;
	alignas(16) RefractionParameters refraction;
	alignas(16) ResolutionParameters resolution;
	alignas(16) ClustererParametersLegacy clusterer;
};
static_assert(sizeof(CombinedRenderParameters) <= Vulkan::VULKAN_MAX_UBO_SIZE, "CombinedRenderParameters cannot fit in min-spec.");

struct LightingParameters
{
	FogParameters fog = {};
	DirectionalParameters directional;
	ShadowParameters shadow;
	EnvironmentParameters environment;
	RefractionParameters refraction;

	Vulkan::ImageView *environment_radiance = nullptr;
	Vulkan::ImageView *environment_irradiance = nullptr;
	Vulkan::ImageView *shadows = nullptr;
	Vulkan::ImageView *ambient_occlusion = nullptr;
	const LightClusterer *cluster = nullptr;
	const VolumetricFog *volumetric_fog = nullptr;
};
}