/* Copyright (c) 2017-2026 Hans-Kristian Arntzen
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

#define NOMINMAX
#include "lights.hpp"
#include "render_queue.hpp"
#include "render_context.hpp"
#include "shader_suite.hpp"
#include "device.hpp"
#include "mesh_util.hpp"
#include "clusterer.hpp"
#include "quirks.hpp"
#include "muglm/matrix_helper.hpp"
#include "common_renderer_data.hpp"
#include "simd.hpp"
#include <atomic>
#include <float.h>

using namespace Vulkan;
using namespace Util;

namespace Granite
{
static std::atomic_uint light_cookie_count;

PositionalLight::PositionalLight(Type type_)
	: type(type_)
{
	cookie = light_cookie_count.fetch_add(1, std::memory_order_relaxed) + 1;
}

void PositionalLight::set_color(vec3 color_)
{
	color = color_;
	recompute_range();
}

void PositionalLight::set_maximum_range(float range)
{
	cutoff_range = range;
	recompute_range();
}

void PositionalLight::recompute_range()
{
	// Check when attenuation drops below a constant.
	const float target_atten = 0.1f;
	float max_color = max(max(color.x, color.y), color.z);
	float d = muglm::sqrt(max_color / target_atten);
	set_range(d);
}

void SpotLight::set_spot_parameters(float inner_cone_, float outer_cone_)
{
	inner_cone = clamp(inner_cone_, 0.001f, 1.0f);
	outer_cone = clamp(outer_cone_, 0.001f, 1.0f);
	recompute_range();
}

void SpotLight::set_range(float range)
{
	falloff_range = range;

	float max_range = min(falloff_range, cutoff_range);
	float min_z = -max_range;
	float xy = muglm::sqrt(1.0f - outer_cone * outer_cone) / outer_cone;
	xy_range = xy;
	xy *= max_range;
	aabb = AABB(vec3(-xy, -xy, min_z), vec3(xy, xy, 0.0f));
}

void SpotLight::set_shadow_info(const Vulkan::ImageView *shadow, const mat4 &transform)
{
	atlas = shadow;
	shadow_transform = transform;
}

mat_affine SpotLight::build_model_matrix(const mat_affine &transform) const
{
	float max_range = min(falloff_range, cutoff_range);
	mat_affine res;
	SIMD::mul(res, transform, scale_affine(vec3(xy_range * max_range, xy_range * max_range, max_range)));
	return res;
}

PositionalFragmentInfo SpotLight::get_shader_info(const mat_affine &transform) const
{
	// If the point light node has been scaled, renormalize this.
	// This assumes a uniform scale.
	float scale_factor = transform.get_uniform_scale();

	// This assumes a uniform scale.
	float max_range = min(falloff_range, cutoff_range) * scale_factor;

	float spot_scale = 1.0f / max(0.001f, inner_cone - outer_cone);
	float spot_bias = -outer_cone * spot_scale;

	// Simplified version of Frustum::get_bounding_sphere, where N = 0.
	// Compute tan^2(outer_angle) = sin^2(x) / cos^2(x) here.
	// x^2 == f^2 + (R - x)^2 =>
	// x^2 == F + R^2 - 2Rx + x^2 =>
	// x = (F + R^2) / 2R =>
	// x = 0.5 * (tan^2(angle) * R + R) =>
	// x = 0.5 * (tan^2(angle) + 1) * R
	float tan2 = (1.0f - outer_cone * outer_cone) / (outer_cone * outer_cone);
	float center_distance = ((tan2 + 1.0f) * max_range) * 0.5f;
	float spot_offset, spot_radius;
	if (center_distance < max_range)
	{
		spot_offset = center_distance;
		spot_radius = center_distance;
	}
	else
	{
		spot_offset = max_range;
		spot_radius = muglm::sqrt(tan2) * max_range;
	}

	return {
		color * (scale_factor * scale_factor),
		floatToHalf(vec2(spot_scale, spot_bias)),
		transform.get_translation(),
		floatToHalf(vec2(spot_offset, spot_radius)),
		normalize(transform.get_forward()),
		1.0f / max_range,
	};
}

SpotLight::SpotLight()
	: PositionalLight(PositionalLight::Type::Spot)
{
}

vec2 SpotLight::get_z_range(const RenderContext &context, const mat_affine &transform) const
{
	auto &params = context.get_render_parameters();
	float max_range = min(falloff_range, cutoff_range);
	mat_affine model;
	SIMD::mul(model, transform, scale_affine(vec3(xy_range * max_range, xy_range * max_range, max_range)));

	static const vec4 sample_points[] = {
		vec4(0.0f, 0.0f, 0.0f, 1.0f), // Cone origin
		vec4(-1.0f, -1.0f, -1.0f, 1.0f),
		vec4(+1.0f, -1.0f, -1.0f, 1.0f),
		vec4(-1.0f, +1.0f, -1.0f, 1.0f),
		vec4(+1.0f, +1.0f, -1.0f, 1.0f),
	};

	// This can be optimized quite a lot.
	vec2 range(FLT_MAX, -FLT_MAX);
	for (auto &s : sample_points)
	{
		vec4 pos;
		SIMD::mul(pos, model, s);
		float z = dot(pos.xyz() - params.camera_position, params.camera_front);
		range.x = muglm::min(range.x, z);
		range.y = muglm::max(range.y, z);
	}

	return range;
}

PointLight::PointLight()
	: PositionalLight(PositionalLight::Type::Point)
{
}

vec2 PointLight::get_z_range(const RenderContext &context, const mat_affine &transform) const
{
	float scale_factor = transform.get_uniform_scale();
	float max_range = 1.15f * min(falloff_range, cutoff_range) * scale_factor;
	float z = dot(transform.get_translation() - context.get_render_parameters().camera_position,
	              context.get_render_parameters().camera_front);
	return vec2(z - max_range, z + max_range);
}

void PointLight::set_range(float range)
{
	falloff_range = range;
	float max_range = 1.15f * min(falloff_range, cutoff_range); // Fudge factor used in vertex shader.
	aabb = AABB(vec3(-max_range), vec3(max_range));
}

PositionalFragmentInfo PointLight::get_shader_info(const mat_affine &transform) const
{
	// If the point light node has been scaled, renormalize this.
	// This assumes a uniform scale.
	float scale_factor = transform.get_uniform_scale();

	// This assumes a uniform scale.
	float max_range = min(falloff_range, cutoff_range) * scale_factor;

	return {
		color * (scale_factor * scale_factor),
		{},
		transform.get_translation(),
		floatToHalf(vec2(0.0f, max_range)),
		transform.get_forward(), // This shouldn't matter
		1.0f / max_range,
	};
}

void PointLight::set_shadow_info(const Vulkan::ImageView *shadow, const PointTransform &transform)
{
	shadow_atlas = shadow;
	shadow_transform = transform;
}

float VolumetricDiffuseLight::get_guard_band_factor()
{
	return 1.10f;
}

const AABB &VolumetricDiffuseLight::get_static_aabb()
{
	static AABB aabb(vec3(-0.5f * get_guard_band_factor()), vec3(0.5f * get_guard_band_factor()));
	return aabb;
}

const Vulkan::ImageView *VolumetricDiffuseLight::get_volume_view() const
{
	return volume ? &volume->get_view() : nullptr;
}

const Vulkan::ImageView *VolumetricDiffuseLight::get_prev_volume_view() const
{
	return prev_volume ? &prev_volume->get_view() : nullptr;
}

const Vulkan::ImageView *VolumetricDiffuseLight::get_accumulation_view(unsigned index) const
{
	if (index >= accums.size())
		return nullptr;
	return accums[index] ? &accums[index]->get_view() : nullptr;
}

void VolumetricDiffuseLight::swap_volumes()
{
	std::swap(volume, prev_volume);
}

void VolumetricDiffuseLight::set_volumes(Vulkan::ImageHandle vol, Vulkan::ImageHandle prev_vol)
{
	volume = std::move(vol);
	prev_volume = std::move(prev_vol);
}

void VolumetricDiffuseLight::set_accumulation_volumes(Util::SmallVector<Vulkan::ImageHandle> accums_)
{
	accums = std::move(accums_);
}

void VolumetricDiffuseLight::set_buffers(Vulkan::BufferHandle atomics_, Vulkan::BufferHandle worklist_)
{
	atomics = std::move(atomics_);
	worklist = std::move(worklist_);
}

const Vulkan::Buffer *VolumetricDiffuseLight::get_atomic_buffer() const
{
	return atomics.get();
}

const Vulkan::Buffer *VolumetricDiffuseLight::get_worklist_buffer() const
{
	return worklist.get();
}

const VolumetricDiffuseLight::GBuffer &VolumetricDiffuseLight::get_gbuffer() const
{
	return gbuffer;
}

void VolumetricDiffuseLight::set_probe_gbuffer(GBuffer gbuffer_)
{
	gbuffer = std::move(gbuffer_);
}

uvec3 VolumetricDiffuseLight::get_resolution() const
{
	return resolution;
}

void VolumetricDiffuseLight::set_resolution(uvec3 resolution_)
{
	resolution = resolution_;
}

VolumetricDiffuseLight::VolumetricDiffuseLight()
{
	EVENT_MANAGER_REGISTER_LATCH(VolumetricDiffuseLight, on_device_created, on_device_destroyed,
	                             Vulkan::DeviceCreatedEvent);
}

void VolumetricDiffuseLight::on_device_created(const Vulkan::DeviceCreatedEvent &)
{
}

void VolumetricDiffuseLight::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	volume.reset();
	prev_volume.reset();
	atomics.reset();
	worklist.reset();
	accums.clear();
	hemisphere_view.reset();
	hemisphere.reset();
	gbuffer = {};
}

vec2 point_light_z_range(const RenderContext &context, const vec3 &center, float radius)
{
	auto &pos = context.get_render_parameters().camera_position;
	auto &front = context.get_render_parameters().camera_front;

	float z = dot(center - pos, front);
	return vec2(z - radius, z + radius);
}

vec2 spot_light_z_range(const RenderContext &context, const mat_affine &model)
{
	auto &pos = context.get_render_parameters().camera_position;
	auto &front = context.get_render_parameters().camera_front;

	float lo = std::numeric_limits<float>::infinity();
	float hi = -lo;

	vec3 base_pos = model.get_translation();
	vec3 x_off = model.get_right();
	vec3 y_off = model.get_up();
	vec3 z_off = model.get_forward();

	vec3 z_base = base_pos + z_off;

	const vec3 world_pos[5] = {
		base_pos,
		z_base + x_off + y_off,
		z_base - x_off + y_off,
		z_base + x_off - y_off,
		z_base - x_off - y_off,
	};

	for (auto &p : world_pos)
	{
		float z = dot(p - pos, front);
		lo = muglm::min(z, lo);
		hi = muglm::max(z, hi);
	}

	return vec2(lo, hi);
}
}
