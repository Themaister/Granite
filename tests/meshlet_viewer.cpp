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

	void render_frame(double, double) override
	{
		scene.update_all_transforms();

		auto &wsi = get_wsi();
		auto &device = wsi.get_device();
		auto cmd = device.request_command_buffer();
		cmd->begin_debug_channel(this, "cull", 16 * 1024 * 1024);

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
			uint32_t node_instance;
			uint32_t node_count; // Skinning
			uint32_t meshlet_index; // Debug
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

		BufferHandle task_buffer, cached_transform_buffer, aabb_buffer;

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

		BufferHandle readback;
		{
			BufferCreateInfo info;
			info.size = 4;
			info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			info.domain = BufferDomain::CachedHost;
			readback = device.create_buffer(info);
		}

		if (manager.get_mesh_encoding() == Vulkan::ResourceManager::MeshEncoding::Meshlet)
		{
			auto *header_buffer = manager.get_meshlet_header_buffer();
			auto *stream_header_buffer = manager.get_meshlet_stream_header_buffer();
			auto *payload_buffer = manager.get_meshlet_payload_buffer();

			cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::Depth));
			camera.set_aspect(cmd->get_viewport().width / cmd->get_viewport().height);
			render_context.set_camera(camera);
			cmd->set_opaque_state();

			*cmd->allocate_typed_constant_data<mat4>(1, 0, 1) = render_context.get_render_parameters().view_projection;
			memcpy(cmd->allocate_typed_constant_data<vec4>(1, 1, 6), render_context.get_visibility_frustum().get_planes(),
			       6 * sizeof(vec4));

			bool large_workgroup =
					device.get_device_features().mesh_shader_properties.maxPreferredMeshWorkGroupInvocations > 32 &&
					device.get_device_features().mesh_shader_properties.maxMeshWorkGroupInvocations >= 256;

			cmd->set_program("assets://shaders/meshlet_debug.task", "assets://shaders/meshlet_debug.mesh",
			                 "assets://shaders/meshlet_debug.mesh.frag",
			                 {{"MESHLET_PAYLOAD_LARGE_WORKGROUP", int(large_workgroup)}});
			cmd->set_storage_buffer(0, 0, *aabb_buffer);
			cmd->set_storage_buffer(0, 1, *cached_transform_buffer);
			cmd->set_storage_buffer(0, 2, *task_buffer);
			cmd->set_storage_buffer(0, 3, *header_buffer);
			cmd->set_storage_buffer(0, 4, *stream_header_buffer);
			cmd->set_storage_buffer(0, 5, *payload_buffer);

			cmd->set_sampler(0, 6, StockSampler::DefaultGeometryFilterWrap);
			GRANITE_MATERIAL_MANAGER()->set_bindless(*cmd, 2);

			cmd->set_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_TASK_BIT_EXT);
			cmd->set_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_MESH_BIT_EXT);
			cmd->enable_subgroup_size_control(true, VK_SHADER_STAGE_MESH_BIT_EXT);
			cmd->enable_subgroup_size_control(true, VK_SHADER_STAGE_TASK_BIT_EXT);
			cmd->set_specialization_constant_mask(1);
			cmd->set_specialization_constant(0, style_to_u32_streams(MeshStyle::Textured));

			uint32_t count = task_params.size();
			cmd->push_constants(&count, 0, sizeof(count));
			cmd->draw_mesh_tasks((count + 31) / 32, 1, 1);
			cmd->end_render_pass();
		}
		else
		{
			auto *ibo = manager.get_index_buffer();
			auto *pos = manager.get_position_buffer();
			auto *attr = manager.get_attribute_buffer();
			auto *indirect = manager.get_indirect_buffer();

			BufferHandle indirect_draws, compacted_params;
			{
				BufferCreateInfo info;
				info.size = max_draws * sizeof(VkDrawIndexedIndirectCommand) + 256;
				info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
				info.domain = BufferDomain::Device;
				info.misc = BUFFER_MISC_ZERO_INITIALIZE_BIT;
				indirect_draws = device.create_buffer(info);
			}

			{
				BufferCreateInfo info;
				info.size = max_draws * sizeof(DrawParameters);
				info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
				info.domain = BufferDomain::Device;
				compacted_params = device.create_buffer(info);
			}

			cmd->set_program("assets://shaders/meshlet_cull.comp");
			cmd->set_storage_buffer(0, 0, *aabb_buffer);
			cmd->set_storage_buffer(0, 1, *cached_transform_buffer);
			cmd->set_storage_buffer(0, 2, *task_buffer);
			cmd->set_storage_buffer(0, 3, *indirect);
			cmd->set_storage_buffer(0, 4, *indirect_draws);
			cmd->set_storage_buffer(0, 5, *compacted_params);
			cmd->set_storage_buffer(0, 6, *manager.get_cluster_bounds_buffer());
			memcpy(cmd->allocate_typed_constant_data<vec4>(0, 7, 6),
			       render_context.get_visibility_frustum().get_planes(),
			       6 * sizeof(vec4));
			uint32_t count = task_params.size();

			struct
			{
				vec3 camera_pos;
				uint32_t count;
			} push;

			push.camera_pos = render_context.get_render_parameters().camera_position;
			push.count = count;

			cmd->push_constants(&push, 0, sizeof(push));
			cmd->enable_subgroup_size_control(true, VK_SHADER_STAGE_COMPUTE_BIT);
			cmd->set_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_COMPUTE_BIT);
			cmd->dispatch((count + 31) / 32, 1, 1);

			cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			             VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			             VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

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
			                                 256, max_draws, sizeof(VkDrawIndexedIndirectCommand),
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
		fence->wait();
		LOGI("Number of draws: %u\n",
			 *static_cast<const uint32_t *>(device.map_host_buffer(*readback, MEMORY_ACCESS_READ_BIT)));
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
