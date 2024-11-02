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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <algorithm>
#include <atomic>

#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <avrt.h>
#include "audio_interface.hpp"
#include "dsp/dsp.hpp"
#include "logging.hpp"

static const size_t MAX_NUM_FRAMES = 256;

// Doesn't link properly on MinGW.
const static GUID _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
	0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

namespace Granite
{
namespace Audio
{
struct WASAPIBackend final : Backend
{
	WASAPIBackend(BackendCallback *callback_)
		: Backend(callback_)
	{
		dead = false;
		audio_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
	}

	~WASAPIBackend();
	bool init(float sample_rate, unsigned channels);

	bool start() override;
	bool stop() override;

	bool get_buffer_status(size_t &write_avail, size_t &write_avail_frames, uint32_t &latency_usec) override;
	size_t write_frames_interleaved(const float *data, size_t frames, bool blocking) override;

	const char *get_backend_name() override
	{
		return "WASAPI";
	}

	float get_sample_rate() override
	{
		return float(format->nSamplesPerSec);
	}

	unsigned get_num_channels() override
	{
		return format->nChannels;
	}

	void thread_runner() noexcept;

	std::thread thr;
	std::atomic_bool dead;

	IMMDeviceEnumerator *pEnumerator = nullptr;
	IMMDevice *pDevice = nullptr;
	IAudioClient3 *pAudioClient = nullptr;
	IAudioRenderClient *pRenderClient = nullptr;
	uint32_t buffer_latency_us = 0;
	UINT32 buffer_frames = 0;
	WAVEFORMATEX *format = nullptr;
	bool is_active = false;
	HANDLE audio_event = nullptr;

	bool kick_start() noexcept;

	bool get_write_avail(UINT32 &avail) noexcept;
	bool get_write_avail_blocking(UINT32 &avail) noexcept;
	uint32_t get_latency_usec() noexcept;
};

WASAPIBackend::~WASAPIBackend()
{
	stop();
	if (format)
		CoTaskMemFree(format);
	if (pRenderClient)
		pRenderClient->Release();
	if (pAudioClient)
		pAudioClient->Release();
	if (pDevice)
		pDevice->Release();
	if (pEnumerator)
		pEnumerator->Release();
	if (audio_event)
		CloseHandle(audio_event);
}

static double reference_time_to_seconds(REFERENCE_TIME t)
{
	return double(t) / 10000000.0;
}

bool WASAPIBackend::init(float sample_rate, unsigned channels)
{
	if (FAILED(CoInitialize(nullptr)))
	{
		LOGE("WASAPI: Failed to initialize COM!\n");
		return false;
	}

	if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
	                            CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
	                            reinterpret_cast<void **>(&pEnumerator))))
	{
		LOGE("WASAPI: Failed to create MM instance.\n");
		return false;
	}

	if (FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice)))
	{
		LOGE("WASAPI: Failed to get default audio endpoint.\n");
		return false;
	}

	if (FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
	                             reinterpret_cast<void **>(&pAudioClient))))
	{
		LOGE("WASAPI: Failed to activate AudioClient.\n");
		return false;
	}

	if (FAILED(pAudioClient->GetMixFormat(&format)))
	{
		LOGE("WASAPI: Failed to get mix format.\n");
		return false;
	}

	if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
	{
		LOGE("WASAPI: Mix format is weird.\n");
		return false;
	}

	auto *ex = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(format);
	if (ex->SubFormat != _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT ||
	    format->wBitsPerSample != 32)
	{
		LOGE("WASAPI: Mix format is not FP32.\n");
		return false;
	}

	format->nChannels = WORD(channels);
	DWORD base_rate = format->nSamplesPerSec;
	if (sample_rate > 0.0f)
		format->nSamplesPerSec = DWORD(sample_rate);

	UINT default_period = 0, fundamental_period = 0, min_period = 0, max_period = 0;
	HRESULT hr;
	if (FAILED(hr = pAudioClient->GetSharedModeEnginePeriod(format, &default_period, &fundamental_period, &min_period, &max_period)))
	{
		if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT && format->nSamplesPerSec != base_rate)
		{
			format->nSamplesPerSec = base_rate;
			hr = pAudioClient->GetSharedModeEnginePeriod(format, &default_period, &fundamental_period, &min_period, &max_period);
		}

		if (FAILED(hr))
		{
			LOGE("WASAPI: Failed to query shared mode engine period.\n");
			return false;
		}
	}

	// Sanity check, but you'd think default period should "just werk".
	if (fundamental_period != 0 && default_period % fundamental_period != 0)
	{
		LOGE("WASAPI: Nonsensical default period.\n");
		return false;
	}

	if (FAILED(pAudioClient->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, default_period, format, nullptr)))
	{
		LOGE("WASAPI: Failed to initialize audio client.\n");
		return false;
	}

	if (FAILED(pAudioClient->SetEventHandle(audio_event)))
	{
		LOGE("WASAPI: Failed to set event handle.\n");
		return false;
	}

	if (FAILED(pAudioClient->GetBufferSize(&buffer_frames)))
	{
		LOGE("WASAPI: Failed to get buffer size.\n");
		return false;
	}

	if (FAILED(pAudioClient->GetService(__uuidof(IAudioRenderClient),
	                                    reinterpret_cast<void **>(&pRenderClient))))
	{
		LOGE("WASAPI: Failed to get render client service.\n");
		return false;
	}

	if (callback)
	{
		callback->set_latency_usec((1000000 * buffer_frames) / format->nSamplesPerSec);
		callback->set_backend_parameters(get_sample_rate(), get_num_channels(), MAX_NUM_FRAMES);
	}
	return true;
}

uint32_t WASAPIBackend::get_latency_usec() noexcept
{
	REFERENCE_TIME latency;
	if (FAILED(pAudioClient->GetStreamLatency(&latency)))
	{
		LOGE("WASAPI: Failed to get stream latency.\n");
		return 0;
	}

	double server_latency = reference_time_to_seconds(latency);
	server_latency += double(buffer_frames) / get_sample_rate();
	return uint32_t(server_latency * 1e6);
}

bool WASAPIBackend::start()
{
	if (is_active)
		return false;
	is_active = true;
	dead = false;

	if (callback)
	{
		callback->on_backend_start();
		thr = std::thread(&WASAPIBackend::thread_runner, this);
	}
	else
	{
		if (!kick_start())
			return false;
	}

	return true;
}

bool WASAPIBackend::stop()
{
	if (!is_active)
		return false;
	is_active = false;

	if (thr.joinable())
	{
		dead.store(true, std::memory_order_relaxed);
		SetEvent(audio_event);
		thr.join();
	}

	if (callback)
		callback->on_backend_stop();
	return true;
}

bool WASAPIBackend::kick_start() noexcept
{
	BYTE *interleaved = nullptr;
	if (FAILED(pRenderClient->GetBuffer(buffer_frames, &interleaved)))
	{
		LOGE("WASAPI: Failed to get buffer (start).\n");
		return false;
	}

	if (FAILED(pRenderClient->ReleaseBuffer(buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT)))
	{
		LOGE("WASAPI: Failed to release buffer (start).\n");
		return false;
	}

	if (FAILED(pAudioClient->Start()))
	{
		LOGE("WASAPI: Failed to start audio client.\n");
		return false;
	}

	buffer_latency_us = get_latency_usec();

	return true;
}

size_t WASAPIBackend::write_frames_interleaved(const float *frames, size_t num_frames, bool blocking)
{
	if (callback)
		return 0;

	BYTE *interleaved = nullptr;
	size_t written_frames = 0;

	while (num_frames)
	{
		UINT32 write_avail = 0;

		if (blocking)
		{
			if (!get_write_avail_blocking(write_avail))
				break;
		}
		else
		{
			if (!get_write_avail(write_avail))
				break;
		}

		size_t to_write = std::min<size_t>(num_frames, write_avail);

		if (to_write)
		{
			if (FAILED(pRenderClient->GetBuffer(to_write, &interleaved)))
			{
				LOGE("WASAPI: Failed to get buffer.\n");
				break;
			}

			memcpy(interleaved, frames, to_write * sizeof(float) * format->nChannels);

			if (FAILED(pRenderClient->ReleaseBuffer(to_write, 0)))
			{
				LOGE("WASAPI: Failed to release buffer.\n");
				break;
			}

			frames += to_write * format->nChannels;
			written_frames += to_write;
			num_frames -= to_write;
		}
		else
			break;
	}

	return written_frames;
}

bool WASAPIBackend::get_buffer_status(size_t &write_avail, size_t &max_write_avail, uint32_t &latency_us)
{
	if (callback)
		return false;

	UINT32 write_avail_u32;
	if (!get_write_avail(write_avail_u32))
		return false;

	write_avail = write_avail_u32;
	max_write_avail = buffer_frames;
	latency_us = buffer_latency_us;
	return true;
}

bool WASAPIBackend::get_write_avail(UINT32 &avail) noexcept
{
	UINT32 padding;
	if (FAILED(pAudioClient->GetCurrentPadding(&padding)))
	{
		LOGE("WASAPI: Failed to get buffer padding.\n");
		return false;
	}

	avail = buffer_frames - padding;
	return true;
}

bool WASAPIBackend::get_write_avail_blocking(UINT32 &avail) noexcept
{
	if (!get_write_avail(avail))
		return false;

	while (!dead.load(std::memory_order_relaxed) && avail == 0)
	{
		DWORD ret = WaitForSingleObject(audio_event, 2000);

		if (ret == WAIT_OBJECT_0)
		{
			if (!get_write_avail(avail))
				return false;
		}
		else if (ret == WAIT_TIMEOUT)
		{
			LOGE("Timed out waiting for audio event.\n");
			return false;
		}
		else
		{
			LOGE("Other error for WFSO: #%x.\n", unsigned(ret));
			return false;
		}
	}

	return avail != 0;
}

void WASAPIBackend::thread_runner() noexcept
{
	DWORD task_index = 0;
	HANDLE audio_task = AvSetMmThreadCharacteristicsA("Games", &task_index);
	if (!audio_task)
		LOGW("Failed to set thread characteristic.\n");

	if (!kick_start())
	{
		if (audio_task)
			AvRevertMmThreadCharacteristics(audio_task);
		return;
	}

	float mix_channels[Backend::MaxAudioChannels][MAX_NUM_FRAMES];
	float *mix_channel_ptr[Backend::MaxAudioChannels];
	for (unsigned i = 0; i < format->nChannels; i++)
		mix_channel_ptr[i] = mix_channels[i];

	while (!dead.load(std::memory_order_relaxed))
	{
		UINT32 write_avail = 0;
		if (!get_write_avail_blocking(write_avail))
			break;

		float *interleaved = nullptr;
		if (FAILED(pRenderClient->GetBuffer(write_avail, reinterpret_cast<BYTE **>(&interleaved))))
		{
			LOGE("WASAPI: Failed to get buffer.\n");
			break;
		}

		UINT32 to_release = write_avail;

		while (write_avail != 0)
		{
			size_t to_write = std::min<size_t>(write_avail, MAX_NUM_FRAMES);
			callback->mix_samples(mix_channel_ptr, to_write);
			write_avail -= to_write;

			if (format->nChannels == 2)
			{
				DSP::interleave_stereo_f32(interleaved, mix_channels[0], mix_channels[1], to_write);
				interleaved += to_write * format->nChannels;
			}
			else
			{
				for (size_t f = 0; f < to_write; f++)
					for (unsigned c = 0; c < format->nChannels; c++)
						*interleaved++ = mix_channels[c][f];
			}
		}

		if (FAILED(pRenderClient->ReleaseBuffer(to_release, 0)))
		{
			LOGE("WASAPI: Failed to release buffer.\n");
			break;
		}
	}

	if (audio_task)
		AvRevertMmThreadCharacteristics(audio_task);

	if (FAILED(pAudioClient->Stop()))
	{
		LOGE("WASAPI: Failed to stop audio client.\n");
		return;
	}

	if (FAILED(pAudioClient->Reset()))
	{
		LOGE("WASAPI: Failed to reset audio client.\n");
		return;
	}
}

Backend *create_wasapi_backend(BackendCallback *callback, float sample_rate, unsigned channels)
{
	auto *backend = new WASAPIBackend(callback);
	if (!backend->init(sample_rate, channels))
	{
		delete backend;
		return nullptr;
	}
	return backend;
}
}
}

