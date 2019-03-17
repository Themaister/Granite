/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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

#include "application.hpp"
#include "os_filesystem.hpp"
#include "scene.hpp"
#include "device.hpp"
#include "mesh_util.hpp"
#include "renderer.hpp"
#include "render_context.hpp"
#include "render_components.hpp"
#include "global_managers.hpp"
#include "physics_system.hpp"

using namespace Granite;

struct PhysicsSandboxApplication : Application, EventHandler
{
	PhysicsSandboxApplication()
		: renderer(RendererType::GeneralForward)
	{
		camera.set_position(vec3(0.0f, 2.0f, 8.0f));
		cube = Util::make_handle<CubeMesh>();
		sphere = Util::make_handle<SphereMesh>();
		init_plane();
		init_scene();
		EVENT_MANAGER_REGISTER_LATCH(PhysicsSandboxApplication, on_swapchain_created, on_swapchain_destroyed, Vulkan::SwapchainParameterEvent);
		EVENT_MANAGER_REGISTER(PhysicsSandboxApplication, on_key, KeyboardEvent);
	}

	void on_swapchain_created(const Vulkan::SwapchainParameterEvent &swap)
	{
		camera.set_aspect(swap.get_aspect_ratio());
		camera.set_fovy(0.4f * pi<float>());
		camera.set_depth_range(0.1f, 500.0f);
	}

	void on_swapchain_destroyed(const Vulkan::SwapchainParameterEvent &)
	{
	}

	void init_plane()
	{
		SceneFormats::Mesh mesh;
		mesh.count = 4;

		const vec3 positions[4] = {
			vec3(-1000.0f, 0.0f, -1000.0f),
			vec3(-1000.0f, 0.0f, +1000.0f),
			vec3(+1000.0f, 0.0f, -1000.0f),
			vec3(+1000.0f, 0.0f, +1000.0f),
		};

		const vec2 uvs[4] = {
			vec2(-1000.0f, -1000.0f),
			vec2(-1000.0f, +1000.0f),
			vec2(+1000.0f, -1000.0f),
			vec2(+1000.0f, +1000.0f),
		};

		mesh.positions.resize(sizeof(positions));
		memcpy(mesh.positions.data(), positions, sizeof(positions));
		mesh.attributes.resize(sizeof(uvs));
		memcpy(mesh.attributes.data(), uvs, sizeof(uvs));
		mesh.position_stride = sizeof(vec3);
		mesh.attribute_stride = sizeof(vec2);
		mesh.attribute_layout[Util::ecast(MeshAttribute::Position)].format = VK_FORMAT_R32G32B32_SFLOAT;
		mesh.attribute_layout[Util::ecast(MeshAttribute::UV)].format = VK_FORMAT_R32G32_SFLOAT;
		mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		mesh.has_material = true;
		mesh.material_index = 0;
		mesh.static_aabb = AABB(vec3(-1000.0f, -1.0f, -1000.0f), vec3(+1000.0f, 0.0f, +1000.0f));

		SceneFormats::MaterialInfo info;
		info.pipeline = DrawPipeline::Opaque;
		info.base_color.path = "builtin://textures/checkerboard.png";
		info.bandlimited_pixel = true;
		info.uniform_roughness = 1.0f;
		info.uniform_metallic = 0.0f;
		plane = Util::make_handle<ImportedMesh>(mesh, info);
	}

	void init_scene()
	{
		auto root_node = scene.create_node();
		auto entity = scene.create_renderable(plane, root_node.get());
		entity->allocate_component<PhysicsComponent>()->handle =
				Global::physics()->add_infinite_plane(vec4(0.0f, 1.0f, 0.0f, 0.0f));

		{
			auto cube_node = scene.create_node();
			cube_node->transform.translation = vec3(5.0f, 3.0f, 0.0f);
			cube_node->invalidate_cached_transform();
			root_node->add_child(cube_node);
			auto entity = scene.create_renderable(cube, cube_node.get());
			sphere_physics = entity->allocate_component<PhysicsComponent>()->handle =
					Global::physics()->add_cube(cube_node.get(), 5.0f);
		}

		{
			auto sphere_node = scene.create_node();
			sphere_node->transform.translation = vec3(3.8f, 18.0f, 1.1f);
			sphere_node->invalidate_cached_transform();
			root_node->add_child(sphere_node);
			auto entity = scene.create_renderable(sphere, sphere_node.get());
			entity->allocate_component<PhysicsComponent>()->handle =
					Global::physics()->add_sphere(sphere_node.get(), 5.0f);
		}

		scene.set_root_node(root_node);

		context.set_lighting_parameters(&lighting);
	}

	bool on_key(const KeyboardEvent &e)
	{
		if (e.get_key() == Key::Space && e.get_key_state() == KeyState::Pressed)
		{
			Global::physics()->apply_impulse(sphere_physics,
					vec3(0.0f, 12.0f, -4.0f),
					vec3(0.2f, 0.0f, 0.0f));
		}

		return true;
	}

	void render_frame(double frame_time, double elapsed_time) override
	{
		Global::physics()->iterate(frame_time);
		scene.update_cached_transforms();

		lighting.directional.direction = normalize(vec3(1.0f, 0.5f, 1.0f));
		lighting.directional.color = vec3(1.0f, 0.8f, 0.6f);
		renderer.set_mesh_renderer_options_from_lighting(lighting);
		context.set_camera(camera);
		visible.clear();
		scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);

		auto cmd = get_wsi().get_device().request_command_buffer();
		auto rp = get_wsi().get_device().get_swapchain_render_pass(Vulkan::SwapchainRenderPass::Depth);
		rp.clear_color[0].float32[0] = 0.01f;
		rp.clear_color[0].float32[1] = 0.02f;
		rp.clear_color[0].float32[2] = 0.03f;
		cmd->begin_render_pass(rp);

		renderer.begin();
		renderer.push_renderables(context, visible);
		renderer.flush(*cmd, context, 0);

		cmd->end_render_pass();

		get_wsi().get_device().submit(cmd);
	}

	Scene scene;
	AbstractRenderableHandle cube;
	AbstractRenderableHandle sphere;
	AbstractRenderableHandle plane;
	FPSCamera camera;
	RenderContext context;
	LightingParameters lighting;
	VisibilityList visible;
	Renderer renderer;

	PhysicsHandle *sphere_physics = nullptr;
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	if (argc < 1)
		return nullptr;

	application_dummy();

	try
	{
		auto *app = new PhysicsSandboxApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}