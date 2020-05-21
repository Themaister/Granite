/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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
#include "camera.hpp"

namespace Granite
{
class Application
{
public:
	Application();
	virtual ~Application() = default;
	virtual void render_frame(double frame_time, double elapsed_time) = 0;
	bool init_wsi(std::unique_ptr<Vulkan::WSIPlatform> platform);

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

protected:
	void request_shutdown()
	{
		requested_shutdown = true;
	}

private:
	std::unique_ptr<Vulkan::WSIPlatform> platform;
	Vulkan::WSI application_wsi;
	bool requested_shutdown = false;
};

int application_main(Application *(*create_application)(int, char **), int argc, char **argv);
int application_main_headless(Application *(*create_application)(int, char **), int argc, char **argv);

extern Application *application_create(int argc, char *argv[]);

// Call this to ensure application-main is linked in correctly without having to mess around
// with -Wl,--whole-archive.
void application_dummy();
}
