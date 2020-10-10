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

#include "abstract_renderable.hpp"
#include "image.hpp"
#include "light_info.hpp"

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
	float get_maximum_range() const
	{
		return cutoff_range;
	}

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

	unsigned get_cookie() const
	{
		return cookie;
	}

	virtual vec2 get_z_range(const RenderContext &context, const mat4 &transform) const = 0;

	Util::Hash get_shadow_transform_hash() const
	{
		return shadow_hash;
	}

	void set_shadow_transform_hash(Util::Hash hash)
	{
		shadow_hash = hash;
	}

protected:
	AABB aabb;
	Util::Hash shadow_hash = ~Util::Hash(0);
	vec3 color = vec3(1.0f);
	float falloff_range = 1.0f;
	float cutoff_range = 100.0f;
	void recompute_range();

private:
	Type type;
	unsigned cookie;
	virtual void set_range(float range) = 0;
};

class SpotLight : public PositionalLight
{
public:
	SpotLight();

	void get_render_info(const RenderContext &context, const RenderInfoComponent *transform,
	                     RenderQueue &queue) const override;
	void get_depth_render_info(const RenderContext &context, const RenderInfoComponent *transform,
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

	float get_xy_range() const
	{
		return xy_range;
	}

	mat4 build_model_matrix(const mat4 &transform) const;

private:
	float inner_cone = 0.4f;
	float outer_cone = 0.45f;
	float xy_range = 0.0f;

	const Vulkan::ImageView *atlas = nullptr;
	mat4 shadow_transform;

	void set_range(float range) override;
	vec2 get_z_range(const RenderContext &context, const mat4 &transform) const override final;
};

class PointLight : public PositionalLight
{
public:
	PointLight();

	void get_render_info(const RenderContext &context, const RenderInfoComponent *transform,
	                     RenderQueue &queue) const override;
	void get_depth_render_info(const RenderContext &context, const RenderInfoComponent *transform,
	                           RenderQueue &queue) const override;
	PositionalFragmentInfo get_shader_info(const mat4 &transform) const;

	void set_shadow_info(const Vulkan::ImageView *shadow, const PointTransform &transform);

private:
	void set_range(float range) override;
	vec2 get_z_range(const RenderContext &context, const mat4 &transform) const override final;

	const Vulkan::ImageView *shadow_atlas = nullptr;
	PointTransform shadow_transform;
};

vec2 spot_light_z_range(const RenderContext &context, const mat4 &model);
vec2 point_light_z_range(const RenderContext &context, const vec3 &pos, float radius);
}
