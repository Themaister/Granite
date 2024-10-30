#define NOMINMAX
#include "application.hpp"
#include "command_buffer.hpp"
#include "device.hpp"
#include "os_filesystem.hpp"
#include "cli_parser.hpp"
#include <string.h>

using namespace Granite;
using namespace Vulkan;

struct Options
{
	uint32_t max_count = 1;
	uint32_t indirect_count = 1;
	uint32_t iterations = 1;
	uint32_t primitives_per_draw = 1;
	uint32_t frames = 1000;
	bool use_indirect_count = false;
	bool use_indirect = false;
	bool use_mdi = false;
	bool use_dgc = false;
};

struct DGCTriangleApplication : Granite::Application, Granite::EventHandler
{
	explicit DGCTriangleApplication(const Options &options_)
		: options(options_)
	{
		EVENT_MANAGER_REGISTER_LATCH(DGCTriangleApplication, on_device_created, on_device_destroyed, DeviceCreatedEvent);
		get_wsi().set_present_mode(PresentMode::UnlockedMaybeTear);
	}

	Options options;
	const IndirectLayout *indirect_layout = nullptr;
	Vulkan::BufferHandle dgc_buffer;
	Vulkan::BufferHandle dgc_count_buffer;
	Vulkan::BufferHandle ssbo;
	Vulkan::BufferHandle ssbo_readback;
	uint32_t frame_count = 0;
	bool has_renderdoc = false;

	struct DGC
	{
		uint32_t push;
		VkDrawIndirectCommand draw;
	};

	void on_device_created(const DeviceCreatedEvent &e)
	{
		IndirectLayoutToken tokens[2] = {};

		{
			BufferCreateInfo buf_info = {};
			buf_info.domain = BufferDomain::Device;
			buf_info.size = options.max_count * sizeof(uint32_t);
			buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			buf_info.misc = BUFFER_MISC_ZERO_INITIALIZE_BIT;
			ssbo = e.get_device().create_buffer(buf_info, nullptr);
			buf_info.domain = BufferDomain::CachedHost;
			buf_info.misc = 0;
			ssbo_readback = e.get_device().create_buffer(buf_info, nullptr);
		}

		auto *layout = e.get_device().get_shader_manager().register_graphics(
				"assets://shaders/dgc.vert", "assets://shaders/dgc.frag")->
				register_variant({})->get_program()->get_pipeline_layout();

		tokens[0].type = IndirectLayoutToken::Type::PushConstant;
		tokens[0].offset = offsetof(DGC, push);
		tokens[0].data.push.range = 4;
		tokens[0].data.push.offset = 0;
		tokens[1].type = IndirectLayoutToken::Type::Draw;
		tokens[1].offset = offsetof(DGC, draw);

		if (e.get_device().get_device_features().device_generated_commands_features.deviceGeneratedCommands)
			indirect_layout = e.get_device().request_indirect_layout(layout, tokens, 2, sizeof(DGC));

		std::vector<DGC> dgc_data(options.max_count);
		for (unsigned i = 0; i < options.max_count; i++)
		{
			auto &dgc = dgc_data[i];
			dgc.push = i;
			dgc.draw = { options.primitives_per_draw * 3, 1 };
		}

		BufferCreateInfo buf_info = {};
		buf_info.domain = BufferDomain::LinkedDeviceHost;
		buf_info.size = dgc_data.size() * sizeof(dgc_data.front());
		buf_info.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		dgc_buffer = e.get_device().create_buffer(buf_info, dgc_data.data());

		const uint32_t count_data[] = { options.indirect_count };
		buf_info.size = sizeof(count_data);
		dgc_count_buffer = e.get_device().create_buffer(buf_info, &options.indirect_count);

		has_renderdoc = Device::init_renderdoc_capture();
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		dgc_buffer.reset();
		dgc_count_buffer.reset();
		ssbo.reset();
		ssbo_readback.reset();
		indirect_layout = nullptr;
	}

	void render_frame(double, double) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		if (options.use_dgc && !device.get_device_features().device_generated_commands_features.deviceGeneratedCommands)
		{
			LOGE("DGC is not supported.\n");
			request_shutdown();
			return;
		}

		if (has_renderdoc && frame_count == 0)
			device.begin_renderdoc_capture();

		auto cmd = device.request_command_buffer();
		auto preprocess_cmd = device.request_command_buffer();

		unsigned num_primitives = 0;

		auto start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
		{
			cmd->set_storage_buffer(0, 0, *ssbo);
			cmd->set_opaque_state();
			cmd->set_program("assets://shaders/dgc.vert", "assets://shaders/dgc.frag",
			                 {{ "MDI", int(options.use_mdi && !options.use_dgc) }});
			for (uint32_t i = 0; i < options.iterations; i++)
			{
				uint32_t indirect_draw_count = options.use_indirect_count ?
				                               std::min(options.indirect_count, options.max_count) :
				                               options.max_count;

				if (options.use_dgc)
				{
					cmd->execute_indirect_commands(VK_NULL_HANDLE, indirect_layout, options.max_count, *dgc_buffer, 0,
					                               options.use_indirect_count ? dgc_count_buffer.get() : nullptr, 0,
					                               *preprocess_cmd);
				}
				else if (options.use_indirect)
				{
					for (uint32_t j = 0; j < indirect_draw_count; j++)
					{
						cmd->push_constants(&j, 0, sizeof(j));
						cmd->draw_indirect(*dgc_buffer, offsetof(DGC, draw) + j * sizeof(DGC), 1, sizeof(DGC));
					}
				}
				else
				{
					for (uint32_t j = 0; j < indirect_draw_count; j++)
						cmd->draw(options.primitives_per_draw * 3);
				}
				num_primitives += indirect_draw_count * options.primitives_per_draw;
			}
		}
		cmd->end_render_pass();
		auto end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);

		if (options.use_dgc)
		{
			preprocess_cmd->barrier(VK_PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_EXT, VK_ACCESS_COMMAND_PREPROCESS_WRITE_BIT_EXT,
			                        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
			device.submit(preprocess_cmd);
		}
		else
		{
			device.submit_discard(preprocess_cmd);
		}

		device.submit(cmd);
		device.register_time_interval("GPU", std::move(start_ts), std::move(end_ts), "Shading");

		if (has_renderdoc && frame_count == 0)
			device.end_renderdoc_capture();

		LOGI("Ran frame!\n");
		if (++frame_count >= options.frames)
		{
			request_shutdown();
			device.timestamp_log([num_primitives](const std::string &, const TimestampIntervalReport &report) {
				printf("%.3f ms / frame\n", 1e3 * report.time_per_frame_context);
				printf("%.3f ns / vertex thread\n", 1e9 * report.time_per_frame_context / double(3 * num_primitives));
			});
		}
	}
};

namespace Granite
{
static void print_help()
{
	LOGI("Usage: dgc-test-graphics\n"
		 "\t[--max-count (maxSequenceCount / maxDraws)]\n"
	     "\t[--indirect-count (indirect count placed in indirect buffer)]\n"
	     "\t[--iterations (iterations)]\n"
	     "\t[--indirect (use indirect draw)]\n"
	     "\t[--primitives-per-draw (number of triangles to render)]\n"
	     "\t[--dgc (use NV_dgc)]\n"
	     "\t[--frames (number of frames to render before exiting)]\n"
	     "\t[--mdi (use multi-draw-indirect)]\n");
}

Application *application_create(int argc, char **argv)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	Options options;

	Util::CLICallbacks cbs;
	cbs.add("--max-count", [&](Util::CLIParser &parser) {
		options.max_count = parser.next_uint();
	});
	cbs.add("--indirect-count", [&](Util::CLIParser &parser) {
		options.indirect_count = parser.next_uint();
		options.use_indirect_count = true;
		options.use_indirect = true;
	});
	cbs.add("--iterations", [&](Util::CLIParser &parser) {
		options.iterations = parser.next_uint();
	});
	cbs.add("--indirect", [&](Util::CLIParser &) {
		options.use_indirect = true;
	});
	cbs.add("--primitives-per-draw", [&](Util::CLIParser &parser) {
		options.primitives_per_draw = parser.next_uint();
	});
	cbs.add("--dgc", [&](Util::CLIParser &) {
		options.use_dgc = true;
		options.use_indirect = true;
	});
	cbs.add("--mdi", [&](Util::CLIParser &) {
		options.use_mdi = true;
		options.use_indirect = true;
	});
	cbs.add("--frames", [&](Util::CLIParser &parser) {
		options.frames = parser.next_uint();
	});
	cbs.add("--help", [&](Util::CLIParser &parser) {
		parser.end();
	});

	Util::CLIParser parser(std::move(cbs), argc - 1, argv + 1);
	if (!parser.parse() || parser.is_ended_state())
	{
		print_help();
		return nullptr;
	}

	try
	{
		auto *app = new DGCTriangleApplication(options);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}