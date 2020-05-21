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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <algorithm>
#include <atomic>

#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include "audio_interface.hpp"
#include "dsp/dsp.hpp"
#include "logging.hpp"

using namespace std;

static const size_t MAX_NUM_FRAMES = 256;

// Doesn't link properly on MinGW.
const static GUID _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
	0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

namespace Granite
{
namespace Audio
{
struct WASAPIBackend : Backend
{
	WASAPIBackend(BackendCallback &callback)
		: Backend(callback)
	{
		dead = false;
	}

	~WASAPIBackend();
	bool init(float sample_rate, unsigned channels);

	bool start() override;
	bool stop() override;

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

	thread thr;
	mutex lock;
	condition_variable cond;
	std::atomic<bool> dead;

	IMMDeviceEnumerator *pEnumerator = nullptr;
	IMMDevice *pDevice = nullptr;
	IAudioClient *pAudioClient = nullptr;
	IAudioRenderClient *pRenderClient = nullptr;
	UINT32 buffer_frames = 0;
	WAVEFORMATEX *format = nullptr;
	bool is_active = false;

	uint64_t padding_to_wait_period_us(uint32_t padding) noexcept;
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
}

static REFERENCE_TIME seconds_to_reference_time(double t)
{
	return REFERENCE_TIME(t * 10000000.0 + 0.5);
}

static double reference_time_to_seconds(REFERENCE_TIME t)
{
	return double(t) / 10000000.0;
}

bool WASAPIBackend::init(float, unsigned channels)
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

	const double target_latency = 0.030;
	auto reference_time = seconds_to_reference_time(target_latency);

	if (FAILED(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
	                                    0, reference_time, 0, format, nullptr)))
	{
		LOGE("WASAPI: Failed to initialize audio client.\n");
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

	REFERENCE_TIME latency;
	if (FAILED(pAudioClient->GetStreamLatency(&latency)))
	{
		LOGE("WASAPI: Failed to get stream latency.\n");
		return false;
	}

	double server_latency = reference_time_to_seconds(latency);
	server_latency += double(buffer_frames) / get_sample_rate();
	callback.set_latency_usec(uint32_t(server_latency * 1e6));

	callback.set_backend_parameters(get_sample_rate(), get_num_channels(), MAX_NUM_FRAMES);
	return true;
}

bool WASAPIBackend::start()
{
	if (is_active)
		return false;
	is_active = true;
	dead = false;

	callback.on_backend_start();
	thr = thread(&WASAPIBackend::thread_runner, this);
	return true;
}

bool WASAPIBackend::stop()
{
	if (!is_active)
		return false;
	is_active = false;

	if (thr.joinable())
	{
		{
			lock_guard<mutex> holder{ lock };
			dead.store(true, std::memory_order_relaxed);
			cond.notify_all();
		}
		thr.join();
	}

	callback.on_backend_stop();
	return true;
}

uint64_t WASAPIBackend::padding_to_wait_period_us(uint32_t padding) noexcept
{
	float padding_seconds = padding / float(format->nSamplesPerSec);
	float max_wait = padding_seconds * 0.5f;
	if (max_wait > 0.01f)
		max_wait = 0.01f;

	return uint64_t(max_wait * 1e6f);
}

void WASAPIBackend::thread_runner() noexcept
{
	float *interleaved = nullptr;
	if (FAILED(pRenderClient->GetBuffer(buffer_frames, reinterpret_cast<BYTE **>(&interleaved))))
	{
		LOGE("WASAPI: Failed to get buffer (start).\n");
		return;
	}

	if (FAILED(pRenderClient->ReleaseBuffer(buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT)))
	{
		LOGE("WASAPI: Failed to release buffer (start).\n");
		return;
	}

	if (FAILED(pAudioClient->Start()))
	{
		LOGE("WASAPI: Failed to start audio client.\n");
		return;
	}

	float mix_channels[Backend::MaxAudioChannels][MAX_NUM_FRAMES];
	float *mix_channel_ptr[Backend::MaxAudioChannels];
	for (unsigned i = 0; i < format->nChannels; i++)
		mix_channel_ptr[i] = mix_channels[i];

	while (!dead.load(std::memory_order_relaxed))
	{
		UINT32 padding;
		if (FAILED(pAudioClient->GetCurrentPadding(&padding)))
		{
			LOGE("WASAPI: Failed to get buffer padding.\n");
			break;
		}

		UINT32 write_avail = buffer_frames - padding;
		bool done = false;

		while (write_avail == 0)
		{
			// Sleep for some appropriate time,
			// although we might be woken up by the main thread wanting to kill us.
			auto now = chrono::high_resolution_clock::now();
			now += chrono::microseconds(padding_to_wait_period_us(padding));

			{
				unique_lock<mutex> holder{ lock };
				cond.wait_until(holder, now, [this]() {
					return dead.load(std::memory_order_relaxed);
				});

				if (dead.load(std::memory_order_relaxed))
				{
					done = true;
					break;
				}
			}

			if (FAILED(pAudioClient->GetCurrentPadding(&padding)))
			{
				LOGE("WASAPI: Failed to get buffer padding.\n");
				return;
			}

			write_avail = buffer_frames - padding;
		}

		if (done)
			break;

		if (FAILED(pRenderClient->GetBuffer(write_avail, reinterpret_cast<BYTE **>(&interleaved))))
		{
			LOGE("WASAPI: Failed to get buffer.\n");
			break;
		}

		UINT32 to_release = write_avail;

		while (write_avail != 0)
		{
			size_t to_write = std::min<size_t>(write_avail, MAX_NUM_FRAMES);
			callback.mix_samples(mix_channel_ptr, to_write);
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

Backend *create_wasapi_backend(BackendCallback &callback, float sample_rate, unsigned channels)
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

