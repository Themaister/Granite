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

#include "audio_opensl.hpp"
#include "dsp/dsp.hpp"
#include <SLES/OpenSLES_Android.h>
#include "logging.hpp"
#include <string.h>
#include <cmath>

using namespace std;

namespace Granite
{
namespace Audio
{
static unsigned global_target_sample_rate;
static unsigned global_target_block_frames;

struct OpenSLESBackend : Backend
{
	OpenSLESBackend(BackendCallback &callback)
		: Backend(callback)
	{
	}

	~OpenSLESBackend();
	bool init(float target_sample_rate, unsigned channels);

	vector<vector<int16_t>> buffers;
	vector<float> mix_buffers[2];
	unsigned buffer_index = 0;
	unsigned buffer_count = 0;
	unsigned block_frames = 0;
	unsigned enqueued_blocks = 0;

	SLObjectItf engine_object = nullptr;
	SLEngineItf engine = nullptr;
	SLObjectItf output_mix = nullptr;
	SLObjectItf buffer_queue_object = nullptr;
	SLAndroidSimpleBufferQueueItf buffer_queue = nullptr;
	SLPlayItf player = nullptr;

	void thread_callback() noexcept;

	const char *get_backend_name() override
	{
		return "OpenSLES";
	}

	float get_sample_rate() override
	{
		return sample_rate;
	}

	unsigned get_num_channels() override
	{
		return num_channels;
	}

	bool start() override;
	bool stop() override;

	float sample_rate = 0.0f;
	unsigned num_channels = 0;
	bool is_active = false;
};

static void opensl_callback(SLAndroidSimpleBufferQueueItf itf, void *ctx);

bool OpenSLESBackend::init(float target_sample_rate, unsigned)
{
	if (global_target_sample_rate)
		target_sample_rate = float(global_target_sample_rate);

	sample_rate = target_sample_rate;
	num_channels = 2;

	block_frames = 256;
	if (global_target_sample_rate)
		block_frames = global_target_block_frames;

	if (slCreateEngine(&engine_object, 0, nullptr, 0, nullptr, nullptr) != SL_RESULT_SUCCESS)
		return false;

	if ((*engine_object)->Realize(engine_object, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS)
		return false;

	if ((*engine_object)->GetInterface(engine_object, SL_IID_ENGINE, &engine) != SL_RESULT_SUCCESS)
		return false;

	if ((*engine)->CreateOutputMix(engine, &output_mix, 0, nullptr, nullptr) != SL_RESULT_SUCCESS)
		return false;
	if ((*output_mix)->Realize(output_mix, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS)
		return false;

	// Aim for about 50 ms.
	buffer_count = unsigned(ceil(target_sample_rate / (20 * block_frames)));
	if (buffer_count < 2)
		buffer_count = 2;

	SLDataFormat_PCM fmt = {};
	fmt.formatType = SL_DATAFORMAT_PCM;
	fmt.numChannels = 2;
	fmt.samplesPerSec = uint32_t(target_sample_rate * 1000);
	fmt.bitsPerSample = 16;
	fmt.containerSize = 16;
	fmt.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
	fmt.endianness = SL_BYTEORDER_LITTLEENDIAN;

	SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {};
	SLDataLocator_OutputMix loc_outmix = {};
	SLDataSource audio_src = {};
	SLDataSink audio_sink = {};

	audio_src.pLocator = &loc_bufq;
	audio_src.pFormat = &fmt;

	loc_bufq.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
	loc_bufq.numBuffers = buffer_count;

	loc_outmix.locatorType = SL_DATALOCATOR_OUTPUTMIX;
	loc_outmix.outputMix = output_mix;

	audio_sink.pLocator = &loc_outmix;

	SLInterfaceID id = SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
	SLboolean req = SL_BOOLEAN_TRUE;

	if ((*engine)->CreateAudioPlayer(engine, &buffer_queue_object, &audio_src, &audio_sink,
	                                 1, &id, &req) != SL_RESULT_SUCCESS)
		return false;

	if ((*buffer_queue_object)->Realize(buffer_queue_object, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS)
		return false;

	if ((*buffer_queue_object)->GetInterface(buffer_queue_object,
	                                         SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
	                                         &buffer_queue) != SL_RESULT_SUCCESS)
		return false;

	buffers.resize(buffer_count);
	for (auto &buffer : buffers)
		buffer.resize(2 * block_frames);

	for (auto &mix : mix_buffers)
		mix.resize(block_frames);

	if ((*buffer_queue_object)->GetInterface(buffer_queue_object,
	                                         SL_IID_PLAY,
	                                         &player) != SL_RESULT_SUCCESS)
		return false;

	if ((*buffer_queue)->RegisterCallback(buffer_queue, opensl_callback, this) != SL_RESULT_SUCCESS)
		return false;

	callback.set_backend_parameters(this->sample_rate, num_channels, block_frames);

	double latency = double(buffer_count * block_frames) / this->sample_rate;
	uint32_t latency_usec = uint32_t(latency * 1e6);
	callback.set_latency_usec(latency_usec);

	return true;
}

void OpenSLESBackend::thread_callback() noexcept
{
	if (is_active)
	{
		assert(enqueued_blocks > 0);
		enqueued_blocks--;

		while (enqueued_blocks < buffer_count)
		{
			float *channels[2] = { mix_buffers[0].data(), mix_buffers[1].data() };
			callback.mix_samples(channels, block_frames);

			DSP::interleave_stereo_f32_i16(buffers[buffer_index].data(),
			                               channels[0], channels[1], block_frames);

			if ((*buffer_queue)->Enqueue(buffer_queue,
			                             buffers[buffer_index].data(),
			                             SLuint32(buffers[buffer_index].size() * sizeof(int16_t)))
			    != SL_RESULT_SUCCESS)
			{
				LOGE("Failed Enqueue in thread callback!\n");
			}
			else
			{
				buffer_index = (buffer_index + 1) % buffer_count;
				enqueued_blocks++;
			}
		}
	}
}

bool OpenSLESBackend::start()
{
	if (is_active)
		return false;

	if ((*buffer_queue)->Clear(buffer_queue) != SL_RESULT_SUCCESS)
		return false;

	buffer_index = 1;
	enqueued_blocks = 1;

	// Kick with one silent buffer.
	memset(buffers[0].data(), 0, buffers[0].size() * sizeof(int16_t));
	if ((*buffer_queue)->Enqueue(buffer_queue,
	                             buffers[0].data(),
	                             SLuint32(buffers[0].size() * sizeof(int16_t))) != SL_RESULT_SUCCESS)
	{
		return false;
	}

	is_active = true;
	callback.on_backend_start();
	return (*player)->SetPlayState(player, SL_PLAYSTATE_PLAYING) == SL_RESULT_SUCCESS;
}

bool OpenSLESBackend::stop()
{
	if (!is_active)
		return false;
	is_active = false;
	callback.on_backend_stop();

	if ((*player)->SetPlayState(player, SL_PLAYSTATE_STOPPED) != SL_RESULT_SUCCESS)
		return false;
	if ((*buffer_queue)->Clear(buffer_queue))
		return false;
	return true;
}

OpenSLESBackend::~OpenSLESBackend()
{
	stop();

	if (player)
		(*player)->SetPlayState(player, SL_PLAYSTATE_STOPPED);

	if (buffer_queue_object)
		(*buffer_queue_object)->Destroy(buffer_queue_object);
	if (output_mix)
		(*output_mix)->Destroy(output_mix);
	if (engine_object)
		(*engine_object)->Destroy(engine_object);
}

static void opensl_callback(SLAndroidSimpleBufferQueueItf, void *ctx)
{
	auto *sl = static_cast<OpenSLESBackend *>(ctx);
	sl->thread_callback();
}

Backend *create_opensl_backend(BackendCallback &callback, float sample_rate, unsigned channels)
{
	auto *sl = new OpenSLESBackend(callback);
	if (!sl->init(sample_rate, channels))
	{
		delete sl;
		return nullptr;
	}
	return sl;
}

void set_opensl_low_latency_parameters(unsigned sample_rate, unsigned block_frames)
{
	global_target_sample_rate = sample_rate;
	global_target_block_frames = block_frames;
}
}
}
