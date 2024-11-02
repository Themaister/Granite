/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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
#include "renderer.hpp"
#include "device.hpp"
#include "render_context.hpp"
#include "sprite.hpp"
#include "lights/clusterer.hpp"
#include "lights/volumetric_fog.hpp"
#include "lights/volumetric_diffuse.hpp"
#include "render_parameters.hpp"
#include "global_managers.hpp"
#include "common_renderer_data.hpp"
#include "rapidjson_wrapper.hpp"
#include <string.h>

using namespace Vulkan;
using namespace Util;

enum GlobalDescriptorSetBindings
{
	BINDING_GLOBAL_TRANSFORM = 0,
	BINDING_GLOBAL_RENDER_PARAMETERS = 1,

	BINDING_GLOBAL_VOLUMETRIC_DIFFUSE_PARAMETERS = 2,
	BINDING_GLOBAL_VOLUMETRIC_FOG_PARAMETERS = 3,

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
	BINDING_GLOBAL_CLUSTER_RANGE = 11,
	BINDING_GLOBAL_CLUSTER_BITMASK_DECAL = 12,
	BINDING_GLOBAL_CLUSTER_RANGE_DECAL = 13,

	BINDING_GLOBAL_LINEAR_SAMPLER = 14,
	BINDING_GLOBAL_SHADOW_SAMPLER = 15,
	BINDING_GLOBAL_GEOMETRY_SAMPLER = 16,

	BINDING_GLOBAL_VOLUMETRIC_DIFFUSE_FALLBACK_VOLUME = 17
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
	set_renderer(Type::ShadowDepthDirectionalFallbackPCF, Util::make_handle<Renderer>(RendererType::DepthOnly, nullptr));
	set_renderer(Type::ShadowDepthDirectionalVSM, Util::make_handle<Renderer>(RendererType::DepthOnly, nullptr));
	set_renderer(Type::ShadowDepthPositionalVSM, Util::make_handle<Renderer>(RendererType::DepthOnly, nullptr));
	set_renderer(Type::PrepassDepth, Util::make_handle<Renderer>(RendererType::DepthOnly, nullptr));
	set_renderer(Type::MotionVector, Util::make_handle<Renderer>(RendererType::MotionVector, nullptr));
	set_renderer(Type::Deferred, Util::make_handle<Renderer>(RendererType::GeneralDeferred, nullptr));
}

void RendererSuite::update_mesh_rendering_options(const RenderContext &context, const Config &config)
{
	get_renderer(Type::ShadowDepthDirectionalPCF).set_mesh_renderer_options(
			config.cascaded_directional_shadows ? Renderer::MULTIVIEW_BIT : 0);
	get_renderer(Type::ShadowDepthDirectionalFallbackPCF).set_mesh_renderer_options(0);
	get_renderer(Type::ShadowDepthPositionalPCF).set_mesh_renderer_options(0);
	get_renderer(Type::ShadowDepthDirectionalVSM).set_mesh_renderer_options(
			(config.cascaded_directional_shadows ? Renderer::MULTIVIEW_BIT : 0) | Renderer::SHADOW_VSM_BIT);
	get_renderer(Type::ShadowDepthPositionalVSM).set_mesh_renderer_options(
			Renderer::POSITIONAL_LIGHT_SHADOW_VSM_BIT);
	get_renderer(Type::PrepassDepth).set_mesh_renderer_options(0);
	get_renderer(Type::MotionVector).set_mesh_renderer_options(0);

	Renderer::RendererOptionFlags pcf_flags = 0;
	if (config.pcf_wide)
		pcf_flags |= Renderer::SHADOW_PCF_KERNEL_WIDE_BIT;

	auto opts = Renderer::get_mesh_renderer_options_from_lighting(*context.get_lighting_parameters());

	Util::Hasher h;
	h.u32(opts);
	h.u32(uint32_t(config.pcf_wide));
	h.u32(uint32_t(config.directional_light_vsm));
	h.u32(uint32_t(config.forward_z_prepass));
	h.u32(uint32_t(config.cascaded_directional_shadows));
	Util::Hash config_hash = h.get();

	get_renderer(Type::Deferred).set_mesh_renderer_options(pcf_flags | (opts & Renderer::POSITIONAL_DECALS_BIT));
	get_renderer(Type::ForwardOpaque).set_mesh_renderer_options(
			opts | pcf_flags | (config.forward_z_prepass ? Renderer::ALPHA_TEST_DISABLE_BIT : 0));
	opts &= ~Renderer::AMBIENT_OCCLUSION_BIT;
	get_renderer(Type::ForwardTransparent).set_mesh_renderer_options(opts | pcf_flags);

	if (config_hash != current_config_hash)
	{
		register_variants_from_cache();
		current_config_hash = config_hash;
		promote_read_write_cache_to_read_only();
	}
}

bool RendererSuite::load_variant_cache(const std::string &path)
{
	using namespace rapidjson;
	std::string json;

	if (!GRANITE_FILESYSTEM()->read_file_to_string(path, json))
		return false;

	Document doc;
	doc.Parse(json);
	if (doc.HasParseError())
	{
		LOGE("Failed to parse variant cache format!\n");
		return false;
	}

	if (!doc.HasMember("rendererSuiteCacheVersion"))
	{
		LOGE("Could not find rendererSuiteCacheVersion member.\n");
		return false;
	}

	unsigned version = doc["rendererSuiteCacheVersion"].GetUint();
	if (version != CacheVersion)
	{
		LOGE("Mismatch in renderer suite cache version, %u != %u.\n", version, CacheVersion);
		return false;
	}

	auto &maps = doc["variants"];
	for (auto itr = maps.Begin(); itr != maps.End(); ++itr)
	{
		auto &value = *itr;
		Variant variant = {};
		variant.renderer_suite_type = static_cast<RendererSuite::Type>(value["rendererSuiteType"].GetUint());
		variant.renderable_type = static_cast<RenderableType>(value["renderableType"].GetUint());
		variant.key.word = value["word"].GetUint();
		variants.push_back(variant);
	}

	LOGI("Loaded variant cache from %s.\n", path.c_str());
	return true;
}

bool RendererSuite::save_variant_cache(const std::string &path)
{
	using namespace rapidjson;
	Document doc;
	doc.SetObject();
	auto &allocator = doc.GetAllocator();

	doc.AddMember("rendererSuiteCacheVersion", uint32_t(CacheVersion), allocator);

	Value variants_array(kArrayType);

	for (int suite_type = 0; suite_type < Util::ecast(RendererSuite::Type::Count); suite_type++)
	{
		for (int renderable_type = 0; renderable_type < Util::ecast(RenderableType::Count); renderable_type++)
		{
			auto &suite = handles[suite_type]->get_shader_suites()[renderable_type];
			auto &signatures = suite.get_variant_signatures().get_thread_unsafe();
			for (auto &key : signatures)
			{
				Value variant(kObjectType);
				variant.AddMember("rendererSuiteType", suite_type, allocator);
				variant.AddMember("renderableType", renderable_type, allocator);
				variant.AddMember("word", key.key.word, allocator);
				variants_array.PushBack(variant, allocator);
			}
		}
	}

	doc.AddMember("variants", variants_array, allocator);

	StringBuffer buffer;
	PrettyWriter<StringBuffer> writer(buffer);
	doc.Accept(writer);

	auto file = GRANITE_FILESYSTEM()->open_transactional_mapping(path, buffer.GetSize());
	if (!file)
	{
		LOGE("Failed to open %s for writing.\n", path.c_str());
		return false;
	}

	memcpy(file->mutable_data(), buffer.GetString(), buffer.GetSize());

	LOGI("Saved variant cache to %s.\n", path.c_str());
	return true;
}

void RendererSuite::register_variants_from_cache()
{
	if (variants.empty())
		return;

	GRANITE_SCOPED_TIMELINE_EVENT("renderer-suite-warm-variants");

	for (auto &variant : variants)
	{
		auto *suites = handles[Util::ecast(variant.renderer_suite_type)]->get_shader_suites();
		auto &suite = suites[Util::ecast(variant.renderable_type)];
		suite.get_program(variant.key);
	}

	LOGI("Warmed cached variants.\n");
}

Renderer::Renderer(RendererType type_, const ShaderSuiteResolver *resolver_)
	: type(type_), resolver(resolver_)
{
	EVENT_MANAGER_REGISTER_LATCH(Renderer, on_pipeline_created, on_pipeline_destroyed, DevicePipelineReadyEvent);

	if (type == RendererType::GeneralDeferred || type == RendererType::GeneralForward)
		set_mesh_renderer_options(SHADOW_CASCADE_ENABLE_BIT | SHADOW_ENABLE_BIT | FOG_ENABLE_BIT);
	else
		set_mesh_renderer_options(0);
}

ShaderSuite *Renderer::get_shader_suites()
{
	return suite;
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

	case RendererType::MotionVector:
		return "RENDERER_MOTION_VECTOR";

	default:
		break;
	}

	return "";
}

void Renderer::add_subgroup_defines(Vulkan::Device &device, std::vector<std::pair<std::string, int>> &defines,
                                    VkShaderStageFlagBits stage)
{
	auto &vk11 = device.get_device_features().vk11_props;

	if ((vk11.subgroupSupportedStages & stage) != 0 &&
	    !ImplementationQuirks::get().force_no_subgroups &&
	    vk11.subgroupSize >= 4)
	{
		const VkSubgroupFeatureFlags quad_required =
				(stage & (VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)) != 0 ?
				VK_SUBGROUP_FEATURE_QUAD_BIT : 0;
		const VkSubgroupFeatureFlags required =
				VK_SUBGROUP_FEATURE_BASIC_BIT |
				VK_SUBGROUP_FEATURE_CLUSTERED_BIT |
				quad_required |
				VK_SUBGROUP_FEATURE_BALLOT_BIT |
				VK_SUBGROUP_FEATURE_VOTE_BIT |
				VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;

		if ((vk11.subgroupSupportedOperations & required) == required)
			defines.emplace_back("SUBGROUP_OPS", 1);

		if (!ImplementationQuirks::get().force_no_subgroup_shuffle)
			if ((vk11.subgroupSupportedOperations & VK_SUBGROUP_FEATURE_SHUFFLE_BIT) != 0)
				defines.emplace_back("SUBGROUP_SHUFFLE", 1);

		if (stage == VK_SHADER_STAGE_FRAGMENT_BIT)
			defines.emplace_back("SUBGROUP_FRAGMENT", 1);
		else if (stage == VK_SHADER_STAGE_COMPUTE_BIT)
			defines.emplace_back("SUBGROUP_COMPUTE", 1);
	}
}

void Renderer::set_mesh_renderer_options_internal(RendererOptionFlags flags)
{
	auto global_defines = build_defines_from_renderer_options(type, flags);

	if (device)
	{
		// Safe early-discard.
		if (device->get_device_features().vk13_features.shaderDemoteToHelperInvocation)
			global_defines.emplace_back("DEMOTE", 1);
		add_subgroup_defines(*device, global_defines, VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	auto &meshes = suite[ecast(RenderableType::Mesh)];
	meshes.get_base_defines() = global_defines;
	meshes.bake_base_defines();
	auto &probes = suite[ecast(RenderableType::DebugProbe)];
	probes.get_base_defines() = global_defines;
	probes.bake_base_defines();
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

std::vector<std::pair<std::string, int>> Renderer::build_defines_from_renderer_options(
	RendererType type, RendererOptionFlags flags)
{
	std::vector<std::pair<std::string, int>> global_defines;
	if (flags & SHADOW_ENABLE_BIT)
		global_defines.emplace_back("SHADOWS", 1);
	if (flags & SHADOW_CASCADE_ENABLE_BIT)
		global_defines.emplace_back("SHADOW_CASCADES", 1);
	if (flags & VOLUMETRIC_DIFFUSE_ENABLE_BIT)
		global_defines.emplace_back("VOLUMETRIC_DIFFUSE", 1);
	if (flags & FOG_ENABLE_BIT)
		global_defines.emplace_back("FOG", 1);
	if (flags & VOLUMETRIC_FOG_ENABLE_BIT)
		global_defines.emplace_back("VOLUMETRIC_FOG", 1);
	if (flags & REFRACTION_ENABLE_BIT)
		global_defines.emplace_back("REFRACTION", 1);
	if (flags & POSITIONAL_LIGHT_ENABLE_BIT)
		global_defines.emplace_back("POSITIONAL_LIGHTS", 1);
	if (flags & POSITIONAL_LIGHT_SHADOW_ENABLE_BIT)
		global_defines.emplace_back("POSITIONAL_LIGHTS_SHADOW", 1);
	if (flags & POSITIONAL_LIGHT_CLUSTER_BINDLESS_BIT)
		global_defines.emplace_back("CLUSTERER_BINDLESS", 1);
	if (flags & POSITIONAL_DECALS_BIT)
		global_defines.emplace_back("CLUSTERER_DECALS", 1);

	if (flags & SHADOW_VSM_BIT)
		global_defines.emplace_back("DIRECTIONAL_SHADOW_VSM", 1);
	if (flags & POSITIONAL_LIGHT_SHADOW_VSM_BIT)
		global_defines.emplace_back("POSITIONAL_SHADOW_VSM", 1);
	if (flags & (POSITIONAL_LIGHT_SHADOW_VSM_BIT | SHADOW_VSM_BIT))
		global_defines.emplace_back("SHADOW_RESOLVE_VSM", 1);

	if (flags & SHADOW_PCF_KERNEL_WIDE_BIT)
		global_defines.emplace_back("SHADOW_MAP_PCF_KERNEL_WIDE", 1);

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
		    (lighting.cluster->get_cluster_bindless_set() != VK_NULL_HANDLE))
		{
			flags |= POSITIONAL_LIGHT_SHADOW_ENABLE_BIT;
			if (lighting.cluster->get_shadow_type() == LightClusterer::ShadowType::VSM)
				flags |= POSITIONAL_LIGHT_SHADOW_VSM_BIT;
		}

		if (lighting.cluster->clusterer_is_bindless())
		{
			flags |= POSITIONAL_LIGHT_CLUSTER_BINDLESS_BIT;
			if (lighting.cluster->clusterer_has_volumetric_decals())
				flags |= POSITIONAL_DECALS_BIT;
		}

		if (lighting.cluster->clusterer_has_volumetric_diffuse())
			flags |= VOLUMETRIC_DIFFUSE_ENABLE_BIT;
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

void Renderer::on_pipeline_created(const DevicePipelineReadyEvent &created)
{
	device = &created.get_device();

	{
		GRANITE_SCOPED_TIMELINE_EVENT("renderer-setup-suite");
		setup_shader_suite(*device, type);
	}

	set_mesh_renderer_options_internal(renderer_options);
	for (auto &s : suite)
		s.bake_base_defines();
}

void Renderer::on_pipeline_destroyed(const DevicePipelineReadyEvent &)
{
	device = nullptr;
}

void Renderer::begin(RenderQueue &queue) const
{
	queue.reset();
	queue.set_shader_suites(suite);
	queue.set_device(device);
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
}

static void set_cluster_parameters_bindless(Vulkan::CommandBuffer &cmd, const LightClusterer &cluster)
{
	*cmd.allocate_typed_constant_data<ClustererParametersBindless>(0, BINDING_GLOBAL_CLUSTERER_PARAMETERS, 1) = cluster.get_cluster_parameters_bindless();
	cmd.set_storage_buffer(0, BINDING_GLOBAL_CLUSTER_TRANSFORM, *cluster.get_cluster_transform_buffer());
	cmd.set_storage_buffer(0, BINDING_GLOBAL_CLUSTER_BITMASK, *cluster.get_cluster_bitmask_buffer());
	cmd.set_storage_buffer(0, BINDING_GLOBAL_CLUSTER_RANGE, *cluster.get_cluster_range_buffer());
	if (cluster.clusterer_has_volumetric_decals())
	{
		cmd.set_storage_buffer(0, BINDING_GLOBAL_CLUSTER_BITMASK_DECAL, *cluster.get_cluster_bitmask_decal_buffer());
		cmd.set_storage_buffer(0, BINDING_GLOBAL_CLUSTER_RANGE_DECAL, *cluster.get_cluster_range_decal_buffer());
	}

	if (cluster.get_cluster_bindless_set() != VK_NULL_HANDLE)
	{
		cmd.set_bindless(1, cluster.get_cluster_bindless_set());

		if (cluster.clusterer_has_volumetric_diffuse())
		{
			size_t size = cluster.get_cluster_volumetric_diffuse_size();
			void *parameters = cmd.allocate_constant_data(0, BINDING_GLOBAL_VOLUMETRIC_DIFFUSE_PARAMETERS, size);
			memcpy(parameters, &cluster.get_cluster_volumetric_diffuse_data(), size);
		}

		if (cluster.clusterer_has_volumetric_fog())
		{
			size_t size = cluster.get_cluster_volumetric_fog_size();
			void *parameters = cmd.allocate_constant_data(0, BINDING_GLOBAL_VOLUMETRIC_FOG_PARAMETERS, size);
			memcpy(parameters, &cluster.get_cluster_volumetric_fog_data(), size);
		}
	}
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
	if (!lighting)
		return;

	auto *combined = cmd.allocate_typed_constant_data<CombinedRenderParameters>(0, BINDING_GLOBAL_RENDER_PARAMETERS, 1);
	memset(combined, 0, sizeof(*combined));

	cmd.set_sampler(0, BINDING_GLOBAL_LINEAR_SAMPLER, StockSampler::LinearClamp);
	cmd.set_sampler(0, BINDING_GLOBAL_SHADOW_SAMPLER, StockSampler::LinearShadow);
	cmd.set_sampler(0, BINDING_GLOBAL_GEOMETRY_SAMPLER, StockSampler::DefaultGeometryFilterClamp);

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

	auto *brdf = cmd.get_device().get_resource_manager().get_image_view_blocking(GRANITE_COMMON_RENDERER_DATA()->brdf_tables);
	VK_ASSERT(brdf);
	cmd.set_texture(0, BINDING_GLOBAL_BRDF_TABLE, *brdf, Vulkan::StockSampler::LinearClamp);

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

	if (lighting->volumetric_diffuse)
	{
		cmd.set_buffer_view(0, BINDING_GLOBAL_VOLUMETRIC_DIFFUSE_FALLBACK_VOLUME,
							lighting->volumetric_diffuse->get_fallback_volume_view());
	}
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

	CommandBufferSavedState state = {};
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

void Renderer::flush(Vulkan::CommandBuffer &cmd, RenderQueue &queue,
                     const RenderContext &context, RendererFlushFlags options,
                     const FlushParameters *params) const
{
	if ((options & SKIP_SORTING_BIT) == 0)
		queue.sort();
	flush_subset(cmd, queue, context, options | SKIP_SORTING_BIT, params, 0, 1);
}

void Renderer::flush(Vulkan::CommandBuffer &cmd, const RenderQueue &queue,
                     const RenderContext &context, RendererFlushFlags options,
                     const FlushParameters *params) const
{
	if ((options & SKIP_SORTING_BIT) == 0)
		LOGE("SKIP_SORTING was not specified!\n");
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
		debug.program = suite[ecast(RenderableType::DebugMesh)].get_program(
			VariantSignatureKey::build(DrawPipeline::Opaque,
			                           MESH_ATTRIBUTE_POSITION_BIT |
			                           MESH_ATTRIBUTE_VERTEX_COLOR_BIT, 0));
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

	cmd.set_sampler(0, BINDING_GLOBAL_LINEAR_SAMPLER, StockSampler::LinearClamp);
	cmd.set_sampler(0, BINDING_GLOBAL_SHADOW_SAMPLER, StockSampler::LinearShadow);

	auto &device = cmd.get_device();
	auto *program = device.get_shader_manager().register_graphics("builtin://shaders/lights/directional.vert",
	                                                              "builtin://shaders/lights/directional.frag");

	auto &light = *context.get_lighting_parameters();
	auto &vk11 = device.get_device_features().vk11_props;

	std::vector<std::pair<std::string, int>> defines;
	if (light.shadows && light.shadows->get_create_info().layers > 1)
	{
		defines.emplace_back("SHADOW_CASCADES", 1);
		if ((vk11.subgroupSupportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) != 0 &&
		    (vk11.subgroupSupportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) != 0 &&
		    !ImplementationQuirks::get().force_no_subgroups)
		{
			// For cascaded shadows.
			defines.emplace_back("SUBGROUP_OPS", 1);
		}
	}

	if (light.shadows)
	{
		defines.emplace_back("SHADOWS", 1);
		if (!format_has_depth_or_stencil_aspect(light.shadows->get_format()))
			defines.emplace_back("DIRECTIONAL_SHADOW_VSM", 1);
		else if (flags & Renderer::SHADOW_PCF_KERNEL_WIDE_BIT)
			defines.emplace_back("SHADOW_MAP_PCF_KERNEL_WIDE", 1);
	}

	bool cluster_volumetric_diffuse = light.cluster && light.cluster->clusterer_has_volumetric_diffuse();
	if (!cluster_volumetric_diffuse)
	{
		defines.emplace_back("VOLUMETRIC_DIFFUSE_FALLBACK", 1);
		if (light.ambient_occlusion)
			defines.emplace_back("AMBIENT_OCCLUSION", 1);
	}

	auto *variant = program->register_variant(defines);
	cmd.set_program(variant->get_program());
	cmd.set_depth_test(true, false);
	cmd.set_depth_compare(VK_COMPARE_OP_GREATER);

	auto *brdf = device.get_resource_manager().get_image_view_blocking(GRANITE_COMMON_RENDERER_DATA()->brdf_tables);
	VK_ASSERT(brdf);
	cmd.set_texture(0, BINDING_GLOBAL_BRDF_TABLE, *brdf, Vulkan::StockSampler::LinearClamp);

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
		alignas(16) vec3 color;
		alignas(16) vec3 camera_pos;
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
	push.color = light.directional.color;
	push.direction = light.directional.direction;
	push.cascade_log_bias = light.shadow.cascade_log_bias;

	push.camera_pos = context.get_render_parameters().camera_position;
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

		std::vector<std::pair<std::string, int>> cluster_defines;
		if (light.cluster->get_spot_light_shadows() ||
		    light.cluster->get_cluster_bindless_set())
		{
			cluster_defines.emplace_back("POSITIONAL_LIGHTS_SHADOW", 1);
			if (light.cluster->get_shadow_type() == LightClusterer::ShadowType::VSM)
				cluster_defines.emplace_back("POSITIONAL_SHADOW_VSM", 1);
			else if (flags & Renderer::SHADOW_PCF_KERNEL_WIDE_BIT)
				cluster_defines.emplace_back("SHADOW_MAP_PCF_KERNEL_WIDE", 1);
		}

		if (light.cluster->clusterer_is_bindless())
		{
			cluster_defines.emplace_back("CLUSTERER_BINDLESS", 1);
			if (light.cluster->get_cluster_bindless_set())
			{
				if (cluster_volumetric_diffuse)
				{
					cluster_defines.emplace_back("VOLUMETRIC_DIFFUSE", 1);
					if (light.volumetric_diffuse)
					{
						cmd.set_buffer_view(0, BINDING_GLOBAL_VOLUMETRIC_DIFFUSE_FALLBACK_VOLUME,
											light.volumetric_diffuse->get_fallback_volume_view());
					}
				}

				if (light.ambient_occlusion)
					cluster_defines.emplace_back("AMBIENT_OCCLUSION", 1);
			}
		}

		Renderer::add_subgroup_defines(device, cluster_defines, VK_SHADER_STAGE_FRAGMENT_BIT);

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
	if (renderer == RendererType::GeneralDeferred || renderer == RendererType::GeneralForward)
	{
		switch (drawable)
		{
		case RenderableType::Mesh:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/static_mesh.vert", "builtin://shaders/static_mesh.frag");
			break;

		case RenderableType::DebugMesh:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/debug_mesh.vert", "builtin://shaders/debug_mesh.frag");
			break;

		case RenderableType::DebugProbe:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/debug_probe.vert", "builtin://shaders/debug_probe.frag");
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
	else if (renderer == RendererType::DepthOnly || renderer == RendererType::MotionVector)
	{
		switch (drawable)
		{
		case RenderableType::Mesh:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/static_mesh.vert",
			                    renderer == RendererType::DepthOnly ?
			                    "builtin://shaders/static_mesh_depth.frag" :
			                    "builtin://shaders/static_mesh_mv.frag");
			break;

		case RenderableType::Ground:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/ground.vert", "builtin://shaders/dummy_depth.frag");
			break;

		case RenderableType::Ocean:
			suite.init_graphics(&device.get_shader_manager(), "builtin://shaders/ocean/ocean.vert", "builtin://shaders/dummy_depth.frag");
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
