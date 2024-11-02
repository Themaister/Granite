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

#pragma once
#include "wsi.hpp"
#include "application_wsi_events.hpp"
#include "input.hpp"
#include "application_glue.hpp"

namespace Granite
{
class Application
{
public:
	virtual ~Application();
	virtual void render_frame(double frame_time, double elapsed_time) = 0;
	bool init_platform(std::unique_ptr<Vulkan::WSIPlatform> platform);
	bool init_wsi(Vulkan::ContextHandle context = {});
	void teardown_wsi();

	// Called after the frame is submitted for presentation.
	// Can do "garbage collection" or similar batched cleanup
	// that does not depend on submitting more graphics work.
	virtual void post_frame();

	// In early loading, we have not loaded SPIR-V yet.
	// Rendering a background color or extremely basic shaders could work here.
	// If compiling without SPIRV-Cross or compiler support in a shipping configuration,
	// any SPIR-V must be provided inline through slangmosh or similar.
	virtual void render_early_loading(double frame_time, double elapsed_time);
	// In loading, we have access to SPIR-V, but compiling pipelines is not done yet.
	// This stage is more suited for rendering splash screens or similar.
	virtual void render_loading(double frame_time, double elapsed_time);

	Vulkan::WSI &get_wsi()
	{
		return application_wsi;
	}

	Vulkan::WSIPlatform &get_platform()
	{
		return *platform;
	}

	virtual std::string get_name()
	{
		return "granite";
	}

	virtual unsigned get_version()
	{
		return 0;
	}

	virtual unsigned get_default_width()
	{
		return 1280;
	}

	virtual unsigned get_default_height()
	{
		return 720;
	}

	bool poll();
	void run_frame();
	void show_message_box(const std::string &str, Vulkan::WSIPlatform::MessageType type);

protected:
	void request_shutdown()
	{
		requested_shutdown = true;
	}

	void poll_input_tracker_async(InputTrackerHandler *override_handler);

private:
	std::unique_ptr<Vulkan::WSIPlatform> platform;
	Vulkan::WSI application_wsi;
	bool requested_shutdown = false;

	// Ready state for deferred device initialization.
	bool ready_modules = false;
	bool ready_pipelines = false;
	void check_initialization_progress();
};
}