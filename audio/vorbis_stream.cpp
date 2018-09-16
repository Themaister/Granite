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

#include "vorbis_stream.hpp"
#include "filesystem.hpp"
#include "stb_vorbis.h"
#include "util.hpp"
#include <string.h>

using namespace std;

namespace Granite
{
namespace Audio
{
struct VorbisStream : MixerStream
{
	~VorbisStream();
	bool init(const string &path);

	size_t accumulate_samples(float * const *channels, const float *gains, size_t num_frames) noexcept override;

	float get_sample_rate() const override
	{
		return sample_rate;
	}

	unsigned get_num_channels() const override
	{
		return num_channels;
	}

	void set_max_num_frames(size_t num_frames)
	{
		for (auto &mix : mix_buffer)
			mix.clear();

		for (unsigned c = 0; c < num_channels; c++)
		{
			mix_buffer[c].resize(num_frames);
			mix_channels[c] = mix_buffer[c].data();
		}
	}

	stb_vorbis *file = nullptr;
	unique_ptr<File> filesystem_file;

	float sample_rate = 0.0f;
	unsigned num_channels = 0;

	std::vector<float> mix_buffer[Backend::MaxAudioChannels];
	float *mix_channels[Backend::MaxAudioChannels] = {};
};

bool VorbisStream::init(const string &path)
{
	filesystem_file = Filesystem::get().open(path, FileMode::ReadOnly);
	if (!filesystem_file)
		return false;

	if (filesystem_file->get_size() == 0)
		return false;
	void *mapped = filesystem_file->map();
	if (!mapped)
		return false;

	int error;
	file = stb_vorbis_open_memory(static_cast<const unsigned char *>(mapped), int(filesystem_file->get_size()),
	                              &error, nullptr);
	if (!file)
	{
		LOGE("Failed to load Vorbis file, error: %d\n", error);
		return false;
	}

	auto info = stb_vorbis_get_info(file);
	sample_rate = info.sample_rate;
	num_channels = unsigned(info.channels);

	return true;
}

size_t VorbisStream::accumulate_samples(float *const *channels, const float *gains, size_t num_frames) noexcept
{
	auto actual_frames = stb_vorbis_get_samples_float(file, num_channels, mix_channels, int(num_frames));
	for (unsigned c = 0; c < num_channels; c++)
		DSP::accumulate_channel(channels[c], mix_channels[c], gains[c], size_t(actual_frames));
	return size_t(actual_frames);
}

VorbisStream::~VorbisStream()
{
	if (file)
		stb_vorbis_close(file);
}

MixerStream *create_vorbis_stream(const string &path)
{
	auto vorbis = new VorbisStream;
	if (!vorbis->init(path))
	{
		vorbis->dispose();
		return nullptr;
	}

	return vorbis;
}
}
}