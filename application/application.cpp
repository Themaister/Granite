/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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
#include "sprite.hpp"
#include "horizontal_packing.hpp"
#include "image_widget.hpp"
#include "label.hpp"
#include "quirks.hpp"
#include "post/hdr.hpp"
#include "rapidjson_wrapper.hpp"
#include "light_export.hpp"
#include "muglm/matrix_helper.hpp"
#include "muglm/muglm_impl.hpp"
#include "utils/image_utils.hpp"
#ifdef HAVE_GRANITE_AUDIO
#include "audio_mixer.hpp"
#endif

using namespace std;
using namespace Vulkan;

namespace Granite
{
Application::Application()
{
}

bool Application::init_wsi(std::unique_ptr<WSIPlatform> new_platform)
{
	platform = move(new_platform);
	wsi.set_platform(platform.get());
	if (!platform->has_external_swapchain() && !wsi.init())
		return false;

	return true;
}

bool Application::poll()
{
	auto &wsi = get_wsi();
	if (!get_platform().alive(wsi))
		return false;

	if (requested_shutdown)
		return false;

	auto *fs = Global::filesystem();
	auto *em = Global::event_manager();
	if (fs)
		fs->poll_notifications();
	if (em)
		em->dispatch();
#ifdef HAVE_GRANITE_AUDIO
	auto *am = Global::audio_mixer();
	if (am)
		am->dispose_dead_streams();
#endif
	return true;
}

void Application::run_frame()
{
	wsi.begin_frame();
	render_frame(wsi.get_platform().get_frame_timer().get_frame_time(),
	             wsi.get_platform().get_frame_timer().get_elapsed());
	wsi.end_frame();
}

}
