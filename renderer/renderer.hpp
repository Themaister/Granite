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

#include "application_wsi_events.hpp"
#include "shader_suite.hpp"
#include "renderer_enums.hpp"

namespace Granite
{
struct Sprite;
class LightClusterer;
class RenderQueue;
struct LightingParameters;
class Frustum;

class ShaderSuiteResolver
{
public:
	virtual ~ShaderSuiteResolver() = default;
	virtual void init_shader_suite(Vulkan::Device &device, ShaderSuite &suite,
	                               RendererType renderer, RenderableType drawable) const;
};

class RenderContextParameterBinder
{
public:
	virtual ~RenderContextParameterBinder() = default;
	virtual void bind_render_context_parameters(Vulkan::CommandBuffer &cmd, const RenderContext &context) = 0;
};

class Renderer : public EventHandler, public Util::IntrusivePtrEnabled<Renderer>
{
public:
	explicit Renderer(RendererType type, const ShaderSuiteResolver *resolver);
	virtual ~Renderer() = default;

	enum RendererOptionBits
	{
		SHADOW_ENABLE_BIT = 1 << 0,
		SHADOW_CASCADE_ENABLE_BIT = 1 << 1,
		FOG_ENABLE_BIT = 1 << 2,
		ENVIRONMENT_ENABLE_BIT = 1 << 3,
		REFRACTION_ENABLE_BIT = 1 << 4,
		POSITIONAL_LIGHT_ENABLE_BIT = 1 << 5,
		POSITIONAL_LIGHT_SHADOW_ENABLE_BIT = 1 << 6,
		POSITIONAL_LIGHT_CLUSTER_LIST_BIT = 1 << 7,
		SHADOW_VSM_BIT = 1 << 8,
		POSITIONAL_LIGHT_SHADOW_VSM_BIT = 1 << 9,
		SHADOW_PCF_KERNEL_WIDTH_3_BIT = 1 << 10,
		SHADOW_PCF_KERNEL_WIDTH_5_BIT = 1 << 11,
		VOLUMETRIC_FOG_ENABLE_BIT = 1 << 12,
		ALPHA_TEST_DISABLE_BIT = 1 << 13,
		POSITIONAL_LIGHT_CLUSTER_BINDLESS_BIT = 1 << 14,
		MULTIVIEW_BIT = 1 << 15,
		AMBIENT_OCCLUSION_BIT = 1 << 16
	};
	using RendererOptionFlags = uint32_t;

	enum RendererFlushBits
	{
		FRONT_FACE_CLOCKWISE_BIT = 1 << 0,
		DEPTH_BIAS_BIT = 1 << 1,
		DEPTH_STENCIL_READ_ONLY_BIT = 1 << 2,
		NO_COLOR_BIT = 1 << 3,
		BACKFACE_BIT = 1 << 4,
		STENCIL_WRITE_REFERENCE_BIT = 1 << 5,
		STENCIL_COMPARE_REFERENCE_BIT = 1 << 6,
		SKIP_SORTING_BIT = 1 << 7,
		DEPTH_TEST_INVERT_BIT = 1 << 8,
		DEPTH_TEST_EQUAL_BIT = 1 << 9
	};
	using RendererFlushFlags = uint32_t;

	void set_mesh_renderer_options(RendererOptionFlags flags);
	void set_mesh_renderer_options_from_lighting(const LightingParameters &params);

	static RendererOptionFlags get_mesh_renderer_options_from_lighting(const LightingParameters &params);
	static std::vector<std::pair<std::string, int>> build_defines_from_renderer_options(
			RendererType type, RendererOptionFlags flags);
	static void bind_global_parameters(Vulkan::CommandBuffer &cmd, const RenderContext &context);
	static void bind_lighting_parameters(Vulkan::CommandBuffer &cmd, const RenderContext &context);

	RendererOptionFlags get_mesh_renderer_options() const;

	void render_debug_aabb(RenderQueue &queue, const RenderContext &context, const AABB &aabb, const vec4 &color);
	void render_debug_frustum(RenderQueue &queue, const RenderContext &context, const Frustum &frustum, const vec4 &color);

	void begin(RenderQueue &queue) const;

	struct FlushParameters
	{
		struct
		{
			uint8_t compare_mask;
			uint8_t write_mask;
			uint8_t ref;
		} stencil;
	};
	void flush(Vulkan::CommandBuffer &cmd, RenderQueue &queue, const RenderContext &context, RendererFlushFlags options = 0, const FlushParameters *params = nullptr) const;
	// Multi-threaded dispatch from a queue.
	// queue is assumed to be sorted already.
	void flush_subset(Vulkan::CommandBuffer &cmd, const RenderQueue &queue, const RenderContext &context,
	                  RendererFlushFlags options, const FlushParameters *params, unsigned index, unsigned num_indices) const;

	RendererType get_renderer_type() const
	{
		return type;
	}

	void set_render_context_parameter_binder(RenderContextParameterBinder *binder);

	void promote_read_write_cache_to_read_only();

protected:
	mutable ShaderSuite suite[Util::ecast(RenderableType::Count)];

private:
	void on_device_created(const Vulkan::DeviceCreatedEvent &e);

	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);

	Vulkan::Device *device = nullptr;

	DebugMeshInstanceInfo &render_debug(RenderQueue &queue, const RenderContext &context, unsigned count);
	void setup_shader_suite(Vulkan::Device &device, RendererType type);

	RendererType type;
	const ShaderSuiteResolver *resolver = nullptr;
	RenderContextParameterBinder *render_context_parameter_binder = nullptr;
	uint32_t renderer_options = ~0u;

	void set_mesh_renderer_options_internal(RendererOptionFlags flags);
};
using RendererHandle = Util::IntrusivePtr<Renderer>;

class RendererSuite
{
public:
	enum class Type
	{
		ForwardOpaque = 0,
		ForwardTransparent,
		ShadowDepthDirectionalPCF,
		ShadowDepthPositionalPCF,
		ShadowDepthDirectionalVSM,
		ShadowDepthPositionalVSM,
		PrepassDepth,
		Deferred,
		Count
	};

	void set_renderer(Type type, RendererHandle handle);
	void set_default_renderers();

	struct Config
	{
		unsigned pcf_width = 1;
		bool directional_light_vsm = false;
		bool forward_z_prepass = false;
		bool cascaded_directional_shadows = false;
	};

	void update_mesh_rendering_options(const RenderContext &context, const Config &config);

	Renderer &get_renderer(Type type);
	const Renderer &get_renderer(Type type) const;
	void promote_read_write_cache_to_read_only();

private:
	RendererHandle handles[Util::ecast(Type::Count)];
};

class DeferredLightRenderer
{
public:
	static void render_light(Vulkan::CommandBuffer &cmd, const RenderContext &context, Renderer::RendererOptionFlags flags);
};
}
