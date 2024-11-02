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
#include "application.hpp"
#include "asset_manager.hpp"
#include "thread_group.hpp"
#ifdef HAVE_GRANITE_RENDERER
#include "material_manager.hpp"
#include "common_renderer_data.hpp"
#endif
#ifdef HAVE_GRANITE_AUDIO
#include "audio_mixer.hpp"
#endif

using namespace Vulkan;

namespace Granite
{
Application::~Application()
{
	auto *group = GRANITE_THREAD_GROUP();
	if (group)
		group->wait_idle();

	teardown_wsi();
}

bool Application::init_platform(std::unique_ptr<WSIPlatform> new_platform)
{
#ifdef HAVE_GRANITE_RENDERER
	if (auto *common = GRANITE_COMMON_RENDERER_DATA())
		common->initialize_static_assets(GRANITE_ASSET_MANAGER(), GRANITE_FILESYSTEM());
#endif
	platform = std::move(new_platform);
	application_wsi.set_platform(platform.get());

	if (auto *event = GRANITE_EVENT_MANAGER())
		event->enqueue_latched<ApplicationWSIPlatformEvent>(*platform);

	return true;
}

void Application::teardown_wsi()
{
	if (auto *event = GRANITE_EVENT_MANAGER())
	{
		event->dequeue_all_latched(DevicePipelineReadyEvent::get_type_id());
		event->dequeue_all_latched(DeviceShaderModuleReadyEvent::get_type_id());
		event->dequeue_all_latched(ApplicationWSIPlatformEvent::get_type_id());
	}
	application_wsi.teardown();
	ready_modules = false;
	ready_pipelines = false;
}

bool Application::init_wsi(Vulkan::ContextHandle context)
{
	if (context)
	{
		if (!application_wsi.init_from_existing_context(std::move(context)))
			return false;
	}
	else
	{
		Context::SystemHandles system_handles;
		system_handles.filesystem = GRANITE_FILESYSTEM();
		system_handles.thread_group = GRANITE_THREAD_GROUP();
		system_handles.asset_manager = GRANITE_ASSET_MANAGER();
		system_handles.timeline_trace_file = system_handles.thread_group->get_timeline_trace_file();

		if (!application_wsi.init_context_from_platform(
				system_handles.thread_group->get_num_threads() + 1,
				system_handles))
		{
			return false;
		}
	}

	if (!application_wsi.init_device())
		return false;

	application_wsi.get_device().begin_shader_caches();

	{
		GRANITE_SCOPED_TIMELINE_EVENT("wsi-init-swapchain");
		if (!platform->has_external_swapchain() && !application_wsi.init_surface_swapchain())
			return false;
	}

	return true;
}

void Application::poll_input_tracker_async(Granite::InputTrackerHandler *override_handler)
{
	get_platform().poll_input_async(override_handler);
}

bool Application::poll()
{
	auto &wsi = get_wsi();
	if (!get_platform().alive(wsi))
		return false;

	if (requested_shutdown)
		return false;

	auto *fs = GRANITE_FILESYSTEM();
	auto *em = GRANITE_EVENT_MANAGER();
	if (fs)
		fs->poll_notifications();
	if (em)
		em->dispatch();

#ifdef HAVE_GRANITE_AUDIO
	auto *backend = GRANITE_AUDIO_BACKEND();
	if (backend)
		backend->heartbeat();
	auto *am = GRANITE_AUDIO_MIXER();
	if (am)
	{
		// Pump through events from audio thread.
		auto &queue = am->get_message_queue();
		Util::MessageQueuePayload payload;
		while ((payload = queue.read_message()))
		{
			auto &event = payload.as<Event>();
			if (em)
				em->dispatch_inline(event);
			queue.recycle_payload(std::move(payload));
		}

		// Recycle dead streams.
		am->dispose_dead_streams();
	}
#endif

	return true;
}

void Application::check_initialization_progress()
{
	auto &device = get_wsi().get_device();

	if (!ready_modules)
	{
		if (device.query_initialization_progress(Device::InitializationStage::CacheMaintenance) >= 100 &&
		    device.query_initialization_progress(Device::InitializationStage::ShaderModules) >= 100)
		{
			if (auto *manager = GRANITE_ASSET_MANAGER())
			{
				// Now is a good time to kick shader manager since it might require compute shaders for decode.
				manager->iterate(GRANITE_THREAD_GROUP());
			}

			if (auto *event = GRANITE_EVENT_MANAGER())
			{
				GRANITE_SCOPED_TIMELINE_EVENT("dispatch-ready-modules");
#ifdef HAVE_GRANITE_RENDERER
				auto *manager = &device.get_shader_manager();
#else
				constexpr Vulkan::ShaderManager *manager = nullptr;
#endif
				event->enqueue_latched<DeviceShaderModuleReadyEvent>(&device, manager);
			}
			ready_modules = true;
		}
	}

	if (!ready_pipelines)
	{
		if (device.query_initialization_progress(Device::InitializationStage::Pipelines) >= 100)
		{
			if (auto *event = GRANITE_EVENT_MANAGER())
			{
				GRANITE_SCOPED_TIMELINE_EVENT("dispatch-ready-pipelines");
#ifdef HAVE_GRANITE_RENDERER
				auto *manager = &device.get_shader_manager();
#else
				constexpr Vulkan::ShaderManager *manager = nullptr;
#endif
				event->enqueue_latched<DevicePipelineReadyEvent>(&device, manager);
			}
			ready_pipelines = true;
		}
	}
}

void Application::show_message_box(const std::string &str, Vulkan::WSIPlatform::MessageType type)
{
	if (platform)
		platform->show_message_box(str, type);

	switch (type)
	{
	case Vulkan::WSIPlatform::MessageType::Error:
		LOGE("%s\n", str.c_str());
		break;

	case Vulkan::WSIPlatform::MessageType::Warning:
		LOGW("%s\n", str.c_str());
		break;

	case Vulkan::WSIPlatform::MessageType::Info:
		LOGI("%s\n", str.c_str());
		break;
	}
}

void Application::run_frame()
{
	check_initialization_progress();

	{
		GRANITE_SCOPED_TIMELINE_EVENT("wsi-begin-frame");
		if (!application_wsi.begin_frame())
		{
			LOGE("Failed to begin frame. Fatal error. Shutting down.\n");
			request_shutdown();
			return;
		}
	}

	double smooth_frame_time = application_wsi.get_smooth_frame_time();
	double smooth_elapsed = application_wsi.get_smooth_elapsed_time();

	if (!ready_modules)
	{
		GRANITE_SCOPED_TIMELINE_EVENT("render-early-loading");
		render_early_loading(smooth_frame_time, smooth_elapsed);
	}
	else if (!ready_pipelines)
	{
		GRANITE_SCOPED_TIMELINE_EVENT("render-loading");
		render_loading(smooth_frame_time, smooth_elapsed);
	}
	else
	{
		GRANITE_SCOPED_TIMELINE_EVENT("render-frame");
		render_frame(smooth_frame_time, smooth_elapsed);
	}

	{
		GRANITE_SCOPED_TIMELINE_EVENT("wsi-end-frame");
		application_wsi.end_frame();
	}

	{
		GRANITE_SCOPED_TIMELINE_EVENT("post-frame");
		post_frame();
	}
}

void Application::render_early_loading(double, double)
{
	auto &device = application_wsi.get_device();
	auto cmd = device.request_command_buffer();
	auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
	rp.clear_color[0].float32[0] = 0.01f;
	rp.clear_color[0].float32[2] = 0.02f;
	rp.clear_color[0].float32[3] = 0.03f;
	cmd->begin_render_pass(rp);
	auto vp = cmd->get_viewport();

	VkClearRect rect = {};
	rect.layerCount = 1;
	rect.rect.extent = {
		uint32_t(vp.width * 0.01f * float(device.query_initialization_progress(Device::InitializationStage::ShaderModules))),
		uint32_t(vp.height),
	};

	VkClearValue value = {};
	value.color.float32[0] = 0.08f;
	value.color.float32[1] = 0.01f;
	value.color.float32[2] = 0.01f;

	if (rect.rect.extent.width > 0)
		cmd->clear_quad(0, rect, value);

	cmd->end_render_pass();
	device.submit(cmd);
}

void Application::render_loading(double, double)
{
	auto &device = application_wsi.get_device();
	auto cmd = device.request_command_buffer();
	auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
	rp.clear_color[0].float32[0] = 0.01f;
	rp.clear_color[0].float32[2] = 0.02f;
	rp.clear_color[0].float32[3] = 0.03f;
	cmd->begin_render_pass(rp);
	auto vp = cmd->get_viewport();

	VkClearRect rect = {};
	rect.layerCount = 1;
	rect.rect.extent = {
		uint32_t(vp.width * 0.01f * float(device.query_initialization_progress(Device::InitializationStage::Pipelines))),
		uint32_t(vp.height),
	};

	VkClearValue value = {};
	value.color.float32[0] = 0.01f;
	value.color.float32[1] = 0.08f;
	value.color.float32[2] = 0.01f;
	if (rect.rect.extent.width > 0)
		cmd->clear_quad(0, rect, value);

	cmd->end_render_pass();
	device.submit(cmd);
}

void Application::post_frame()
{
	// Texture manager might require shaders to be ready before we can submit work.
	if (ready_modules)
	{
		if (auto *manager = GRANITE_ASSET_MANAGER())
			manager->iterate(GRANITE_THREAD_GROUP());
	}

	if (auto *manager = Global::material_manager())
		manager->iterate(Global::asset_manager());
}
}
