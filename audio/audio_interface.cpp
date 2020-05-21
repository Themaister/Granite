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

#include "filesystem.hpp"
#include "audio_interface.hpp"
#include "logging.hpp"
#include "dsp/dsp.hpp"
#ifdef AUDIO_HAVE_PULSE
#include "audio_pulse.hpp"
#endif
#ifdef AUDIO_HAVE_AAUDIO
#include "audio_aaudio.hpp"
#endif
#ifdef AUDIO_HAVE_OBOE
#include "audio_oboe.hpp"
#endif
#ifdef AUDIO_HAVE_OPENSL
#include "audio_opensl.hpp"
#endif
#ifdef AUDIO_HAVE_WASAPI
#include "audio_wasapi.hpp"
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
#ifdef AUDIO_HAVE_OBOE
		create_oboe_backend,
#endif
		// Buggy on Android 8.0, should work fine on 8.1?
#ifdef AUDIO_HAVE_AAUDIO
		create_aaudio_backend,
#endif
#ifdef AUDIO_HAVE_OPENSL
		create_opensl_backend,
#endif
#ifdef AUDIO_HAVE_WASAPI
		create_wasapi_backend,
#endif
		nullptr,
};

Backend::Backend(BackendCallback &callback_)
	: callback(callback_)
{
}

void Backend::heartbeat()
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

struct DumpBackend::Impl
{
	std::string path;

	std::unique_ptr<File> file;
	float *mapped = nullptr;

	std::vector<float> mix_buffers[Backend::MaxAudioChannels];
	float *mix_buffers_ptr[Backend::MaxAudioChannels] = {};

	float target_sample_rate = 0;
	unsigned target_channels = 0;
	unsigned frames_per_tick = 0;
	unsigned frames = 0;
	unsigned frame_offset = 0;
};

DumpBackend::DumpBackend(BackendCallback &callback_, const std::string &path, float target_sample_rate,
                         unsigned target_channels, unsigned frames_per_tick, unsigned frames)
	: Backend(callback_)
{
	impl.reset(new Impl);
	impl->path = path;
	impl->target_sample_rate = target_sample_rate;
	impl->target_channels = target_channels;
	impl->frames = frames;
	impl->frames_per_tick = frames_per_tick;

	callback_.set_backend_parameters(target_sample_rate, target_channels, frames_per_tick);
	callback_.set_latency_usec(0);

	for (unsigned c = 0; c < target_channels; c++)
	{
		impl->mix_buffers[c].resize(frames_per_tick);
		impl->mix_buffers_ptr[c] = impl->mix_buffers[c].data();
	}
}

DumpBackend::~DumpBackend()
{
}

void DumpBackend::frame()
{
	if ((impl->frame_offset < impl->frames) && impl->mapped)
	{
		callback.mix_samples(impl->mix_buffers_ptr, impl->frames_per_tick);

		if (impl->target_channels == 2)
		{
			DSP::interleave_stereo_f32(impl->mapped, impl->mix_buffers_ptr[0], impl->mix_buffers_ptr[1], impl->frames_per_tick);
			impl->mapped += impl->frames_per_tick * impl->target_channels;
		}
		else
		{
			unsigned channels = impl->target_channels;
			size_t frames_per_tick = impl->frames_per_tick;
			for (size_t f = 0; f < frames_per_tick; f++)
				for (unsigned c = 0; c < channels; c++)
					*impl->mapped++ = impl->mix_buffers_ptr[c][f];
		}
	}

	impl->frame_offset++;
}

bool DumpBackend::start()
{
	size_t target_size = impl->frames * impl->frames_per_tick * impl->target_channels * sizeof(float);

	impl->file = Granite::Global::filesystem()->open(impl->path, Granite::FileMode::WriteOnly);
	if (!impl->file)
	{
		LOGE("Failed to open dump file for writing.\n");
		return false;
	}

	impl->mapped = static_cast<float *>(impl->file->map_write(target_size));
	if (!impl->mapped)
	{
		LOGE("Failed to map dump file for writing.\n");
		return false;
	}

	callback.on_backend_start();
	return true;
}

bool DumpBackend::stop()
{
	if (impl->file && impl->mapped)
	{
		impl->file->unmap();
		impl->mapped = nullptr;
		impl->frame_offset = 0;
	}

	impl->file.reset();
	callback.on_backend_stop();
	return true;
}

float DumpBackend::get_sample_rate()
{
	return impl->target_sample_rate;
}

unsigned DumpBackend::get_num_channels()
{
	return impl->target_channels;
}

const char *DumpBackend::get_backend_name()
{
	return "dump";
}

}
}
