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

#include "renderer.hpp"
#include "device.hpp"
#include "render_context.hpp"
#include "sprite.hpp"
#include "lights/clusterer.hpp"
#include "lights/volumetric_fog.hpp"
#include "render_parameters.hpp"
#include "global_managers.hpp"
#include "common_renderer_data.hpp"
#include <string.h>

using namespace Vulkan;
using namespace Util;
using namespace std;

enum GlobalDescriptorSetBindings
{
	BINDING_GLOBAL_TRANSFORM = 0,
	BINDING_GLOBAL_RENDER_PARAMETERS = 1,
	BINDING_GLOBAL_ENV_RADIANCE = 2,
	BINDING_GLOBAL_ENV_IRRADIANCE = 3,
	BINDING_GLOBAL_BRDF_TABLE = 4,
	BINDING_GLOBAL_DIRECTIONAL_SHADOW = 5,
	BINDING_GLOBAL_AMBIENT_OCCLUSION = 6,
	BINDING_GLOBAL_VOLUMETRIC_FOG = 7,

	BINDING_GLOBAL_CLUSTERER_PARAMETERS = 8,

	BINDING_GLOBAL_CLUSTER_IMAGE_LEGACY = 9,
	BINDING_GLOBAL_CLUSTER_SPOT_LEGACY = 10,
	BINDING_GLOBAL_CLUSTER_POINT_LEGACY = 11,
	BINDING_GLOBAL_CLUSTER_LIST_LEGACY = 12,

	BINDING_GLOBAL_CLUSTER_TRANSFORM = 9,
	BINDING_GLOBAL_CLUSTER_BITMASK = 10,
	BINDING_GLOBAL_CLUSTER_RANGE = 11
};

namespace Granite
{
void RendererSuite::set_renderer(Type type, RendererHandle handle)
{
	handles[Util::ecast(type)] = std::move(handle);
}

Renderer &RendererSuite::get_renderer(Type type)
{
	return *handles[Util::ecast(type)];
}

const Renderer &RendererSuite::get_renderer(Type type) const
{
	return *handles[Util::ecast(type)];
}

void RendererSuite::promote_read_write_cache_to_read_only()
{
	for (auto &renderer : handles)
		if (renderer)
			renderer->promote_read_write_cache_to_read_only();
}

void RendererSuite::set_default_renderers()
{
	set_renderer(Type::ForwardOpaque, Util::make_handle<Renderer>(RendererType::GeneralForward, nullptr));
	set_renderer(Type::ForwardTransparent, Util::make_handle<Renderer>(RendererType::GeneralForward, nullptr));
	set_renderer(Type::ShadowDepthPositionalPCF, Util::make_handle<Renderer>(RendererType::DepthOnly, nullptr));
	set_renderer(Type::ShadowDepthDirectionalPCF, Util::make_handle<Renderer>(RendererType::DepthOnly, nullptr));
	set_renderer(Type::ShadowDepthDirectionalVSM, Util::make_handle<Renderer>(RendererType::DepthOnly, nullptr));
	set_renderer(Type::ShadowDepthPositionalVSM, Util::make_handle<Renderer>(RendererType::DepthOnly, nullptr));
	set_renderer(Type::PrepassDepth, Util::make_handle<Renderer>(RendererType::DepthOnly, nullptr));
	set_renderer(Type::Deferred, Util::make_handle<Renderer>(RendererType::GeneralDeferred, nullptr));
}

void RendererSuite::update_mesh_rendering_options(const RenderContext &context, const Config &config)
{
	get_renderer(Type::ShadowDepthDirectionalPCF).set_mesh_renderer_options(
			config.cascaded_directional_shadows ? Renderer::MULTIVIEW_BIT : 0);
	get_renderer(Type::ShadowDepthPositionalPCF).set_mesh_renderer_options(0);
	get_renderer(Type::ShadowDepthDirectionalVSM).set_mesh_renderer_options(
			(config.cascaded_directional_shadows ? Renderer::MULTIVIEW_BIT : 0) | Renderer::SHADOW_VSM_BIT);
	get_renderer(Type::ShadowDepthPositionalVSM).set_mesh_renderer_options(
			Renderer::POSITIONAL_LIGHT_SHADOW_VSM_BIT);
	get_renderer(Type::PrepassDepth).set_mesh_renderer_options(0);

	Renderer::RendererOptionFlags pcf_flags = 0;
	if (config.pcf_width == 5)
		pcf_flags |= Renderer::SHADOW_PCF_KERNEL_WIDTH_5_BIT;
	else if (config.pcf_width == 3)
		pcf_flags |= Renderer::SHADOW_PCF_KERNEL_WIDTH_3_BIT;

	get_renderer(Type::Deferred).set_mesh_renderer_options(pcf_flags);

	auto opts = Renderer::get_mesh_renderer_options_from_lighting(*context.get_lighting_parameters());
	get_renderer(Type::ForwardOpaque).set_mesh_renderer_options(
			opts | pcf_flags | (config.forward_z_prepass ? Renderer::ALPHA_TEST_DISABLE_BIT : 0));
	opts &= ~Renderer::AMBIENT_OCCLUSION_BIT;
	get_renderer(Type::ForwardTransparent).set_mesh_renderer_options(opts | pcf_flags);
}

Renderer::Renderer(RendererType type_, const ShaderSuiteResolver *resolver_)
	: type(type_), resolver(resolver_)
{
	EVENT_MANAGER_REGISTER_LATCH(Renderer, on_device_created, on_device_destroyed, DeviceCreatedEvent);

	if (type == RendererType::GeneralDeferred || type == RendererType::GeneralForward)
		set_mesh_renderer_options(SHADOW_CASCADE_ENABLE_BIT | SHADOW_ENABLE_BIT | FOG_ENABLE_BIT | ENVIRONMENT_ENABLE_BIT);
	else
		set_mesh_renderer_options(0);
}

static const char *renderer_to_define(RendererType type)
{
	switch (type)
	{
	case RendererType::GeneralForward:
		return "RENDERER_FORWARD";

	case RendererType::GeneralDeferred:
		return "RENDERER_DEFERRED";

	case RendererType::DepthOnly:
		return "RENDERER_DEPTH";

	default:
		break;
	}

	return "";
}

void Renderer::set_mesh_renderer_options_internal(RendererOptionFlags flags)
{
	auto global_defines = build_defines_from_renderer_options(type, flags);

	if (device)
	{
		// Safe early-discard.
		if (device->get_device_features().demote_to_helper_invocation_features.shaderDemoteToHelperInvocation)
			global_defines.emplace_back("DEMOTE", 1);

		// Used for early-kill alpha testing if demote_to_helper isn't available.
		auto &subgroup = device->get_device_features().subgroup_properties;
		if ((subgroup.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) != 0 &&
		    !ImplementationQuirks::get().force_no_subgroups &&
		    subgroup.subgroupSize >= 4)
		{
			if ((subgroup.supportedOperations & VK_SUBGROUP_FEATURE_BASIC_BIT) != 0)
				global_defines.emplace_back("SUBGROUP_BASIC", 1);

			if ((subgroup.supportedOperations & VK_SUBGROUP_FEATURE_CLUSTERED_BIT) != 0)
				global_defines.emplace_back("SUBGROUP_CLUSTERED", 1);
			if ((subgroup.supportedOperations & VK_SUBGROUP_FEATURE_QUAD_BIT) != 0)
				global_defines.emplace_back("SUBGROUP_QUAD", 1);
			if ((subgroup.supportedOperations & VK_SUBGROUP_FEATURE_BALLOT_BIT) != 0)
				global_defines.emplace_back("SUBGROUP_BALLOT", 1);
			if ((subgroup.supportedOperations & VK_SUBGROUP_FEATURE_VOTE_BIT) != 0)
				global_defines.emplace_back("SUBGROUP_VOTE", 1);
			if ((subgroup.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) != 0)
				global_defines.emplace_back("SUBGROUP_ARITHMETIC", 1);

			if (flags & POSITIONAL_LIGHT_ENABLE_BIT)
			{
				// Try to enable wave-optimizations.
				static const VkSubgroupFeatureFlags required_subgroup =
						VK_SUBGROUP_FEATURE_BALLOT_BIT |
						VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;

				if ((subgroup.supportedOperations & required_subgroup) == required_subgroup)
					global_defines.emplace_back("CLUSTERING_WAVE_UNIFORM", 1);
			}
		}
	}

	auto &meshes = suite[ecast(RenderableType::Mesh)];
	meshes.get_base_defines() = global_defines;
	meshes.bake_base_defines();
	auto &ground = suite[ecast(RenderableType::Ground)];
	ground.get_base_defines() = global_defines;
	ground.bake_base_defines();
	auto &ocean = suite[ecast(RenderableType::Ocean)];
	ocean.get_base_defines() = global_defines;
	ocean.bake_base_defines();
	auto &plane = suite[ecast(RenderableType::TexturePlane)];
	plane.get_base_defines() = global_defines;
	plane.bake_base_defines();
	auto &spot = suite[ecast(RenderableType::SpotLight)];
	spot.get_base_defines() = global_defines;
	spot.bake_base_defines();
	auto &point = suite[ecast(RenderableType::PointLight)];
	point.get_base_defines() = global_defines;
	point.bake_base_defines();

	// Skybox renderers only depend on VOLUMETRIC_FOG.
	ShaderSuite *suites[] = {
		&suite[ecast(RenderableType::Skybox)],
		&suite[ecast(RenderableType::SkyCylinder)],
	};

	for (auto *shader_suite : suites)
	{
		shader_suite->get_base_defines().clear();
		if (flags & VOLUMETRIC_FOG_ENABLE_BIT)
			shader_suite->get_base_defines().emplace_back("VOLUMETRIC_FOG", 1);
		shader_suite->get_base_defines().emplace_back(renderer_to_define(type), 1);
		shader_suite->bake_base_defines();
	}

	renderer_options = flags;
}

Renderer::RendererOptionFlags Renderer::get_mesh_renderer_options() const
{
	return renderer_options;
}

void Renderer::set_mesh_renderer_options(RendererOptionFlags flags)
{
	if (renderer_options != flags)
		set_mesh_renderer_options_internal(flags);
}

vector<pair<string, int>> Renderer::build_defines_from_renderer_options(RendererType type, RendererOptionFlags flags)
{
	vector<pair<string, int>> global_defines;
	if (flags & SHADOW_ENABLE_BIT)
		global_defines.emplace_back("SHADOWS", 1);
	if (flags & SHADOW_CASCADE_ENABLE_BIT)
		global_defines.emplace_back("SHADOW_CASCADES", 1);
	if (flags & FOG_ENABLE_BIT)
		global_defines.emplace_back("FOG", 1);
	if (flags & VOLUMETRIC_FOG_ENABLE_BIT)
		global_defines.emplace_back("VOLUMETRIC_FOG", 1);
	if (flags & ENVIRONMENT_ENABLE_BIT)
		global_defines.emplace_back("ENVIRONMENT", 1);
	if (flags & REFRACTION_ENABLE_BIT)
		global_defines.emplace_back("REFRACTION", 1);
	if (flags & POSITIONAL_LIGHT_ENABLE_BIT)
		global_defines.emplace_back("POSITIONAL_LIGHTS", 1);
	if (flags & POSITIONAL_LIGHT_SHADOW_ENABLE_BIT)
		global_defines.emplace_back("POSITIONAL_LIGHTS_SHADOW", 1);
	if (flags & POSITIONAL_LIGHT_CLUSTER_LIST_BIT)
		global_defines.emplace_back("CLUSTER_LIST", 1);
	if (flags & POSITIONAL_LIGHT_CLUSTER_BINDLESS_BIT)
		global_defines.emplace_back("CLUSTERER_BINDLESS", 1);

	if (flags & SHADOW_VSM_BIT)
		global_defines.emplace_back("DIRECTIONAL_SHADOW_VSM", 1);
	if (flags & POSITIONAL_LIGHT_SHADOW_VSM_BIT)
		global_defines.emplace_back("POSITIONAL_SHADOW_VSM", 1);
	if (flags & (POSITIONAL_LIGHT_SHADOW_VSM_BIT | SHADOW_VSM_BIT))
		global_defines.emplace_back("SHADOW_RESOLVE_VSM", 1);

	if (flags & SHADOW_PCF_KERNEL_WIDTH_5_BIT)
		global_defines.emplace_back("SHADOW_MAP_PCF_KERNEL_WIDTH", 5);
	else if (flags & SHADOW_PCF_KERNEL_WIDTH_3_BIT)
		global_defines.emplace_back("SHADOW_MAP_PCF_KERNEL_WIDTH", 3);

	if (flags & ALPHA_TEST_DISABLE_BIT)
		global_defines.emplace_back("ALPHA_TEST_DISABLE", 1);

	if (flags & MULTIVIEW_BIT)
		global_defines.emplace_back("MULTIVIEW", 1);

	if (flags & AMBIENT_OCCLUSION_BIT)
		global_defines.emplace_back("AMBIENT_OCCLUSION", 1);

	global_defines.emplace_back(renderer_to_define(type), 1);

	return global_defines;
}

Renderer::RendererOptionFlags Renderer::get_mesh_renderer_options_from_lighting(const LightingParameters &lighting)
{
	uint32_t flags = 0;
	if (lighting.environment_irradiance && lighting.environment_radiance)
		flags |= ENVIRONMENT_ENABLE_BIT;

	if (lighting.shadows)
	{
		flags |= SHADOW_ENABLE_BIT;
		if (!Vulkan::format_has_depth_or_stencil_aspect(lighting.shadows->get_format()))
			flags |= SHADOW_VSM_BIT;
		if (lighting.shadows->get_create_info().layers > 1)
			flags |= SHADOW_CASCADE_ENABLE_BIT;
	}

	if (lighting.volumetric_fog)
		flags |= VOLUMETRIC_FOG_ENABLE_BIT;
	else if (lighting.fog.falloff > 0.0f)
		flags |= FOG_ENABLE_BIT;

	if (lighting.cluster && (lighting.cluster->get_cluster_image() || lighting.cluster->get_cluster_bitmask_buffer()))
	{
		flags |= POSITIONAL_LIGHT_ENABLE_BIT;
		if ((lighting.cluster->get_spot_light_shadows() && lighting.cluster->get_point_light_shadows()) ||
		    (lighting.cluster->get_cluster_shadow_map_bindless_set() != VK_NULL_HANDLE))
		{
			flags |= POSITIONAL_LIGHT_SHADOW_ENABLE_BIT;
			if (lighting.cluster->get_shadow_type() == LightClusterer::ShadowType::VSM)
				flags |= POSITIONAL_LIGHT_SHADOW_VSM_BIT;
		}

		if (lighting.cluster->get_cluster_list_buffer())
			flags |= POSITIONAL_LIGHT_CLUSTER_LIST_BIT;
		if (lighting.cluster->clusterer_is_bindless())
			flags |= POSITIONAL_LIGHT_CLUSTER_BINDLESS_BIT;
	}

	if (lighting.ambient_occlusion)
		flags |= AMBIENT_OCCLUSION_BIT;

	return flags;
}

void Renderer::set_mesh_renderer_options_from_lighting(const LightingParameters &lighting)
{
	auto flags = get_mesh_renderer_options_from_lighting(lighting);
	set_mesh_renderer_options(flags);
}

void Renderer::setup_shader_suite(Device &device_, RendererType renderer_type)
{
	ShaderSuiteResolver default_resolver;
	auto *res = resolver ? resolver : &default_resolver;
	for (int i = 0; i < ecast(RenderableType::Count); i++)
		res->init_shader_suite(device_, suite[i], renderer_type, static_cast<RenderableType>(i));
}

void Renderer::on_device_created(const DeviceCreatedEvent &created)
{
	device = &created.get_device();
	setup_shader_suite(*device, type);
	set_mesh_renderer_options_internal(renderer_options);
	for (auto &s : suite)
		s.bake_base_defines();
}

void Renderer::on_device_destroyed(const DeviceCreatedEvent &)
{
}

void Renderer::begin(RenderQueue &queue) const
{
	queue.reset();
	queue.set_shader_suites(suite);
}

static void set_cluster_parameters_legacy(Vulkan::CommandBuffer &cmd, const LightClusterer &cluster)
{
	auto &params = *cmd.allocate_typed_constant_data<ClustererParametersLegacy>(0, BINDING_GLOBAL_CLUSTERER_PARAMETERS, 1);
	memset(&params, 0, sizeof(params));

	cmd.set_texture(0, BINDING_GLOBAL_CLUSTER_IMAGE_LEGACY, *cluster.get_cluster_image(), StockSampler::NearestClamp);

	params.transform = cluster.get_cluster_transform();
	memcpy(params.spots, cluster.get_active_spot_lights(),
	       cluster.get_active_spot_light_count() * sizeof(PositionalFragmentInfo));
	memcpy(params.points, cluster.get_active_point_lights(),
	       cluster.get_active_point_light_count() * sizeof(PositionalFragmentInfo));

	if (cluster.get_spot_light_shadows() && cluster.get_point_light_shadows())
	{
		auto spot_sampler = format_has_depth_or_stencil_aspect(cluster.get_spot_light_shadows()->get_format()) ?
		                    StockSampler::LinearShadow : StockSampler::LinearClamp;
		auto point_sampler = format_has_depth_or_stencil_aspect(cluster.get_point_light_shadows()->get_format()) ?
		                     StockSampler::LinearShadow : StockSampler::LinearClamp;

		cmd.set_texture(0, BINDING_GLOBAL_CLUSTER_SPOT_LEGACY, *cluster.get_spot_light_shadows(), spot_sampler);
		cmd.set_texture(0, BINDING_GLOBAL_CLUSTER_POINT_LEGACY, *cluster.get_point_light_shadows(), point_sampler);

		memcpy(params.spot_shadow_transforms, cluster.get_active_spot_light_shadow_matrices(),
		       cluster.get_active_spot_light_count() * sizeof(mat4));

		memcpy(params.point_shadow, cluster.get_active_point_light_shadow_transform(),
		       cluster.get_active_point_light_count() * sizeof(PointTransform));
	}

	if (cluster.get_cluster_list_buffer())
		cmd.set_storage_buffer(0, BINDING_GLOBAL_CLUSTER_LIST_LEGACY, *cluster.get_cluster_list_buffer());
}

static void set_cluster_parameters_bindless(Vulkan::CommandBuffer &cmd, const LightClusterer &cluster)
{
	*cmd.allocate_typed_constant_data<ClustererParametersBindless>(0, BINDING_GLOBAL_CLUSTERER_PARAMETERS, 1) = cluster.get_cluster_parameters_bindless();
	cmd.set_storage_buffer(0, BINDING_GLOBAL_CLUSTER_TRANSFORM, *cluster.get_cluster_transform_buffer());
	cmd.set_storage_buffer(0, BINDING_GLOBAL_CLUSTER_BITMASK, *cluster.get_cluster_bitmask_buffer());
	cmd.set_storage_buffer(0, BINDING_GLOBAL_CLUSTER_RANGE, *cluster.get_cluster_range_buffer());
	if (cluster.get_cluster_shadow_map_bindless_set() != VK_NULL_HANDLE)
		cmd.set_bindless(1, cluster.get_cluster_shadow_map_bindless_set());
}

static void set_cluster_parameters(Vulkan::CommandBuffer &cmd, const LightClusterer &cluster)
{
	if (cluster.clusterer_is_bindless())
		set_cluster_parameters_bindless(cmd, cluster);
	else
		set_cluster_parameters_legacy(cmd, cluster);
}

void Renderer::bind_lighting_parameters(Vulkan::CommandBuffer &cmd, const RenderContext &context)
{
	auto *lighting = context.get_lighting_parameters();
	assert(lighting);

	auto *combined = cmd.allocate_typed_constant_data<CombinedRenderParameters>(0, BINDING_GLOBAL_RENDER_PARAMETERS, 1);
	memset(combined, 0, sizeof(*combined));

	combined->environment.intensity = lighting->environment.intensity;
	if (lighting->environment_radiance)
		combined->environment.mipscale = float(lighting->environment_radiance->get_create_info().levels - 1);

	if (lighting->volumetric_fog)
	{
		cmd.set_texture(0, BINDING_GLOBAL_VOLUMETRIC_FOG, lighting->volumetric_fog->get_view(), StockSampler::LinearClamp);
		combined->volumetric_fog.slice_z_log2_scale = lighting->volumetric_fog->get_slice_z_log2_scale();
	}
	else
		combined->fog = lighting->fog;

	combined->shadow = lighting->shadow;
	combined->directional = lighting->directional;
	combined->refraction = lighting->refraction;

	combined->resolution.resolution = vec2(cmd.get_viewport().width, cmd.get_viewport().height);
	combined->resolution.inv_resolution = vec2(1.0f / cmd.get_viewport().width, 1.0f / cmd.get_viewport().height);

	cmd.set_texture(0, BINDING_GLOBAL_BRDF_TABLE,
	                cmd.get_device().get_texture_manager().request_texture("builtin://textures/ibl_brdf_lut.gtx")->get_image()->get_view(),
	                Vulkan::StockSampler::LinearClamp);

	if (lighting->environment_radiance != nullptr)
		cmd.set_texture(0, BINDING_GLOBAL_ENV_RADIANCE, *lighting->environment_radiance, Vulkan::StockSampler::TrilinearClamp);
	if (lighting->environment_irradiance != nullptr)
		cmd.set_texture(0, BINDING_GLOBAL_ENV_IRRADIANCE, *lighting->environment_irradiance, Vulkan::StockSampler::LinearClamp);

	if (lighting->shadows != nullptr)
	{
		auto sampler = format_has_depth_or_stencil_aspect(lighting->shadows->get_format()) ? StockSampler::LinearShadow
		                                                                                   : StockSampler::LinearClamp;
		cmd.set_texture(0, BINDING_GLOBAL_DIRECTIONAL_SHADOW, *lighting->shadows, sampler);
	}

	if (lighting->ambient_occlusion)
		cmd.set_texture(0, BINDING_GLOBAL_AMBIENT_OCCLUSION, *lighting->ambient_occlusion, StockSampler::LinearClamp);

	if (lighting->cluster && (lighting->cluster->get_cluster_image() || lighting->cluster->get_cluster_bitmask_buffer()))
		set_cluster_parameters(cmd, *lighting->cluster);
}

void Renderer::bind_global_parameters(Vulkan::CommandBuffer &cmd, const RenderContext &context)
{
	auto *global = cmd.allocate_typed_constant_data<RenderParameters>(0, BINDING_GLOBAL_TRANSFORM, 1);
	*global = context.get_render_parameters();
}

void Renderer::set_render_context_parameter_binder(RenderContextParameterBinder *binder)
{
	render_context_parameter_binder = binder;
}

void Renderer::promote_read_write_cache_to_read_only()
{
	for (auto &s : suite)
		s.promote_read_write_cache_to_read_only();
}

void Renderer::flush_subset(Vulkan::CommandBuffer &cmd, const RenderQueue &queue, const RenderContext &context,
                            RendererFlushFlags options, const FlushParameters *parameters, unsigned index, unsigned num_indices) const
{
	assert((options & SKIP_SORTING_BIT) != 0);

	if (render_context_parameter_binder)
	{
		render_context_parameter_binder->bind_render_context_parameters(cmd, context);
	}
	else
	{
		bind_global_parameters(cmd, context);
		if (type == RendererType::GeneralForward)
			bind_lighting_parameters(cmd, context);
	}

	cmd.set_opaque_state();

	if (options & FRONT_FACE_CLOCKWISE_BIT)
		cmd.set_front_face(VK_FRONT_FACE_CLOCKWISE);

	if (options & NO_COLOR_BIT)
		cmd.set_color_write_mask(0);

	if (options & DEPTH_STENCIL_READ_ONLY_BIT)
		cmd.set_depth_test(true, false);

	if (options & DEPTH_BIAS_BIT)
	{
		cmd.set_depth_bias(true);
		cmd.set_depth_bias(+4.0f, +3.0f);
	}

	if (options & BACKFACE_BIT)
	{
		cmd.set_cull_mode(VK_CULL_MODE_FRONT_BIT);
		cmd.set_depth_compare(VK_COMPARE_OP_GREATER);
	}

	if (options & DEPTH_TEST_EQUAL_BIT)
		cmd.set_depth_compare(VK_COMPARE_OP_EQUAL);
	else if (options & DEPTH_TEST_INVERT_BIT)
		cmd.set_depth_compare(VK_COMPARE_OP_GREATER);

	if (options & STENCIL_WRITE_REFERENCE_BIT)
	{
		cmd.set_stencil_test(true);
		cmd.set_stencil_ops(VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP);
		cmd.set_stencil_reference(parameters->stencil.compare_mask, parameters->stencil.write_mask, parameters->stencil.ref);
	}

	CommandBufferSavedState state;
	cmd.save_state(COMMAND_BUFFER_SAVED_SCISSOR_BIT | COMMAND_BUFFER_SAVED_VIEWPORT_BIT | COMMAND_BUFFER_SAVED_RENDER_STATE_BIT, state);
	// No need to spend write bandwidth on writing 0 to light buffer, render opaque emissive on top.
	queue.dispatch_subset(Queue::Opaque, cmd, &state, index, num_indices);
	queue.dispatch_subset(Queue::OpaqueEmissive, cmd, &state, index, num_indices);

	if (type == RendererType::GeneralDeferred)
	{
		// General deferred renderers can render light volumes.
		cmd.restore_state(state);
		cmd.set_input_attachments(3, 0);
		cmd.set_depth_test(true, false);
		cmd.set_blend_enable(true);
		cmd.set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE);
		cmd.set_blend_op(VK_BLEND_OP_ADD);

		cmd.set_stencil_test(true);
		if (options & STENCIL_COMPARE_REFERENCE_BIT)
			cmd.set_stencil_reference(parameters->stencil.compare_mask, 0, parameters->stencil.ref);
		else
			cmd.set_stencil_reference(0xff, 0, 0);

		cmd.set_stencil_front_ops(VK_COMPARE_OP_EQUAL, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP);
		cmd.set_stencil_back_ops(VK_COMPARE_OP_EQUAL, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP);
		cmd.save_state(COMMAND_BUFFER_SAVED_SCISSOR_BIT | COMMAND_BUFFER_SAVED_VIEWPORT_BIT | COMMAND_BUFFER_SAVED_RENDER_STATE_BIT, state);
		queue.dispatch_subset(Queue::Light, cmd, &state, index, num_indices);
	}
	else if (type == RendererType::GeneralForward)
	{
		// Forward renderers can also render transparent objects.
		cmd.restore_state(state);
		cmd.set_blend_enable(true);
		cmd.set_blend_factors(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
		cmd.set_blend_op(VK_BLEND_OP_ADD);
		cmd.set_depth_test(true, false);
		cmd.save_state(COMMAND_BUFFER_SAVED_SCISSOR_BIT | COMMAND_BUFFER_SAVED_VIEWPORT_BIT | COMMAND_BUFFER_SAVED_RENDER_STATE_BIT, state);
		queue.dispatch_subset(Queue::Transparent, cmd, &state, index, num_indices);
	}
}

void Renderer::flush(Vulkan::CommandBuffer &cmd, RenderQueue &queue, const RenderContext &context, RendererFlushFlags options, const FlushParameters *params) const
{
	if ((options & SKIP_SORTING_BIT) == 0)
		queue.sort();
	flush_subset(cmd, queue, context, options | SKIP_SORTING_BIT, params, 0, 1);
}

DebugMeshInstanceInfo &Renderer::render_debug(RenderQueue &queue, const RenderContext &context, unsigned count)
{
	DebugMeshInfo debug;

	auto *instance_data = queue.allocate_one<DebugMeshInstanceInfo>();
	instance_data->count = count;
	instance_data->colors = queue.allocate_many<vec4>(count);
	instance_data->positions = queue.allocate_many<vec3>(count);

	Hasher hasher;
	hasher.string("debug");
	auto instance_key = hasher.get();
	auto sorting_key = RenderInfo::get_sort_key(context, Queue::Opaque, hasher.get(), hasher.get(), vec3(0.0f));
	debug.MVP = context.get_render_parameters().view_projection;

	auto *debug_info = queue.push<DebugMeshInfo>(Queue::Opaque, instance_key, sorting_key,
	                                             RenderFunctions::debug_mesh_render,
	                                             instance_data);

	if (debug_info)
	{
		debug.program = suite[ecast(RenderableType::DebugMesh)].get_program(DrawPipeline::Opaque,
		                                                                    MESH_ATTRIBUTE_POSITION_BIT |
		                                                                    MESH_ATTRIBUTE_VERTEX_COLOR_BIT, 0);
		*debug_info = debug;
	}

	return *instance_data;
}

template <typename T>
inline void dump_debug_coords(vec3 *pos, const T &t)
{
	*pos++ = t.get_coord(0.0f, 0.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 0.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 0.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 0.0f, 1.0f);
	*pos++ = t.get_coord(1.0f, 0.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 0.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 0.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 0.0f, 0.0f);

	*pos++ = t.get_coord(0.0f, 1.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 1.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 1.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 1.0f, 1.0f);
	*pos++ = t.get_coord(1.0f, 1.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 1.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 1.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 1.0f, 0.0f);

	*pos++ = t.get_coord(0.0f, 0.0f, 0.0f);
	*pos++ = t.get_coord(0.0f, 1.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 0.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 1.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 0.0f, 1.0f);
	*pos++ = t.get_coord(1.0f, 1.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 0.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 1.0f, 1.0f);
}

void Renderer::render_debug_frustum(RenderQueue &queue, const RenderContext &context, const Frustum &frustum, const vec4 &color)
{
	auto &debug = render_debug(queue, context, 12 * 2);
	for (unsigned i = 0; i < debug.count; i++)
		debug.colors[i] = color;
	dump_debug_coords(debug.positions, frustum);
}

void Renderer::render_debug_aabb(RenderQueue &queue, const RenderContext &context, const AABB &aabb, const vec4 &color)
{
	auto &debug = render_debug(queue, context, 12 * 2);
	for (unsigned i = 0; i < debug.count; i++)
		debug.colors[i] = color;
	dump_debug_coords(debug.positions, aabb);
}

void DeferredLightRenderer::render_light(Vulkan::CommandBuffer &cmd, const RenderContext &context,
                                         Renderer::RendererOptionFlags flags)
{
	cmd.set_quad_state();
	cmd.set_input_attachments(3, 0);
	cmd.set_blend_enable(true);
	cmd.set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE);
	cmd.set_blend_op(VK_BLEND_OP_ADD);
	CommandBufferUtil::set_fullscreen_quad_vertex_state(cmd);

	auto &device = cmd.get_device();
	auto *program = device.get_shader_manager().register_graphics("builtin://shaders/lights/directional.vert",
	                                                              "builtin://shaders/lights/directional.frag");

	auto &light = *context.get_lighting_parameters();
	auto &subgroup = device.get_device_features().subgroup_properties;

	vector<pair<string, int>> defines;
	if (light.shadows && light.shadows->get_create_info().layers > 1)
	{
		defines.emplace_back("SHADOW_CASCADES", 1);
		if ((subgroup.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) != 0 &&
		    (subgroup.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) != 0 &&
		    !ImplementationQuirks::get().force_no_subgroups)
		{
			// For cascaded shadows.
			defines.emplace_back("SUBGROUP_ARITHMETIC", 1);
		}
	}
	if (light.environment_radiance && light.environment_irradiance)
		defines.emplace_back("ENVIRONMENT", 1);
	if (light.shadows)
	{
		defines.emplace_back("SHADOWS", 1);
		if (!format_has_depth_or_stencil_aspect(light.shadows->get_format()))
			defines.emplace_back("DIRECTIONAL_SHADOW_VSM", 1);
		else
		{
			if (flags & Renderer::SHADOW_PCF_KERNEL_WIDTH_5_BIT)
				defines.emplace_back("SHADOW_MAP_PCF_KERNEL_WIDTH", 5);
			else if (flags & Renderer::SHADOW_PCF_KERNEL_WIDTH_3_BIT)
				defines.emplace_back("SHADOW_MAP_PCF_KERNEL_WIDTH", 3);
		}
	}
	if (light.ambient_occlusion)
		defines.emplace_back("AMBIENT_OCCLUSION", 1);

	auto *variant = program->register_variant(defines);
	cmd.set_program(variant->get_program());
	cmd.set_depth_test(true, false);
	cmd.set_depth_compare(VK_COMPARE_OP_GREATER);

	if (light.environment_radiance)
		cmd.set_texture(0, BINDING_GLOBAL_ENV_RADIANCE, *light.environment_radiance, Vulkan::StockSampler::LinearClamp);
	if (light.environment_irradiance)
		cmd.set_texture(0, BINDING_GLOBAL_ENV_IRRADIANCE, *light.environment_irradiance, Vulkan::StockSampler::LinearClamp);

	cmd.set_texture(0, BINDING_GLOBAL_BRDF_TABLE, Granite::Global::common_renderer_data()->brdf_tables.get_texture()->get_image()->get_view(),
	                Vulkan::StockSampler::LinearClamp);

	if (light.shadows)
	{
		auto sampler = format_has_depth_or_stencil_aspect(light.shadows->get_format()) ? StockSampler::LinearShadow
		                                                                               : StockSampler::LinearClamp;
		cmd.set_texture(0, BINDING_GLOBAL_DIRECTIONAL_SHADOW, *light.shadows, sampler);
	}

	if (light.ambient_occlusion)
		cmd.set_texture(0, BINDING_GLOBAL_AMBIENT_OCCLUSION, *light.ambient_occlusion, Vulkan::StockSampler::LinearClamp);

	struct DirectionalLightPush
	{
		alignas(16) vec4 inv_view_proj_col2;
		alignas(16) vec4 color_env_intensity;
		alignas(16) vec4 camera_pos_mipscale;
		alignas(16) vec3 direction;
		alignas(4) float cascade_log_bias;
		alignas(16) vec3 camera_front;
		alignas(8) vec2 inv_resolution;
	} push;

	struct DirectionalLightUBO
	{
		mat4 inv_view_projection;
		mat4 transforms[NumShadowCascades];
	};
	auto *ubo = static_cast<DirectionalLightUBO *>(cmd.allocate_constant_data(0, 0, sizeof(DirectionalLightUBO)));
	ubo->inv_view_projection = context.get_render_parameters().inv_view_projection;
	for (int i = 0; i < NumShadowCascades; i++)
		ubo->transforms[i] = light.shadow.transforms[i];

	push.inv_view_proj_col2 = context.get_render_parameters().inv_view_projection[2];
	push.color_env_intensity = vec4(light.directional.color, light.environment.intensity);
	push.direction = light.directional.direction;
	push.cascade_log_bias = light.shadow.cascade_log_bias;

	float mipscale = 0.0f;
	if (light.environment_radiance)
		mipscale = float(light.environment_radiance->get_create_info().levels - 1);

	push.camera_pos_mipscale = vec4(context.get_render_parameters().camera_position, mipscale);
	push.camera_front = context.get_render_parameters().camera_front;
	push.inv_resolution.x = 1.0f / cmd.get_viewport().width;
	push.inv_resolution.y = 1.0f / cmd.get_viewport().height;
	cmd.push_constants(&push, 0, sizeof(push));

	CommandBufferUtil::draw_fullscreen_quad(cmd);

	// Clustered lighting.
	if (light.cluster && (light.cluster->get_cluster_image() || light.cluster->get_cluster_bitmask_buffer()))
	{
		struct ClusterPush
		{
			alignas(16) vec4 inv_view_proj_col2;
			alignas(16) vec3 camera_pos;
			alignas(8) vec2 inv_resolution;
		};

		ClusterPush cluster_push = {
			context.get_render_parameters().inv_view_projection[2],
			context.get_render_parameters().camera_position,
			vec2(1.0f / cmd.get_viewport().width, 1.0f / cmd.get_viewport().height),
		};

		vector<pair<string, int>> cluster_defines;
		if (light.cluster->get_spot_light_shadows() ||
		    light.cluster->get_cluster_shadow_map_bindless_set())
		{
			cluster_defines.emplace_back("POSITIONAL_LIGHTS_SHADOW", 1);
			if (light.cluster->get_shadow_type() == LightClusterer::ShadowType::VSM)
				cluster_defines.emplace_back("POSITIONAL_SHADOW_VSM", 1);
			else
			{
				if (flags & Renderer::SHADOW_PCF_KERNEL_WIDTH_5_BIT)
					cluster_defines.emplace_back("SHADOW_MAP_PCF_KERNEL_WIDTH", 5);
				else if (flags & Renderer::SHADOW_PCF_KERNEL_WIDTH_3_BIT)
					cluster_defines.emplace_back("SHADOW_MAP_PCF_KERNEL_WIDTH", 3);
			}
		}

		if (light.cluster->clusterer_is_bindless())
			cluster_defines.emplace_back("CLUSTERER_BINDLESS", 1);
		else if (light.cluster->get_cluster_list_buffer())
			cluster_defines.emplace_back("CLUSTER_LIST", 1);

		// Try to enable wave-optimizations.
		static const VkSubgroupFeatureFlags required_subgroup = VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;
		if ((subgroup.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) != 0 &&
		    !ImplementationQuirks::get().force_no_subgroups &&
		    (subgroup.supportedOperations & required_subgroup) == required_subgroup)
		{
			cluster_defines.emplace_back("CLUSTERING_WAVE_UNIFORM", 1);
		}

		cmd.set_program("builtin://shaders/lights/clustering.vert",
		                "builtin://shaders/lights/clustering.frag",
		                cluster_defines);

		cmd.push_constants(&cluster_push, 0, sizeof(cluster_push));
		set_cluster_parameters(cmd, *light.cluster);
		CommandBufferUtil::draw_fullscreen_quad(cmd);
	}

	// Skip fog for non-reflection passes.
	if (light.volumetric_fog != nullptr)
	{
		struct Fog
		{
			vec4 inv_z;
			float slice_z_log2_scale;
		} fog;

		fog.inv_z = vec4(context.get_render_parameters().inv_projection[2].zw(),
		                 context.get_render_parameters().inv_projection[3].zw());
		fog.slice_z_log2_scale = light.volumetric_fog->get_slice_z_log2_scale();
		cmd.push_constants(&fog, 0, sizeof(fog));

		cmd.set_texture(2, 0, light.volumetric_fog->get_view(), StockSampler::LinearClamp);
		cmd.set_program("builtin://shaders/lights/volumetric_fog.vert", "builtin://shaders/lights/volumetric_fog.frag");
		cmd.set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_ALPHA);
		// Always render volumetric fog.
		cmd.set_depth_test(false, false);
		cmd.set_stencil_test(false);
		CommandBufferUtil::draw_fullscreen_quad(cmd);
	}
	else if (light.fog.falloff > 0.0f)
	{
		struct Fog
		{
			mat4 inv_view_proj;
			vec4 camera_pos;
			vec4 color_falloff;
		} fog;

		fog.inv_view_proj = context.get_render_parameters().inv_view_projection;
		fog.camera_pos = vec4(context.get_render_parameters().camera_position, 0.0f);
		fog.color_falloff = vec4(light.fog.color, light.fog.falloff);
		cmd.push_constants(&fog, 0, sizeof(fog));

		cmd.set_blend_factors(VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA);
		cmd.set_program("builtin://shaders/lights/fog.vert", "builtin://shaders/lights/fog.frag");
		CommandBufferUtil::draw_fullscreen_quad(cmd);
	}
}

void ShaderSuiteResolver::init_shader_suite(Device &device, ShaderSuite &suite,
                                            RendererType renderer,
                                            RenderableType drawable) const
{
	if (renderer == RendererType::GeneralDeferred ||
	    renderer == RendererType::GeneralForward)
	{
		switch (drawable)
		{
		case RenderableType::Mesh:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/static_mesh.vert", "builtin://shaders/static_mesh.frag");
			break;

		case RenderableType::DebugMesh:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/debug_mesh.vert", "builtin://shaders/debug_mesh.frag");
			break;

		case RenderableType::Skybox:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/skybox.vert", "builtin://shaders/skybox.frag");
			break;

		case RenderableType::SkyCylinder:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/skycylinder.vert", "builtin://shaders/skycylinder.frag");
			break;

		case RenderableType::Ground:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/ground.vert", "builtin://shaders/ground.frag");
			break;

		case RenderableType::Ocean:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/ocean/ocean.vert", "builtin://shaders/ocean/ocean.frag");
			break;

		case RenderableType::TexturePlane:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/texture_plane.vert", "builtin://shaders/texture_plane.frag");
			break;

		default:
			break;
		}
	}
	else if (renderer == RendererType::DepthOnly)
	{
		switch (drawable)
		{
		case RenderableType::Mesh:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/static_mesh.vert", "builtin://shaders/static_mesh_depth.frag");
			break;

		case RenderableType::Ground:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/ground.vert", "builtin://shaders/dummy_depth.frag");
			break;

		case RenderableType::TexturePlane:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/texture_plane.vert", "builtin://shaders/dummy_depth.frag");
			break;

		case RenderableType::SpotLight:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/lights/spot.vert", "builtin://shaders/dummy.frag");
			break;

		case RenderableType::PointLight:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/lights/point.vert", "builtin://shaders/dummy.frag");
			break;

		default:
			break;
		}
	}
	else if (renderer == RendererType::Flat)
	{
		if (drawable == RenderableType::Sprite)
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/sprite.vert", "builtin://shaders/sprite.frag");
		else if (drawable == RenderableType::LineUI)
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/line_ui.vert", "builtin://shaders/debug_mesh.frag");
	}

	if (renderer == RendererType::GeneralDeferred)
	{
		if (drawable == RenderableType::SpotLight)
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/lights/spot.vert", "builtin://shaders/lights/spot.frag");
		else if (drawable == RenderableType::PointLight)
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/lights/point.vert", "builtin://shaders/lights/point.frag");
	}
}

}
