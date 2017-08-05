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

#include "vulkan_events.hpp"
#include "scene.hpp"
#include "shader_suite.hpp"
#include "renderer_enums.hpp"

namespace Granite
{
struct Sprite;

class Renderer : public EventHandler
{
public:
	Renderer(RendererType type = RendererType::GeneralDeferred);

	enum RendererOptionBits
	{
		SHADOW_ENABLE_BIT = 1 << 0,
		SHADOW_CASCADE_ENABLE_BIT = 1 << 1,
		FOG_ENABLE_BIT = 1 << 2,
		ENVIRONMENT_ENABLE_BIT = 1 << 3,
		REFRACTION_ENABLE_BIT = 1 << 4
	};
	using RendererOptionFlags = uint32_t;

	void set_mesh_renderer_options(RendererOptionFlags flags);

	void begin();

	void push_renderables(RenderContext &context, const VisibilityList &visible);

	void flush(Vulkan::CommandBuffer &cmd, RenderContext &context);

	void render_debug_aabb(RenderContext &context, const AABB &aabb, const vec4 &color);

	void render_debug_frustum(RenderContext &context, const Frustum &frustum, const vec4 &color);

	RenderQueue &get_render_queue()
	{
		return queue;
	}

	RendererType get_renderer_type() const
	{
		return type;
	}

private:
	void on_device_created(const Event &e);

	void on_device_destroyed(const Event &e);

	Vulkan::Device *device = nullptr;
	RenderQueue queue;
	ShaderSuite suite[Util::ecast(RenderableType::Count)];

	DebugMeshInstanceInfo &render_debug(RenderContext &context, unsigned count);

	RendererType type;
	uint32_t renderer_options = ~0u;

	void set_lighting_parameters(Vulkan::CommandBuffer &cmd, const RenderContext &context);
};

class DeferredLightRenderer
{
public:
	static void render_light(Vulkan::CommandBuffer &cmd, RenderContext &context);
};
}
