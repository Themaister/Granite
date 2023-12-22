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
#include "gltf.hpp"
#include <string.h>
#include <float.h>
#include <stdexcept>

using namespace Granite;
using namespace Vulkan;
using namespace Vulkan::Meshlet;

static uint32_t style_to_u32_streams(MeshStyle style)
{
	switch (style)
	{
	case MeshStyle::Wireframe:
		return 3;
	case MeshStyle::Textured:
		return 7;
	case MeshStyle::Skinned:
		return 9;
	default:
		return 0;
	}
}

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

struct MeshletViewerApplication : Granite::Application, Granite::EventHandler, Vulkan::DebugChannelInterface
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
		for (int z = -10; z <= 10; z++)
			for (int y = -10; y <= 10; y++)
				for (int x = -10; x <= 10; x++)
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
					renderable->flags |= RENDERABLE_FORCE_VISIBLE_BIT;
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

		for (auto &vis : list)
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
			assert((range.offset & 31) == 0);

			max_draws += range.count;

			for (uint32_t i = 0; i < range.count; i += 32)
			{
				draw.mesh_index_count = range.offset + i + (std::min(range.count - i, 32u) - 1);
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

		BufferHandle task_buffer, cached_transform_buffer, aabb_buffer, compacted_params, indirect_draws;

		{
			BufferCreateInfo info;
			info.size = task_params.size() * sizeof(task_params.front());
			info.domain = BufferDomain::LinkedDeviceHostPreferDevice;
			info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			task_buffer = device.create_buffer(info, task_params.data());
		}

		{
			BufferCreateInfo info;
			info.size = scene.get_transforms().get_count() * sizeof(*scene.get_transforms().get_cached_transforms());
			info.domain = BufferDomain::LinkedDeviceHostPreferDevice;
			info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			cached_transform_buffer = device.create_buffer(info, scene.get_transforms().get_cached_transforms());
		}

		{
			BufferCreateInfo info;
			info.size = scene.get_aabbs().get_count() * sizeof(*scene.get_aabbs().get_aabbs());
			info.domain = BufferDomain::LinkedDeviceHostPreferDevice;
			info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			aabb_buffer = device.create_buffer(info, scene.get_aabbs().get_aabbs());
		}

		auto &manager = device.get_resource_manager();

		BufferHandle readback_counter, readback;
		{
			BufferCreateInfo info;
			info.size = 4;
			info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			info.domain = BufferDomain::LinkedDeviceHost;
			readback = device.create_buffer(info);

			info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			info.domain = BufferDomain::Device;
			info.misc = BUFFER_MISC_ZERO_INITIALIZE_BIT;
			readback_counter = device.create_buffer(info);
		}

		struct
		{
			vec3 camera_pos;
			uint32_t count;
			uint32_t offset;
		} push;

		push.camera_pos = render_context.get_render_parameters().camera_position;
		const bool use_meshlets = manager.get_mesh_encoding() != Vulkan::ResourceManager::MeshEncoding::VBOAndIBOMDI;
		const bool use_preculling = !use_meshlets;

		uint32_t target_meshlet_workgroup_size =
		    max(32u, device.get_device_features().mesh_shader_properties.maxPreferredMeshWorkGroupInvocations);

		target_meshlet_workgroup_size = min(256u, target_meshlet_workgroup_size);
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

			{
				BufferCreateInfo info;
				info.size = max_draws * sizeof(DrawParameters);
				info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
				info.domain = BufferDomain::Device;
				compacted_params = device.create_buffer(info);
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

			bool use_hierarchical = device.get_device_features().driver_id != VK_DRIVER_ID_NVIDIA_PROPRIETARY;
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
			GRANITE_MATERIAL_MANAGER()->set_bindless(*cmd, 2);

			const char *mesh_path = use_encoded ? "assets://shaders/meshlet_debug.mesh" : "assets://shaders/meshlet_debug_plain.mesh";

			bool supports_wave32 = device.supports_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_MESH_BIT_EXT);
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
			cmd->end_render_pass();
		}
		else
		{
			cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::Depth));
			camera.set_aspect(cmd->get_viewport().width / cmd->get_viewport().height);
			cmd->set_opaque_state();

			*cmd->allocate_typed_constant_data<mat4>(1, 0, 1) = render_context.get_render_parameters().view_projection;

			cmd->set_program("assets://shaders/meshlet_debug.vert", "assets://shaders/meshlet_debug.frag");
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

			cmd->end_render_pass();
			cmd->barrier(VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0,
						 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_READ_BIT);
			cmd->copy_buffer(*readback, 0, *indirect_draws, 0, sizeof(uint32_t));
			cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
						 VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
		}

		Fence fence;
		device.submit(cmd, &fence);
		//fence->wait();
		//LOGI("Number of draws: %u\n",
		//     *static_cast<const uint32_t *>(device.map_host_buffer(*readback, MEMORY_ACCESS_READ_BIT)));
	}

	void message(const std::string &tag, uint32_t code, uint32_t x, uint32_t y, uint32_t z, uint32_t,
	             const Word *words) override
	{
		if (x || y || z)
			return;

		LOGI("%.3f %.3f %.3f %.3f\n", words[0].f32, words[1].f32, words[2].f32, words[3].f32);
	}
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	if (argc != 2)
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
