#include "util.hpp"
#include "compiler.hpp"
#include "filesystem.hpp"
#include "event.hpp"
#include "path.hpp"
#include "render_queue.hpp"
#include "vulkan_events.hpp"
#include <unistd.h>
#include <string.h>
#include <wsi/wsi.hpp>
#include <shader_manager.hpp>
#include "ecs.hpp"
#include "math.hpp"
#include "scene.hpp"
#include "frustum.hpp"

#include "mesh_util.hpp"

using namespace Granite;
using namespace std;
using namespace Util;

struct Position : ComponentBase
{
	vec3 pos;
	~Position()
	{
		LOGI("Position dtor.\n");
	}
};

struct Velocity : ComponentBase
{
	vec3 vel;
	~Velocity()
	{
		LOGI("Velocity dtor.\n");
	}
};

int main()
{
	Scene scene;
	scene.create_renderable(Util::make_abstract_handle<AbstractRenderable, CubeMesh>());

	EntityPool pool;
	auto entity = pool.create_entity();
	auto *pos = entity->allocate_component<Position>();
	pos->pos = vec3(1.0f, 1.0f, 1.0f);
	auto *vel = entity->allocate_component<Velocity>();
	auto &group = pool.get_component_group<Position, Velocity>();

	auto entity2 = pool.create_entity();
	auto *pos2 = entity2->allocate_component<Position>();
	pos2->pos = vec3(3.0f, 3.0f, 3.0f);
	auto *vel2 = entity2->allocate_component<Velocity>();

	auto &group2 = pool.get_component_group<Position>();
	auto &group3 = pool.get_component_group<Velocity>();

	entity2->free_component<Velocity>();

	for (auto &tmp : group2)
	{
		Position *pos;
		tie(pos) = tmp;

		LOGI("Pos.x = %f\n", pos->pos.x);
	}

	LOGI(":D\n");

	class Handler : public EventHandler
	{
	public:
		void device_create(const Event &e)
		{
			auto &ev = e.as<Vulkan::DeviceCreatedEvent>();
			LOGI("Create device: %p\n", &ev.get_device());
		}

		void device_destroy(const Event &e)
		{
			auto &ev = e.as<Vulkan::DeviceCreatedEvent>();
			LOGI("Destroy device: %p\n", &ev.get_device());
		}

		void swapchain_create(const Event &e)
		{
			auto &ev = e.as<Vulkan::SwapchainParameterEvent>();
			LOGI("Create swapchain: %u x %u x %u\n", ev.get_width(), ev.get_height(), ev.get_image_count());
		}

		void swapchain_destroy(const Event &e)
		{
			auto &ev = e.as<Vulkan::SwapchainParameterEvent>();
			LOGI("Destroy swapchain: %u x %u x %u\n", ev.get_width(), ev.get_height(), ev.get_image_count());
		}

		void swapchain_index(const Event &e)
		{
			auto &ev = e.as<Vulkan::SwapchainIndexEvent>();
			LOGI("Swapchain index: %u\n", ev.get_index());
		}

		void swapchain_index_end(const Event &)
		{}
	} handler;

	auto &em = Granite::EventManager::get_global();
	em.register_latch_handler(Vulkan::DeviceCreatedEvent::type_id, &Handler::device_create, &Handler::device_destroy, &handler);
	em.register_latch_handler(Vulkan::SwapchainParameterEvent::type_id, &Handler::swapchain_create, &Handler::swapchain_destroy, &handler);
	em.register_latch_handler(Vulkan::SwapchainIndexEvent::type_id, &Handler::swapchain_index, &Handler::swapchain_index_end, &handler);

	try {
		Vulkan::WSI wsi;
		if (!wsi.init(1280, 720))
			return 1;

		auto &device = wsi.get_device();

		auto &sm = device.get_shader_manager();
		auto *pass0 = sm.register_graphics("assets://shaders/quad.vert", "assets://shaders/quad.frag");
		auto *pass1 = sm.register_graphics("assets://shaders/quad.vert", "assets://shaders/depth.frag");
		unsigned v0 = pass0->register_variant({});
		unsigned v2 = pass1->register_variant({{{"FOOBAR", 0}, { "BOO", 2 }}});
		unsigned v1 = pass1->register_variant({{{"FOOBAR", 1}, { "BOO", 2 }}});
		auto *tex = device.get_texture_manager().request_texture("assets://textures/test.png");

		while (wsi.alive())
		{
			Filesystem::get().poll_notifications();
			wsi.begin_frame();

			auto cmd = device.request_command_buffer();
			auto rp = device.get_swapchain_render_pass(Vulkan::SwapchainRenderPass::ColorOnly);
			rp.clear_color[0].float32[0] = 0.0f;
			rp.clear_color[0].float32[1] = 0.0f;
			rp.clear_color[0].float32[2] = 0.0f;
			rp.clear_color[0].float32[3] = 0.0f;
			rp.clear_depth_stencil.depth = 0.2f;
			rp.clear_depth_stencil.stencil = 128;
			rp.num_color_attachments = 2;
			rp.color_attachments[1] = &device.get_transient_attachment(
				rp.color_attachments[0]->get_image().get_create_info().width,
				rp.color_attachments[0]->get_image().get_create_info().height,
				rp.color_attachments[0]->get_image().get_create_info().format,
				0, 4);

			rp.depth_stencil = &device.get_transient_attachment(
				rp.color_attachments[0]->get_image().get_create_info().width,
				rp.color_attachments[0]->get_image().get_create_info().height,
				device.get_default_depth_stencil_format(),
			    0, 4);

			Vulkan::RenderPassInfo::Subpass subpasses[1] = {};
			subpasses[0].num_color_attachments = 1;
			subpasses[0].color_attachments[0] = 1;
			subpasses[0].num_resolve_attachments = 1;
			subpasses[0].resolve_attachments[0] = 0;
			subpasses[0].depth_stencil_mode = Vulkan::RenderPassInfo::DepthStencil::ReadWrite;
			rp.num_subpasses = 1;
			rp.subpasses = subpasses;

			cmd->begin_render_pass(rp);

			cmd->set_program(*pass0->get_program(0));
			static const int8_t quad[] = {
				-128, -128,
				+127, -128,
				-128, +127,
				+127, +127,
			};
			memcpy(cmd->allocate_vertex_data(0, 6, 2), quad, sizeof(quad));
			cmd->set_quad_state();
			cmd->set_depth_test(true, true);
			cmd->set_depth_compare(VK_COMPARE_OP_LESS_OR_EQUAL);
			cmd->set_vertex_attrib(0, 0, VK_FORMAT_R8G8_SNORM, 0);
			cmd->set_texture(0, 0, tex->get_image()->get_view(), Vulkan::StockSampler::TrilinearClamp);
			cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
			cmd->draw(3);
			cmd->end_render_pass();

			device.submit(cmd);
			wsi.end_frame();
		}
	}
    catch (const exception &e)
	{
		LOGE("Exception: %s\n", e.what());
	}

#if 0
	GLSLCompiler compiler;
	compiler.set_source_from_file(Filesystem::get(), "/tmp/test.frag");
	compiler.preprocess();
	auto spirv = compiler.compile();

	if (spirv.empty())
		LOGE("GLSL: %s\n", compiler.get_error_message().c_str());

	for (auto &dep : compiler.get_dependencies())
		LOGI("Dependency: %s\n", dep.c_str());
	for (auto &dep : compiler.get_variants())
		LOGI("Variant: %s\n", dep.first.c_str());

	auto &fs = Filesystem::get();
	auto file = fs.open("/tmp/foobar", Filesystem::Mode::WriteOnly);
	const string foo = ":D";
	memcpy(file->map_write(foo.size()), foo.data(), foo.size());
#endif
}
