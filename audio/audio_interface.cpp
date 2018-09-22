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

#include "audio_interface.hpp"
#ifdef AUDIO_HAVE_PULSE
#include "audio_pulse.hpp"
#endif
#ifdef AUDIO_HAVE_OPENSL
#include "audio_opensl.hpp"
#endif

namespace Granite
{
namespace Audio
{
using BackendCreationCallback = Backend *(*)(BackendCallback &, float, unsigned);

static const BackendCreationCallback backends[] = {
#ifdef AUDIO_HAVE_PULSE
		create_pulse_backend,
#endif
#ifdef AUDIO_HAVE_OPENSL
		create_opensl_backend,
#endif
		nullptr,
};

Backend::Backend(BackendCallback &callback)
	: callback(callback)
{
}

Backend *create_default_audio_backend(BackendCallback &callback, float target_sample_rate, unsigned target_channels)
{
	for (auto &backend : backends)
	{
		if (backend)
		{
			auto iface = backend(callback, target_sample_rate, target_channels);
			if (iface)
				return iface;
		}
	}
	return nullptr;
}
}
}
