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
struct VorbisStream : BackendCallback
{
	~VorbisStream();
	bool init(const string &path);

	void mix_samples(float * const *channels, size_t num_frames) override;
	void on_backend_start(float sample_rate, unsigned channels, size_t max_num_samples) override;

	stb_vorbis *file = nullptr;
	unique_ptr<File> filesystem_file;

	unsigned num_channels = 0;
};

void VorbisStream::on_backend_start(float, unsigned num_channels, size_t)
{
	this->num_channels = num_channels;
}

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

	return true;
}

void VorbisStream::mix_samples(float *const *channels, size_t num_frames)
{
	auto actual_frames = stb_vorbis_get_samples_float(file, num_channels, const_cast<float **>(channels), int(num_frames));
	for (unsigned c = 0; c < 2; c++)
		memset(channels[c] + actual_frames, 0, (num_frames - actual_frames) * sizeof(float));
}

VorbisStream::~VorbisStream()
{
	if (file)
		stb_vorbis_close(file);
}

unique_ptr<BackendCallback> create_vorbis_stream(const string &path)
{
	auto vorbis = make_unique<VorbisStream>();
	if (!vorbis)
		return {};
	if (!vorbis->init(path))
		return {};
	return move(vorbis);
}
}
}