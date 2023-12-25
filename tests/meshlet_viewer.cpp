/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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
#include "application.hpp"
#include "command_buffer.hpp"
#include "scene_loader.hpp"
#include "device.hpp"
#include "os_filesystem.hpp"
#include "muglm/muglm_impl.hpp"
#include "meshlet.hpp"
#include "aabb.hpp"
#include "event.hpp"
#include "camera.hpp"
#include "event_manager.hpp"
#include "meshlet_export.hpp"
#include "render_context.hpp"
#include "material_manager.hpp"
#include "mesh_util.hpp"
#include "flat_renderer.hpp"
#include "ui_manager.hpp"
#include "gltf.hpp"
#include "cli_parser.hpp"
#include "environment.hpp"
#include <string.h>
#include <float.h>
#include <stdexcept>

using namespace Granite;
using namespace Vulkan;
using namespace Vulkan::Meshlet;

struct MeshletRenderable : AbstractRenderable
{
	AssetID mesh;
	MaterialOffsets material;
	AABB aabb;

	void get_render_info(const RenderContext &, const RenderInfoComponent *,
	                     RenderQueue &) const override
	{
	}

	bool has_static_aabb() const override
	{
		return true;
	}

	const AABB *get_static_aabb() const override
	{
		return &aabb;
	}
};

struct MeshletViewerApplication : Granite::Application, Granite::EventHandler //, Vulkan::DebugChannelInterface
{
	explicit MeshletViewerApplication(const char *path)
	{
		GLTF::Parser parser{path};

		std::vector<AssetID> mesh_assets;
		std::vector<NodeHandle> nodes;
		mesh_assets.reserve(parser.get_meshes().size());

		std::vector<MaterialOffsets> materials;
		materials.reserve(parser.get_materials().size());

		nodes.reserve(parser.get_nodes().size());

		for (auto &mat : parser.get_materials())
		{
			AssetID albedo = GRANITE_ASSET_MANAGER()->register_asset(
					*GRANITE_FILESYSTEM(), mat.paths[int(TextureKind::BaseColor)],
					Granite::AssetClass::ImageColor);

			materials.push_back(GRANITE_MATERIAL_MANAGER()->register_material(&albedo, 1, nullptr, 0));
		}

#if 1
		unsigned count = 0;
		for (auto &mesh : parser.get_meshes())
		{
#if 0
			if (!mesh.has_material ||
			    mesh.attribute_layout[int(MeshAttribute::Normal)].format == VK_FORMAT_UNDEFINED ||
			    mesh.attribute_layout[int(MeshAttribute::UV)].format == VK_FORMAT_UNDEFINED ||
			    mesh.attribute_layout[int(MeshAttribute::Tangent)].format == VK_FORMAT_UNDEFINED)
			{
				mesh_assets.emplace_back();
				continue;
			}
#endif

			auto internal_path = std::string("memory://mesh") + std::to_string(count++);
			if (!::Granite::Meshlet::export_mesh_to_meshlet(internal_path, mesh, MeshStyle::Wireframe))
				throw std::runtime_error("Failed to export meshlet.");

			mesh_assets.push_back(GRANITE_ASSET_MANAGER()->register_asset(
					*GRANITE_FILESYSTEM(), internal_path, Granite::AssetClass::Mesh));
		}
#endif

		for (auto &node : parser.get_nodes())
		{
			if (node.joint || node.has_skin)
			{
				nodes.emplace_back();
				continue;
			}

			auto nodeptr = scene.create_node();
			auto &node_transform = nodeptr->get_transform();
			node_transform.translation = node.transform.translation;
			node_transform.rotation = node.transform.rotation;
			node_transform.scale = node.transform.scale;
			nodes.push_back(std::move(nodeptr));
		}

		for (size_t i = 0, n = nodes.size(); i < n; i++)
		{
			auto &node = parser.get_nodes()[i];
			if (nodes[i])
			{
				for (auto &child : node.children)
					if (nodes[child])
						nodes[i]->add_child(nodes[child]);

#if 1
				for (auto &mesh : node.meshes)
				{
					auto renderable = Util::make_handle<MeshletRenderable>();
					renderable->mesh = mesh_assets[mesh];
					renderable->aabb = parser.get_meshes()[mesh].static_aabb;
					//renderable->material = materials[parser.get_meshes()[mesh].material_index];
					renderable->flags |= RENDERABLE_FORCE_VISIBLE_BIT;
					scene.create_renderable(std::move(renderable), nodes[i].get());
				}
#endif
			}
		}

		auto &scene_nodes = parser.get_scenes()[parser.get_default_scene()];
		auto root = scene.create_node();

#if 1
		for (int z = -6; z <= 6; z++)
			for (int y = -6; y <= 6; y++)
				for (int x = -6; x <= 6; x++)
				{
					if (!x && !y && !z)
						continue;
					auto nodeptr = scene.create_node();
					auto &node_transform = nodeptr->get_transform();
					node_transform.translation = vec3(x, y, z) * 3.0f;
					root->add_child(nodeptr);

					auto renderable = Util::make_handle<MeshletRenderable>();
					renderable->mesh = mesh_assets.front();
					renderable->aabb = parser.get_meshes()[0].static_aabb;
					//renderable->flags |= RENDERABLE_FORCE_VISIBLE_BIT;
					scene.create_renderable(std::move(renderable), nodeptr.get());
				}
#endif

		if (false)
		{
			GeneratedMeshData mesh = create_sphere_mesh(64);
			SceneFormats::Mesh tmp;

			tmp.index_type = VK_INDEX_TYPE_UINT16;
			tmp.indices.resize(mesh.indices.size() * sizeof(uint16_t));
			memcpy(tmp.indices.data(), mesh.indices.data(), tmp.indices.size());

			tmp.position_stride = sizeof(vec3);
			tmp.positions.resize(tmp.position_stride * mesh.positions.size());
			memcpy(tmp.positions.data(), mesh.positions.data(), tmp.positions.size());

			tmp.attribute_layout[int(MeshAttribute::Position)].format = VK_FORMAT_R32G32B32_SFLOAT;
			tmp.count = mesh.indices.size();
			tmp.static_aabb = Granite::AABB{vec3(-1.0f), vec3(1.0f)};
			tmp.topology = mesh.topology;
			tmp.primitive_restart = mesh.primitive_restart;

			std::string internal_path{"memory://mesh.sphere"};
			if (!::Granite::Meshlet::export_mesh_to_meshlet(internal_path, tmp, MeshStyle::Wireframe))
				throw std::runtime_error("Failed to export meshlet.");
			AssetID sphere = GRANITE_ASSET_MANAGER()->register_asset(
					*GRANITE_FILESYSTEM(), internal_path, Granite::AssetClass::Mesh);

			auto renderable = Util::make_handle<MeshletRenderable>();
			renderable->mesh = sphere;
			renderable->aabb = tmp.static_aabb;
			renderable->flags |= RENDERABLE_FORCE_VISIBLE_BIT;
			scene.create_renderable(std::move(renderable), root.get());
		}

		for (auto &scene_node_index : scene_nodes.node_indices)
			root->add_child(nodes[scene_node_index]);
		scene.set_root_node(std::move(root));

		camera.look_at(vec3(0, 0, 30), vec3(0));

		EVENT_MANAGER_REGISTER_LATCH(MeshletViewerApplication, on_device_create, on_device_destroy, DeviceCreatedEvent);
	}

	AABB aabb;
	FPSCamera camera;
	Scene scene;
	RenderContext render_context;
	VisibilityList list;
	BindlessAllocator allocator;

	void on_device_create(const DeviceCreatedEvent &e)
	{
		e.get_device().get_shader_manager().add_include_directory("builtin://shaders/inc");
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
		allocator.reset();
	}

	void render_frame(double frame_time, double) override
	{
		scene.update_all_transforms();
		LOGI("Frame time: %.3f ms.\n", frame_time * 1e3);

		auto &wsi = get_wsi();
		auto &device = wsi.get_device();
		auto cmd = device.request_command_buffer();

		camera.set_depth_range(0.1f, 100.0f);

		render_context.set_camera(camera);

		list.clear();
		scene.gather_visible_opaque_renderables(render_context.get_visibility_frustum(), list);

		bool indirect_rendering = device.get_resource_manager().get_mesh_encoding() != ResourceManager::MeshEncoding::Classic;

		struct TaskParameters
		{
			uint32_t aabb_instance;
			uint32_t node_instance;
			uint32_t node_count_material_index; // Skinning
			uint32_t mesh_index_count;
		};

		struct DrawParameters
		{
			uint32_t meshlet_index; // Debug
			uint32_t node_instance;
			uint32_t node_count; // Skinning
		};

		std::vector<TaskParameters> task_params;
		uint32_t max_draws = 0;

		if (indirect_rendering)
		{
			for (auto &vis: list)
			{
				auto *meshlet = static_cast<const MeshletRenderable *>(vis.renderable);
				auto range = device.get_resource_manager().get_mesh_draw_range(meshlet->mesh);

				TaskParameters draw = {};
				draw.aabb_instance = vis.transform->aabb.offset;
				auto *node = vis.transform->scene_node;
				auto *skin = node->get_skin();
				draw.node_instance = skin ? skin->transform.offset : node->transform.offset;
				draw.node_count_material_index = skin ? skin->transform.count : 1;
				draw.node_count_material_index |= meshlet->material.texture_offset << 8;
				assert((range.meshlet.offset & 31) == 0);

				max_draws += range.meshlet.count;

				for (uint32_t i = 0; i < range.meshlet.count; i += 32)
				{
					draw.mesh_index_count = range.meshlet.offset + i + (std::min(range.meshlet.count - i, 32u) - 1);
					task_params.push_back(draw);
				}
			}

			if (task_params.empty())
			{
				cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::Depth));
				cmd->end_render_pass();
				device.submit(cmd);
				return;
			}
		}

		auto start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);

		BufferHandle task_buffer, cached_transform_buffer, aabb_buffer, compacted_params, indirect_draws;

		if (indirect_rendering)
		{
			BufferCreateInfo info;
			info.size = task_params.size() * sizeof(task_params.front());
			info.domain = BufferDomain::LinkedDeviceHostPreferDevice;
			info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			task_buffer = device.create_buffer(info, task_params.data());
		}

		if (indirect_rendering)
		{
			BufferCreateInfo info;
			info.size = scene.get_transforms().get_count() * sizeof(*scene.get_transforms().get_cached_transforms());
			info.domain = BufferDomain::LinkedDeviceHostPreferDevice;
			info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			cached_transform_buffer = device.create_buffer(info, scene.get_transforms().get_cached_transforms());
		}

		if (indirect_rendering)
		{
			BufferCreateInfo info;
			info.size = scene.get_aabbs().get_count() * sizeof(*scene.get_aabbs().get_aabbs());
			info.domain = BufferDomain::LinkedDeviceHostPreferDevice;
			info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			aabb_buffer = device.create_buffer(info, scene.get_aabbs().get_aabbs());
		}

		auto &manager = device.get_resource_manager();
		const bool use_meshlets = indirect_rendering && manager.get_mesh_encoding() != ResourceManager::MeshEncoding::VBOAndIBOMDI;
		bool use_preculling = !use_meshlets && indirect_rendering;

		if (indirect_rendering)
			use_preculling = Util::get_environment_bool("PRECULL", use_preculling);

		struct
		{
			vec3 camera_pos;
			uint32_t count;
			uint32_t offset;
		} push;

		push.camera_pos = render_context.get_render_parameters().camera_position;

		uint32_t target_meshlet_workgroup_size = 32;
		target_meshlet_workgroup_size = Util::get_environment_uint("MESHLET_SIZE", target_meshlet_workgroup_size);

		target_meshlet_workgroup_size = max(32u, min(256u, target_meshlet_workgroup_size));
		target_meshlet_workgroup_size = 1u << Util::floor_log2(target_meshlet_workgroup_size);
		uint32_t num_chunk_workgroups = 256u / target_meshlet_workgroup_size;

		if (use_preculling)
		{
			BufferCreateInfo info;
			if (use_meshlets)
				info.size = sizeof(VkDrawMeshTasksIndirectCommandEXT);
			else
				info.size = max_draws * sizeof(VkDrawIndexedIndirectCommand) + 256;

			info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
			             VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			info.domain = BufferDomain::Device;
			indirect_draws = device.create_buffer(info);

			if (use_meshlets)
			{
				if (num_chunk_workgroups == 1)
				{
					cmd->fill_buffer(*indirect_draws, 0, 0, 4);
					cmd->fill_buffer(*indirect_draws, 1, 4, 4);
				}
				else
				{
					cmd->fill_buffer(*indirect_draws, num_chunk_workgroups, 0, 4);
					cmd->fill_buffer(*indirect_draws, 0, 4, 4);
				}
				cmd->fill_buffer(*indirect_draws, 1, 8, 4);
			}
			else
			{
				cmd->fill_buffer(*indirect_draws, 0, 0, 256);
			}

			cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			             VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
			             VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

			info.size = max_draws * sizeof(DrawParameters);
			info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			info.domain = BufferDomain::Device;
			compacted_params = device.create_buffer(info);
		}

		BufferHandle readback_counter, readback;
		if (indirect_rendering)
		{
			BufferCreateInfo info;
			info.size = use_meshlets ? 12 : indirect_draws->get_create_info().size;
			info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			info.domain = BufferDomain::CachedHost;
			readback = device.create_buffer(info);

			if (use_meshlets)
			{
				info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
				info.domain = BufferDomain::Device;
				info.misc = BUFFER_MISC_ZERO_INITIALIZE_BIT;
				readback_counter = device.create_buffer(info);
			}
		}

		if (use_preculling)
		{
			auto *indirect = manager.get_indirect_buffer();

			auto command_words = use_meshlets ? 0 : (sizeof(VkDrawIndexedIndirectCommand) / sizeof(uint32_t));

			cmd->set_specialization_constant_mask(3);
			cmd->set_specialization_constant(0, uint32_t(command_words));
			cmd->set_specialization_constant(1, (!use_meshlets || num_chunk_workgroups == 1) ? 0 : 1);

			cmd->set_program("assets://shaders/meshlet_cull.comp");
			cmd->set_storage_buffer(0, 0, *aabb_buffer);
			cmd->set_storage_buffer(0, 1, *cached_transform_buffer);
			cmd->set_storage_buffer(0, 2, *task_buffer);
			cmd->set_storage_buffer(0, 3, indirect ? *indirect : *indirect_draws);
			cmd->set_storage_buffer(0, 4, *indirect_draws);
			cmd->set_storage_buffer(0, 5, *compacted_params);
			cmd->set_storage_buffer(0, 6, *manager.get_cluster_bounds_buffer());
			memcpy(cmd->allocate_typed_constant_data<vec4>(0, 7, 6),
			       render_context.get_visibility_frustum().get_planes(),
			       6 * sizeof(vec4));

			uint32_t count = task_params.size();
			push.count = count;
			cmd->push_constants(&push, 0, sizeof(push));

			cmd->dispatch((count + 31) / 32, 1, 1);

			cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			             VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
			             VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
		}

		auto *ibo = manager.get_index_buffer();
		auto *pos = manager.get_position_buffer();
		auto *attr = manager.get_attribute_buffer();

		bool supports_wave32 = device.supports_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_MESH_BIT_EXT);
		bool use_hierarchical = device.get_device_features().driver_id != VK_DRIVER_ID_NVIDIA_PROPRIETARY;

		if (use_meshlets)
		{
			cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::Depth));
			camera.set_aspect(cmd->get_viewport().width / cmd->get_viewport().height);
			render_context.set_camera(camera);
			cmd->set_opaque_state();

			*cmd->allocate_typed_constant_data<mat4>(1, 0, 1) = render_context.get_render_parameters().view_projection;

			*cmd->allocate_typed_constant_data<vec4>(1, 2, 1) =
					float(1 << 8 /* shader assumes 8 */) *
					vec4(cmd->get_viewport().x + 0.5f * cmd->get_viewport().width - 0.5f,
						 cmd->get_viewport().y + 0.5f * cmd->get_viewport().height - 0.5f,
						 0.5f * cmd->get_viewport().width,
						 0.5f * cmd->get_viewport().height) - vec4(1.0f, 1.0f, 0.0f, 0.0f);

			bool use_encoded = manager.get_mesh_encoding() == Vulkan::ResourceManager::MeshEncoding::MeshletEncoded;

			cmd->set_specialization_constant_mask(3);
			cmd->set_specialization_constant(0, target_meshlet_workgroup_size / 32);
			cmd->set_specialization_constant(1, num_chunk_workgroups);

			if (use_encoded)
			{
				cmd->set_storage_buffer(0, 0, *manager.get_meshlet_header_buffer());
				cmd->set_storage_buffer(0, 1, *manager.get_meshlet_stream_header_buffer());
				cmd->set_storage_buffer(0, 2, *manager.get_meshlet_payload_buffer());
			}
			else
			{
				cmd->set_storage_buffer(0, 0, *ibo);
				cmd->set_storage_buffer(0, 1, *pos);
				cmd->set_storage_buffer(0, 2, *attr);
			}

			if (!use_encoded)
				cmd->set_storage_buffer(0, 3, *manager.get_indirect_buffer());
			if (use_preculling)
				cmd->set_storage_buffer(0, 4, *compacted_params);
			cmd->set_storage_buffer(0, 5, *cached_transform_buffer);
			cmd->set_storage_buffer(0, 10, *readback_counter);
			GRANITE_MATERIAL_MANAGER()->set_bindless(*cmd, 2);

			const char *mesh_path = use_encoded ? "assets://shaders/meshlet_debug.mesh" : "assets://shaders/meshlet_debug_plain.mesh";

			supports_wave32 = Util::get_environment_bool("WAVE32", supports_wave32);
			use_hierarchical = Util::get_environment_bool("HIER_TASK", use_hierarchical);

			bool supports_wg32 = supports_wave32 && target_meshlet_workgroup_size == 32;

			if (use_preculling)
			{
				cmd->set_program("", mesh_path, "assets://shaders/meshlet_debug.mesh.frag",
				                 { { "MESHLET_SIZE", int(target_meshlet_workgroup_size) } });
				memcpy(cmd->allocate_typed_constant_data<vec4>(1, 1, 6),
				       render_context.get_visibility_frustum().get_planes(), 6 * sizeof(vec4));
			}
			else
			{
				cmd->set_program("assets://shaders/meshlet_debug.task", mesh_path,
				                 "assets://shaders/meshlet_debug.mesh.frag",
				                 { { "MESHLET_SIZE", int(target_meshlet_workgroup_size) },
				                   { "MESHLET_RENDER_TASK_HIERARCHICAL", int(use_hierarchical) },
				                   { "MESHLET_PRIMITIVE_CULL_WG32", int(supports_wg32) },
				                   { "MESHLET_PRIMITIVE_CULL_WAVE32", int(supports_wave32) } });

				cmd->set_storage_buffer(0, 6, *aabb_buffer);
				cmd->set_storage_buffer(0, 7, *task_buffer);
				cmd->set_storage_buffer(0, 8, *manager.get_cluster_bounds_buffer());
				memcpy(cmd->allocate_typed_constant_data<vec4>(0, 9, 6),
				       render_context.get_visibility_frustum().get_planes(), 6 * sizeof(vec4));
			}

			if (device.supports_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_MESH_BIT_EXT))
			{
				cmd->enable_subgroup_size_control(true, VK_SHADER_STAGE_MESH_BIT_EXT);
				cmd->set_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_MESH_BIT_EXT);
			}
			else if (device.supports_subgroup_size_log2(true, 0, 7, VK_SHADER_STAGE_MESH_BIT_EXT))
			{
				cmd->enable_subgroup_size_control(true, VK_SHADER_STAGE_MESH_BIT_EXT);
				cmd->set_subgroup_size_log2(true, 0, 7, VK_SHADER_STAGE_MESH_BIT_EXT);
			}

			if (use_preculling)
			{
				cmd->draw_mesh_tasks_indirect(*indirect_draws, 0, 1, sizeof(VkDrawMeshTasksIndirectCommandEXT));
			}
			else
			{
				uint32_t workgroups = task_params.size();
				push.count = workgroups;

				if (use_hierarchical)
					workgroups = (workgroups + 31) / 32;

				for (uint32_t i = 0; i < workgroups; i += device.get_device_features().mesh_shader_properties.maxTaskWorkGroupCount[0])
				{
					uint32_t to_dispatch = std::min(workgroups - i, device.get_device_features().mesh_shader_properties.maxTaskWorkGroupCount[0]);
					push.offset = i;
					cmd->push_constants(&push, 0, sizeof(push));
					cmd->draw_mesh_tasks(to_dispatch, 1, 1);
				}
			}
		}
		else if (manager.get_mesh_encoding() == ResourceManager::MeshEncoding::VBOAndIBOMDI)
		{
			cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::Depth));
			camera.set_aspect(cmd->get_viewport().width / cmd->get_viewport().height);
			cmd->set_opaque_state();

			*cmd->allocate_typed_constant_data<mat4>(1, 0, 1) = render_context.get_render_parameters().view_projection;

			cmd->set_program("assets://shaders/meshlet_debug.vert", "assets://shaders/meshlet_debug.frag",
			                 {{ "SINGLE_INSTANCE_RENDER", 0}});
			cmd->set_index_buffer(*ibo, 0, VK_INDEX_TYPE_UINT8_EXT);
			cmd->set_vertex_binding(0, *pos, 0, 12);
			cmd->set_vertex_binding(1, *attr, 0, 16);
			cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
			cmd->set_vertex_attrib(1, 1, VK_FORMAT_A2B10G10R10_SNORM_PACK32, 0);
			cmd->set_vertex_attrib(2, 1, VK_FORMAT_A2B10G10R10_SNORM_PACK32, 4);
			cmd->set_vertex_attrib(3, 1, VK_FORMAT_R32G32_SFLOAT, 8);

			cmd->set_storage_buffer(0, 0, *compacted_params);
			cmd->set_storage_buffer(0, 1, *cached_transform_buffer);
			cmd->set_sampler(0, 2, StockSampler::DefaultGeometryFilterWrap);
			GRANITE_MATERIAL_MANAGER()->set_bindless(*cmd, 2);

			cmd->draw_indexed_multi_indirect(*indirect_draws,
			                                 256, max_draws,
											 sizeof(VkDrawIndexedIndirectCommand),
			                                 *indirect_draws, 0);
		}
		else
		{
			cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::Depth));
			camera.set_aspect(cmd->get_viewport().width / cmd->get_viewport().height);
			cmd->set_opaque_state();

			*cmd->allocate_typed_constant_data<mat4>(1, 0, 1) = render_context.get_render_parameters().view_projection;

			cmd->set_program("assets://shaders/meshlet_debug.vert", "assets://shaders/meshlet_debug.frag",
			                 {{ "SINGLE_INSTANCE_RENDER", 1}});
			cmd->set_index_buffer(*ibo, 0, VK_INDEX_TYPE_UINT32);
			cmd->set_vertex_binding(0, *pos, 0, 12);
			cmd->set_vertex_binding(1, *attr, 0, 16);
			cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
			cmd->set_vertex_attrib(1, 1, VK_FORMAT_A2B10G10R10_SNORM_PACK32, 0);
			cmd->set_vertex_attrib(2, 1, VK_FORMAT_A2B10G10R10_SNORM_PACK32, 4);
			cmd->set_vertex_attrib(3, 1, VK_FORMAT_R32G32_SFLOAT, 8);

			cmd->set_sampler(0, 2, StockSampler::DefaultGeometryFilterWrap);
			GRANITE_MATERIAL_MANAGER()->set_bindless(*cmd, 2);

			last_mesh_invocations = 0;
			last_vert = 0;
			last_prim = 0;
			for (auto &draw : list)
			{
				auto *render = static_cast<const MeshletRenderable *>(draw.renderable);
				auto indexed = manager.get_mesh_draw_range(render->mesh).indexed;

				*cmd->allocate_typed_constant_data<mat4>(1, 1, 1) = draw.transform->get_world_transform();

				DrawParameters params = {};
				params.meshlet_index = unsigned(&draw - list.data());
				params.node_count = 1;
				params.node_instance = 0;
				cmd->push_constants(&params, 0, sizeof(params));

				last_mesh_invocations += indexed.indexCount / 3;

				cmd->draw_indexed(indexed.indexCount, indexed.instanceCount,
				                  indexed.firstIndex, indexed.vertexOffset,
				                  indexed.firstInstance);
			}
		}

		flat_renderer.begin();
		flat_renderer.render_quad(vec3(0.0f, 0.0f, 0.5f),
		                          vec2(450.0f, 120.0f),
		                          vec4(0.0f, 0.0f, 0.0f, 0.8f));
		char text[256];

		switch (manager.get_mesh_encoding())
		{
		case ResourceManager::MeshEncoding::MeshletEncoded:
			snprintf(text, sizeof(text), "%.3f ms | Meshlet (%u prim/vert) | Inline Decoding",
			         last_frame_time * 1e3, target_meshlet_workgroup_size);
			break;

		case ResourceManager::MeshEncoding::MeshletDecoded:
			snprintf(text, sizeof(text), "%.3f ms | Meshlet (%u prim/vert) | VBO Fetch",
			         last_frame_time * 1e3, target_meshlet_workgroup_size);
			break;

		case ResourceManager::MeshEncoding::VBOAndIBOMDI:
			snprintf(text, sizeof(text), "%.3f ms | MultiDrawIndirect", last_frame_time * 1e3);
			break;

		default:
			snprintf(text, sizeof(text), "%.3f ms | Classic Direct Draw", last_frame_time * 1e3);
			break;
		}

		flat_renderer.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal),
		                          text, vec3(10.0f, 10.0f, 0.0f), vec2(1000.0f));

		if (use_meshlets)
		{
			snprintf(text, sizeof(text), "Mesh shader invocations: %.3f M / %.3f M", 1e-6 * last_mesh_invocations,
			         1e-6 * double(max_draws * MaxElements));
		}
		else if (indirect_rendering)
		{
			snprintf(text, sizeof(text), "MDI primitives: %.3f M / %.3f M", 1e-6 * last_mesh_invocations,
			         1e-6 * double(max_draws * MaxElements));
		}
		else
		{
			snprintf(text, sizeof(text), "Direct primitives: %.3f M", 1e-6 * last_mesh_invocations);
		}

		flat_renderer.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal),
		                          text, vec3(10.0f, 30.0f, 0.0f), vec2(1000.0f));

		snprintf(text, sizeof(text), "ComputeCull %d | mesh wave32 %d | task hier %d",
		         int(use_preculling), int(supports_wave32), int(use_hierarchical));
		flat_renderer.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal),
		                          text, vec3(10.0f, 50.0f, 0.0f), vec2(1000.0f));

		if (use_meshlets)
		{
			snprintf(text, sizeof(text), "Primitives: %.3f M", 1e-6 * last_prim);
			flat_renderer.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal),
			                          text, vec3(10.0f, 70.0f, 0.0f), vec2(1000.0f));
			snprintf(text, sizeof(text), "Vertices: %.3f M", 1e-6 * last_vert);
			flat_renderer.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal),
			                          text, vec3(10.0f, 90.0f, 0.0f), vec2(1000.0f));
		}

		flat_renderer.flush(*cmd, vec3(0.0f), vec3(cmd->get_viewport().width, cmd->get_viewport().height, 1.0f));
		cmd->end_render_pass();

		auto end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);

		if (indirect_rendering)
		{
			cmd->barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			             VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_READ_BIT);
			if (use_meshlets)
				cmd->copy_buffer(*readback, *readback_counter);
			else
				cmd->copy_buffer(*readback, *indirect_draws);
			cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
		}

		Fence fence;
		device.submit(cmd, &fence);

		start_timestamps[readback_index] = std::move(start_ts);
		end_timestamps[readback_index] = std::move(end_ts);
		readback_ring[readback_index] = std::move(readback);
		readback_fence[readback_index] = std::move(fence);
		readback_index = (readback_index + 1) & 3;

		if (start_timestamps[readback_index] && start_timestamps[readback_index]->is_signalled() &&
		    end_timestamps[readback_index] && end_timestamps[readback_index]->is_signalled())
		{
			last_frame_time = device.convert_device_timestamp_delta(
					start_timestamps[readback_index]->get_timestamp_ticks(),
					end_timestamps[readback_index]->get_timestamp_ticks());
		}

		if (indirect_rendering)
		{
			if (readback_fence[readback_index])
			{
				readback_fence[readback_index]->wait();
				auto *mapped = static_cast<const uint32_t *>(device.map_host_buffer(*readback_ring[readback_index],
				                                                                    MEMORY_ACCESS_READ_BIT));

				if (use_meshlets)
				{
					last_mesh_invocations = mapped[0];
					last_prim = mapped[1];
					last_vert = mapped[2];
				}
				else
				{
					last_mesh_invocations = 0;
					uint32_t draws = mapped[0];
					mapped += 256 / sizeof(uint32_t);
					for (uint32_t i = 0; i < draws; i++, mapped += sizeof(VkDrawIndexedIndirectCommand) / sizeof(uint32_t))
						last_mesh_invocations += mapped[0] / 3;
				}
			}
		}
	}

	BufferHandle readback_ring[4];
	Fence readback_fence[4];
	unsigned readback_index = 0;
	unsigned last_mesh_invocations = 0;
	unsigned last_prim = 0;
	unsigned last_vert = 0;
	double last_frame_time = 0.0;
	FlatRenderer flat_renderer;

	QueryPoolHandle start_timestamps[4];
	QueryPoolHandle end_timestamps[4];

#if 0
	void message(const std::string &tag, uint32_t code, uint32_t x, uint32_t y, uint32_t z, uint32_t,
	             const Word *words) override
	{
		if (x || y || z)
			return;

		LOGI("%.3f %.3f %.3f %.3f\n", words[0].f32, words[1].f32, words[2].f32, words[3].f32);
	}
#endif
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	const char *path = nullptr;

	Util::CLICallbacks cbs;
	cbs.add("--size", [](Util::CLIParser &parser) { Util::set_environment("MESHLET_SIZE", parser.next_string()); });
	cbs.add("--encoding", [](Util::CLIParser &parser) { Util::set_environment("GRANITE_MESH_ENCODING", parser.next_string()); });
	cbs.add("--hier-task", [](Util::CLIParser &parser) { Util::set_environment("HIER_TASK", parser.next_string()); });
	cbs.add("--wave32", [](Util::CLIParser &parser) { Util::set_environment("WAVE32", parser.next_string()); });
	cbs.add("--precull", [](Util::CLIParser &parser) { Util::set_environment("PRECULL", parser.next_string()); });
	cbs.default_handler = [&](const char *arg) { path = arg; };

	Util::CLIParser parser(std::move(cbs), argc - 1, argv + 1);
	if (!parser.parse() || parser.is_ended_state() || !path)
	{
		LOGE("Usage: meshlet-viewer path.msh1\n");
		return nullptr;
	}

	try
	{
		auto *app = new MeshletViewerApplication(argv[1]);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
