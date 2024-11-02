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
using BackendCreationCallback = Backend *(*)(BackendCallback *, float, unsigned);
using RecordBackendCreationCallback = RecordStream *(*)(const char *, float, unsigned);

static const BackendCreationCallback backends[] = {
#ifdef AUDIO_HAVE_PULSE
	create_pulse_backend,
#endif
#ifdef AUDIO_HAVE_OBOE
	create_oboe_backend,
#endif
#ifdef AUDIO_HAVE_WASAPI
	create_wasapi_backend,
#endif
	nullptr,
};

static const RecordBackendCreationCallback record_backends[] = {
#ifdef AUDIO_HAVE_PULSE
	create_pulse_record_backend,
#endif
	nullptr,
};

Backend::Backend(BackendCallback *callback_)
	: callback(callback_)
{
}

void Backend::heartbeat()
{
}

bool Backend::get_buffer_status(size_t &, size_t &, uint32_t &)
{
	return false;
}

size_t Backend::write_frames_interleaved(const float *, size_t, bool)
{
	return 0;
}

Backend *create_default_audio_backend(BackendCallback *callback, float target_sample_rate, unsigned target_channels)
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

RecordStream *create_default_audio_record_backend(const char *ident, float target_sample_rate, unsigned target_channels)
{
	for (auto &backend : record_backends)
	{
		if (backend)
		{
			auto iface = backend(ident, target_sample_rate, target_channels);
			if (iface)
				return iface;
		}
	}
	return nullptr;
}

struct DumpBackend::Impl
{
	std::vector<float> mix_buffers[Backend::MaxAudioChannels];
	float *mix_buffers_ptr[Backend::MaxAudioChannels] = {};

	float target_sample_rate = 0;
	unsigned target_channels = 0;
	unsigned frames_per_tick = 0;
	unsigned frames = 0;
};

DumpBackend::DumpBackend(BackendCallback *callback_, float target_sample_rate,
                         unsigned target_channels, unsigned frames_per_tick)
	: Backend(callback_)
{
	impl.reset(new Impl);
	impl->target_sample_rate = target_sample_rate;
	impl->target_channels = target_channels;
	impl->frames_per_tick = frames_per_tick;

	if (callback)
	{
		callback->set_backend_parameters(target_sample_rate, target_channels, frames_per_tick);
		callback->set_latency_usec(0);
	}

	for (unsigned c = 0; c < target_channels; c++)
	{
		impl->mix_buffers[c].resize(frames_per_tick);
		impl->mix_buffers_ptr[c] = impl->mix_buffers[c].data();
	}
}

DumpBackend::~DumpBackend()
{
}

unsigned DumpBackend::get_frames_per_tick() const
{
	return impl->frames_per_tick;
}

void DumpBackend::drain_interleaved_s16(int16_t *data, size_t frames)
{
	size_t mixed_frames = 0;
	while (mixed_frames < frames)
	{
		size_t to_mix = std::min<size_t>(frames - mixed_frames, impl->frames_per_tick);
		callback->mix_samples(impl->mix_buffers_ptr, to_mix);

		if (impl->target_channels == 2)
		{
			DSP::interleave_stereo_f32_i16(data, impl->mix_buffers_ptr[0], impl->mix_buffers_ptr[1], to_mix);
			data += to_mix * impl->target_channels;
		}
		else
		{
			unsigned channels = impl->target_channels;
			for (size_t f = 0; f < to_mix; f++)
				for (unsigned c = 0; c < channels; c++)
					*data++ = DSP::f32_to_i16(impl->mix_buffers_ptr[c][f]);
		}

		mixed_frames += to_mix;
	}
}

bool DumpBackend::start()
{
	if (!callback)
	{
		LOGE("DumpBackend must be used with audio callback.\n");
		return false;
	}

	if (callback)
		callback->on_backend_start();
	return true;
}

bool DumpBackend::stop()
{
	if (callback)
		callback->on_backend_stop();
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

size_t DumpBackend::write_frames_interleaved(const float *, size_t, bool)
{
	return 0;
}

bool DumpBackend::get_buffer_status(size_t &, size_t &, uint32_t &)
{
	return false;
}
}
}
