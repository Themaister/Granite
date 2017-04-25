#include "vulkan.hpp"
#include "wsi.hpp"
#include "scene.hpp"
#include "render_context.hpp"
#include "camera.hpp"
#include "mesh_util.hpp"
#include "renderer.hpp"
#include "material_manager.hpp"
#include "gltf.hpp"
#include "animation_system.hpp"
#include "importers.hpp"
#include <string.h>

using namespace Vulkan;
using namespace Granite;
using namespace Util;

int main()
{
	Vulkan::WSI wsi;
	wsi.init(1280, 720);

	RenderContext context;
	FPSCamera cam;
	cam.look_at(vec3(0.0f, 0.0f, 8.0f), vec3(0.0f));
	context.set_camera(cam);

	Scene scene;
	VisibilityList visible;

	auto skybox = Util::make_abstract_handle<AbstractRenderable, Skybox>("assets://textures/skybox.ktx");
	scene.create_renderable(skybox, nullptr);

	auto &device = wsi.get_device();

	//GLTF::Parser parser("assets://scenes/AnimatedCube.gltf");
	GLTF::Parser parser("assets://scenes/SimpleSkin.gltf");

	std::vector<AbstractRenderableHandle> meshes;
	meshes.reserve(parser.get_meshes().size());
	for (auto &mesh : parser.get_meshes())
	{
		Importer::MaterialInfo default_material;
		default_material.uniform_base_color = vec4(0.0f, 1.0f, 0.0f, 1.0f);
		AbstractRenderableHandle gltf;

		bool skinned = mesh.attribute_layout[ecast(MeshAttribute::BoneIndex)].format != VK_FORMAT_UNDEFINED;
		if (skinned)
		{
			if (mesh.has_material)
				gltf = Util::make_abstract_handle<AbstractRenderable, ImportedSkinnedMesh>(mesh,
				                                                                           parser.get_materials()[mesh.material_index]);
			else
				gltf = Util::make_abstract_handle<AbstractRenderable, ImportedSkinnedMesh>(mesh, default_material);
		}
		else
		{
			if (mesh.has_material)
				gltf = Util::make_abstract_handle<AbstractRenderable, ImportedMesh>(mesh,
				                                                                    parser.get_materials()[mesh.material_index]);
			else
				gltf = Util::make_abstract_handle<AbstractRenderable, ImportedMesh>(mesh, default_material);
		}
		meshes.push_back(gltf);
	}

	std::vector<Scene::NodeHandle> nodes;
	nodes.reserve(parser.get_nodes().size());
	for (auto &node : parser.get_nodes())
	{
		if (!node.joint)
		{
			Scene::NodeHandle nodeptr;
			if (node.has_skin)
				nodeptr = scene.create_skinned_node(parser.get_skins()[node.skin]);
			else
				nodeptr = scene.create_node();

			nodes.push_back(nodeptr);
			nodeptr->transform.translation = node.transform.translation;
			nodeptr->transform.rotation = node.transform.rotation;
			nodeptr->transform.scale = node.transform.scale;
		}
		else
			nodes.push_back({});
	}

	AnimationSystem animation_system;
	size_t i = 0;
	for (auto &node : parser.get_nodes())
	{
		if (nodes[i])
		{
			for (auto &child : node.children)
				nodes[i]->add_child(nodes[child]);
			for (auto &mesh : node.meshes)
				scene.create_renderable(meshes[mesh], nodes[i].get());

			animation_system.add_node(nodes[i]);
		}
		i++;
	}

	for (auto &anim : parser.get_animations())
		animation_system.register_animation("cube", anim);
	animation_system.start_animation(*nodes[0], "cube", wsi.get_elapsed_time(), true);

	auto root = scene.create_node();
	for (auto &node : nodes)
		if (node && !node->get_parent())
			root->add_child(node);

	scene.set_root_node(root);

	Renderer renderer;
	while (wsi.alive())
	{
		Filesystem::get().poll_notifications();
		wsi.begin_frame();

		context.set_camera(cam);
		visible.clear();

		animation_system.animate(wsi.get_elapsed_time());
		scene.update_cached_transforms();
		scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
		scene.gather_background_renderables(visible);

		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::DepthStencil);
		rp.clear_color->float32[0] = 0.2f;
		rp.clear_color->float32[1] = 0.2f;
		rp.clear_color->float32[2] = 0.3f;
		cmd->begin_render_pass(rp);
		renderer.render(*cmd, context, visible);
		cmd->end_render_pass();
		device.submit(cmd);

		wsi.end_frame();
	}
}