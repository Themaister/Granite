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

#pragma once

#include <memory>

struct AVCodecContext;
struct AVCodec;

namespace Vulkan
{
class Device;
}

namespace Granite
{
struct FFmpegHWDevice
{
public:
	FFmpegHWDevice();
	~FFmpegHWDevice();

	bool init_codec_context(const AVCodec *codec, Vulkan::Device *device, AVCodecContext *ctx, const char *type, bool encode);
	bool init_frame_context(AVCodecContext *ctx, unsigned width, unsigned height, int sw_pixel_format);
	int get_hw_device_type() const;
	int get_pix_fmt() const;
	int get_sw_pix_fmt() const;
	void reset();

	struct Impl;
	std::unique_ptr<Impl> impl;
};
}
