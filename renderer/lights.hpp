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

	void set_falloff(float constant, float linear, float quadratic);

protected:
	AABB aabb;
	vec3 color = vec3(1.0f);
	float range = 1.0f;
	void recompute_range();

	float constant = 0.0f;
	float linear = 0.0f;
	float quadratic = 0.0f;

private:
	Type type;
	float maximum_range = 100.0f;
	virtual void set_range(float range) = 0;
};

struct PositionalFragmentInfo
{
	vec4 color_outer;
	vec4 falloff_inv_radius;
	vec4 position_inner;
	vec4 direction_xy_scale;
};

class SpotLight : public PositionalLight
{
public:
	SpotLight();

	void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
	                     RenderQueue &queue) const override;

	void set_spot_parameters(float inner_cone, float outer_cone);
	PositionalFragmentInfo get_shader_info(const mat4 &transform) const;

private:
	float inner_cone = 0.4f;
	float outer_cone = 0.45f;
	float xy_range = 0.0f;

	void set_range(float range) override;
};

class PointLight : public PositionalLight
{
public:
	PointLight();

	void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
	                     RenderQueue &queue) const override;
	PositionalFragmentInfo get_shader_info(const mat4 &transform) const;

private:
	void set_range(float range) override;
};
}
