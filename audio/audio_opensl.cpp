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

#include "audio_opensl.hpp"
#include <SLES/OpenSLES_Android.h>
#include "util.hpp"
#include <string.h>

using namespace std;

namespace Granite
{
namespace Audio
{
static unsigned target_sample_rate;
static unsigned target_block_frames;

struct OpenSLESBackend : Backend
{
	~OpenSLESBackend();
	bool init(float sample_rate, unsigned channels);

	vector<vector<int16_t>> buffers;
	vector<vector<float>> mix_buffers;
	unsigned buffer_index = 0;
	unsigned buffer_count = 0;

	SLObjectItf engine_object = nullptr;
	SLEngineItf engine = nullptr;
	SLObjectItf output_mix = nullptr;
	SLObjectItf buffer_queue_object = nullptr;
	SLAndroidSimpleBufferQueueItf buffer_queue = nullptr;
	SLPlayItf player = nullptr;

	void callback() noexcept;
};

static void opensl_callback(SLAndroidSimpleBufferQueueItf, void *ctx)
{
	auto *sl = static_cast<OpenSLESBackend *>(ctx);
	sl->callback();
}

std::unique_ptr<Backend> create_opensl_backend(float sample_rate, unsigned channels)
{
	auto sl = make_unique<OpenSLESBackend>();
	if (!sl->init(sample_rate, channels))
		return {};
	return move(sl);
}

void set_opensl_low_latency_parameters(unsigned sample_rate, unsigned block_frames)
{
	target_sample_rate = sample_rate;
	target_block_frames = block_frames;
}
}
}
