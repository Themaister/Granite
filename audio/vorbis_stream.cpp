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

#include "vorbis_stream.hpp"
#include "filesystem.hpp"
#include "dsp/dsp.hpp"
#include "stb_vorbis.h"
#include "logging.hpp"
#include <string.h>
#include <algorithm>

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

	void setup(float, unsigned, size_t num_frames) override
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
	bool looping = false;

	std::vector<float> mix_buffer[Backend::MaxAudioChannels];
	float *mix_channels[Backend::MaxAudioChannels] = {};
};

struct DecodedVorbisStream : MixerStream
{
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

	std::vector<float> decoded_audio[Backend::MaxAudioChannels];
	size_t offset = 0;
	float sample_rate = 0.0f;
	unsigned num_channels = 0;
	bool looping = false;
};

bool VorbisStream::init(const string &path)
{
	filesystem_file = Global::filesystem()->open(path, FileMode::ReadOnly);
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

bool DecodedVorbisStream::init(const string &path)
{
	auto filesystem_file = Global::filesystem()->open(path, FileMode::ReadOnly);
	if (!filesystem_file)
		return false;

	if (filesystem_file->get_size() == 0)
		return false;
	void *mapped = filesystem_file->map();
	if (!mapped)
		return false;

	int error;
	stb_vorbis *file = stb_vorbis_open_memory(static_cast<const unsigned char *>(mapped),
	                                          int(filesystem_file->get_size()),
	                                          &error, nullptr);
	if (!file)
	{
		LOGE("Failed to load Vorbis file, error: %d\n", error);
		return false;
	}

	auto info = stb_vorbis_get_info(file);
	sample_rate = info.sample_rate;
	num_channels = unsigned(info.channels);

	float block[Backend::MaxAudioChannels][256];
	float *mix_channels[Backend::MaxAudioChannels];
	for (unsigned c = 0; c < num_channels; c++)
		mix_channels[c] = block[c];

	int ret;
	while ((ret = stb_vorbis_get_samples_float(file, int(num_channels), mix_channels, 256)) > 0)
		for (unsigned c = 0; c < num_channels; c++)
			decoded_audio[c].insert(end(decoded_audio[c]), mix_channels[c], mix_channels[c] + ret);

	if (ret < 0)
		goto error;

	stb_vorbis_close(file);
	return true;

error:
	stb_vorbis_close(file);
	return false;
}

size_t DecodedVorbisStream::accumulate_samples(float *const *channels, const float *gains, size_t num_frames) noexcept
{
	size_t to_write = std::min(decoded_audio[0].size() - offset, num_frames);

	for (unsigned c = 0; c < num_channels; c++)
		DSP::accumulate_channel(channels[c], decoded_audio[c].data() + offset, gains[c], to_write);

	offset += to_write;

	if (offset >= decoded_audio[0].size())
	{
		if (looping)
			offset = 0;
		else
			return to_write;
	}

	size_t spill_to_write = num_frames - to_write;
	if (spill_to_write)
	{
		float *modified_channels[Backend::MaxAudioChannels];
		for (unsigned c = 0; c < num_channels; c++)
			modified_channels[c] = channels[c] + to_write;

		return accumulate_samples(modified_channels, gains, spill_to_write) + to_write;
	}
	else
		return to_write;
}

size_t VorbisStream::accumulate_samples(float * const *channels, const float *gains, size_t num_frames) noexcept
{
	auto actual_frames = stb_vorbis_get_samples_float(file, num_channels, mix_channels, int(num_frames));
	if (actual_frames < 0)
		return 0;

	for (unsigned c = 0; c < num_channels; c++)
		DSP::accumulate_channel(channels[c], mix_channels[c], gains[c], size_t(actual_frames));

	if (looping && size_t(actual_frames) < num_frames)
	{
		stb_vorbis_seek_start(file);
		float *moved_channels[Backend::MaxAudioChannels];
		for (unsigned c = 0; c < num_channels; c++)
			moved_channels[c] = channels[c] + actual_frames;

		actual_frames += accumulate_samples(moved_channels, gains, num_frames - actual_frames);
	}
	return size_t(actual_frames);
}

VorbisStream::~VorbisStream()
{
	if (file)
		stb_vorbis_close(file);
}

MixerStream *create_vorbis_stream(const string &path, bool looping)
{
	auto vorbis = new VorbisStream;
	if (!vorbis->init(path))
	{
		vorbis->dispose();
		return nullptr;
	}

	vorbis->looping = looping;
	return vorbis;
}

MixerStream *create_decoded_vorbis_stream(const string &path, bool looping)
{
	auto vorbis = new DecodedVorbisStream;
	if (!vorbis->init(path))
	{
		vorbis->dispose();
		return nullptr;
	}

	vorbis->looping = looping;
	return vorbis;
}
}
}