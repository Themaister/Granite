/* Copyright (c) 2017 Hans-Kristian Arntzen
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
#include "application_events.hpp"
#include "vulkan.hpp"
#include "os.hpp"
#include "cli_parser.hpp"
#include "dynamic_library.hpp"

extern "C" {
typedef const char * (VKAPI_PTR *PFN_GraniteCustomVulkanSurfaceExtension)();
typedef VkResult (VKAPI_PTR *PFN_GraniteCreateCustomVulkanSurface)(VkInstance instance,
                                                                   PFN_vkGetInstanceProcAddr gpa,
                                                                   uint32_t width, uint32_t height,
                                                                   VkSurfaceKHR *pSurface);
}

using namespace std;
using namespace Vulkan;
using namespace Granite;
using namespace Util;

namespace Granite
{
struct WSIPlatformCustomSurface : Vulkan::WSIPlatform
{
public:
	WSIPlatformCustomSurface(unsigned width, unsigned height, const std::string &path)
		: width(width), height(height), library(path.c_str())
	{
		if (!Context::init_loader(nullptr))
			throw runtime_error("Failed to initialize Vulkan loader.");

		EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
		EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);
	}

	~WSIPlatformCustomSurface()
	{
		EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
	}

	bool alive(Vulkan::WSI &) override
	{
		return true;
	}

	void poll_input() override
	{
		get_input_tracker().dispatch_current_state(get_frame_timer().get_frame_time());
	}

	vector<const char *> get_instance_extensions() override
	{
		auto symbol = library.get_symbol<PFN_GraniteCustomVulkanSurfaceExtension>("GraniteCustomVulkanSurfaceExtension");
		if (symbol)
			return { "VK_KHR_surface", symbol() };
		else
		{
			LOGE("No custom surface extension entry point found, just using VK_KHR_surface.\n");
			return { "VK_KHR_surface" };
		}
	}

	VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice) override
	{
		VkSurfaceKHR surface = VK_NULL_HANDLE;

		auto symbol = library.get_symbol<PFN_GraniteCreateCustomVulkanSurface>("GraniteCreateCustomVulkanSurface");
		if (!symbol)
		{
			LOGE("Failed to get symbol GraniteCreateCustomVulkanSurface from library.\n");
			return VK_NULL_HANDLE;
		}

		if (symbol(instance, vulkan_symbol_wrapper_instance_proc_addr(), width, height, &surface) != VK_SUCCESS)
			return VK_NULL_HANDLE;

		return surface;
	}

	uint32_t get_surface_width() override
	{
		return width;
	}

	uint32_t get_surface_height() override
	{
		return height;
	}

	void notify_resize(unsigned width, unsigned height)
	{
		resize = true;
		this->width = width;
		this->height = height;
	}

private:
	unsigned width = 0;
	unsigned height = 0;
	DynamicLibrary library;
};

void application_dummy()
{
}

}

static void print_help()
{
	LOGI("[--fs-assets <path>] [--fs-cache <path>] [--fs-builtin <path>]\n"
	     "[--width <width>] [--height <height>] [--library <path>] [--frames <frames>].\n");
}

int main(int argc, char *argv[])
{
	struct Args
	{
		string assets;
		string cache;
		string builtin;
		string library;
		unsigned width = 1280;
		unsigned height = 720;
		unsigned frames = 0;
	} args;

	std::vector<char *> filtered_argv;
	filtered_argv.push_back(argv[0]);

	CLICallbacks cbs;
	cbs.add("--width", [&](CLIParser &parser) { args.width = parser.next_uint(); });
	cbs.add("--height", [&](CLIParser &parser) { args.height = parser.next_uint(); });
	cbs.add("--fs-assets", [&](CLIParser &parser) { args.assets = parser.next_string(); });
	cbs.add("--fs-builtin", [&](CLIParser &parser) { args.builtin = parser.next_string(); });
	cbs.add("--fs-cache", [&](CLIParser &parser) { args.cache = parser.next_string(); });
	cbs.add("--library", [&](CLIParser &parser) { args.library = parser.next_string(); });
	cbs.add("--frames", [&](CLIParser &parser) { args.frames = parser.next_uint(); });
	cbs.add("--help", [](CLIParser &parser) { print_help(); parser.end(); });
	cbs.default_handler = [&](const char *arg) { filtered_argv.push_back(const_cast<char *>(arg)); };
	cbs.error_handler = [&]() { print_help(); };
	CLIParser parser(move(cbs), argc - 1, argv + 1);
	parser.ignore_unknown_arguments();
	if (!parser.parse())
		return 1;
	else if (parser.is_ended_state())
		return 0;

	filtered_argv.push_back(nullptr);

	if (!args.assets.empty())
		Filesystem::get().register_protocol("assets", make_unique<OSFilesystem>(args.assets));
	if (!args.builtin.empty())
		Filesystem::get().register_protocol("builtin", make_unique<OSFilesystem>(args.builtin));
	if (!args.cache.empty())
		Filesystem::get().register_protocol("cache", make_unique<OSFilesystem>(args.cache));

	if (args.library.empty())
	{
		LOGE("Need to specify dynamic library for creating Vulkan surface.\n");
		return 1;
	}

	auto app = unique_ptr<Application>(application_create(int(filtered_argv.size() - 1), filtered_argv.data()));

	if (app)
	{
		if (!app->init_wsi(make_unique<WSIPlatformCustomSurface>(args.width, args.height, args.library)))
			return 1;

		unsigned run_frames = 0;
		while (app->poll())
		{
			app->run_frame();
			LOGI("Submitted frame #%u!\n", run_frames);
			run_frames++;

			if (args.frames && run_frames == args.frames)
			{
				LOGI("Completed all submissions ...\n");
				break;
			}
		}
		return 0;
	}
	else
		return 1;
}
