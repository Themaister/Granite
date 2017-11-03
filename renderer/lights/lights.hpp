/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "abstract_renderable.hpp"
#include "image.hpp"

namespace Granite
{
class PositionalLight : public AbstractRenderable
{
public:
	enum class Type
	{
		Spot,
		Point
	};

	PositionalLight(Type type);

	Type get_type() const
	{
		return type;
	}

	void set_maximum_range(float range);

	bool has_static_aabb() const override
	{
		return true;
	}

	const AABB *get_static_aabb() const override
	{
		return &aabb;
	}

	void set_color(vec3 color);
	const vec3 &get_color() const
	{
		return color;
	}

	void set_falloff(float constant, float linear, float quadratic);

	unsigned get_cookie() const
	{
		return cookie;
	}

	float get_constant_falloff() const
	{
		return constant;
	}

	float get_linear_falloff() const
	{
		return linear;
	}

	float get_quadratic_falloff() const
	{
		return quadratic;
	}

protected:
	AABB aabb;
	vec3 color = vec3(1.0f);
	float falloff_range = 1.0f;
	float cutoff_range = 100.0f;
	void recompute_range();

	float constant = 0.0f;
	float linear = 0.0f;
	float quadratic = 0.0f;

private:
	Type type;
	unsigned cookie;
	virtual void set_range(float range) = 0;
};

struct PositionalFragmentInfo
{
	vec4 color_outer;
	vec4 falloff_inv_radius;
	vec4 position_inner;
	vec4 direction_half_angle;
};

class SpotLight : public PositionalLight
{
public:
	SpotLight();

	void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
	                     RenderQueue &queue) const override;

	void set_spot_parameters(float inner_cone, float outer_cone);
	PositionalFragmentInfo get_shader_info(const mat4 &transform) const;

	void set_shadow_info(const Vulkan::ImageView *shadow, const mat4 &transform);

	float get_inner_cone() const
	{
		return inner_cone;
	}

	float get_outer_cone() const
	{
		return outer_cone;
	}

private:
	float inner_cone = 0.4f;
	float outer_cone = 0.45f;
	float xy_range = 0.0f;

	const Vulkan::ImageView *atlas = nullptr;
	mat4 shadow_transform;

	void set_range(float range) override;
};

class PointLight : public PositionalLight
{
public:
	PointLight();

	void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
	                     RenderQueue &queue) const override;
	PositionalFragmentInfo get_shader_info(const mat4 &transform) const;

	void set_shadow_info(const Vulkan::ImageView *shadow, const vec4 &transform, unsigned slice);

private:
	void set_range(float range) override;
	const Vulkan::ImageView *shadow_atlas = nullptr;
	vec4 shadow_transform;
	unsigned shadow_slice;
};
}
