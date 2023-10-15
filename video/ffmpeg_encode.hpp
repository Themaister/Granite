/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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

#include "device.hpp"
#include "image.hpp"
#include "slangmosh_encode_iface.hpp"

namespace Granite
{
namespace Audio
{
class DumpBackend;
class RecordStream;
}

class MuxStreamCallback
{
public:
	virtual ~MuxStreamCallback() = default;
	virtual bool write_stream(const void *data, size_t size) = 0;
};

class VideoEncoder
{
public:
	VideoEncoder();
	~VideoEncoder();

	struct Timebase
	{
		int num = 0;
		int den = 0;
	};

	enum class Format
	{
		NV12
	};

	enum ChromaSiting
	{
		Center,
		TopLeft,
		Left
	};

	struct Options
	{
		unsigned width = 0;
		unsigned height = 0;
		Timebase frame_timebase = {};
		Format format = Format::NV12; // Default for HW encode.
		ChromaSiting siting = ChromaSiting::Left; // Default for H.264.
		// Correlate PTS with wall time.
		bool realtime = false;
		const char *encoder = "libx264";

		struct
		{
			unsigned bitrate_kbits = 6000;
			unsigned max_bitrate_kbits = 8000;
			float gop_seconds = 2.0f;
			unsigned vbv_size_kbits = 6000;
			const char *x264_preset = "fast";
			const char *x264_tune = nullptr;
			const char *muxer_format = nullptr;
			// Also writes packets to a local file.
			const char *local_backup_path = nullptr;
			unsigned threads = 0;
		} realtime_options;
	};

	void set_audio_source(Audio::DumpBackend *backend);
	void set_audio_record_stream(Audio::RecordStream *stream);
	void set_mux_stream_callback(MuxStreamCallback *callback);

	bool init(Vulkan::Device *device, const char *path, const Options &options);

	struct PlaneLayout
	{
		size_t offset;
		size_t stride;
		size_t row_length;
	};

	struct YCbCrPipelineData;
	struct YCbCrPipelineDataDeleter
	{
		void operator()(YCbCrPipelineData *ptr);
	};
	using YCbCrPipeline = std::unique_ptr<YCbCrPipelineData, YCbCrPipelineDataDeleter>;

	YCbCrPipeline create_ycbcr_pipeline(const FFmpegEncode::Shaders<> &shaders) const;
	void process_rgb(Vulkan::CommandBuffer &cmd, YCbCrPipeline &pipeline, const Vulkan::ImageView &view);
	// Handles GPU synchronization if required.
	void submit_process_rgb(Vulkan::CommandBufferHandle &cmd, YCbCrPipeline &pipeline);

	int64_t sample_realtime_pts() const;

	bool encode_frame(YCbCrPipeline &pipeline, int64_t pts, int compensate_audio_us = 0);

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
}
