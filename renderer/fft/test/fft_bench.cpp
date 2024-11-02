/* Copyright (c) 2022-2024 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "device.hpp"
#include "context.hpp"
#include "global_managers_init.hpp"
#include "math.hpp"
#include "fft.hpp"

using namespace Granite;
using namespace Vulkan;

static void log_bench(Device &device)
{
	device.wait_idle();
	device.timestamp_log([&](const std::string &tag, const TimestampIntervalReport &report) {
		if (tag == "FFT")
			LOGI("Time per FFT: %.3f us\n", 1e6 * (report.time_per_frame_context / 10.0));
	});
	device.timestamp_log_reset();
}

struct BenchParams
{
	unsigned width;
	unsigned height;
	unsigned depth;
	unsigned dimensions;
	unsigned iterations;
	bool fp16;
	FFT::Mode mode;
};

static void bench(Device &device, const BenchParams &params)
{
	FFT::Options options = {};
	options.Nx = params.width;
	options.Ny = params.height;
	options.Nz = params.depth;
	options.dimensions = params.dimensions;
	options.data_type = params.fp16 ? FFT::DataType::FP16 : FFT::DataType::FP32;
	options.mode = params.mode;

	FFT fft;
	if (!fft.plan(&device, options))
	{
		LOGE("Failed to plan FFT.\n");
		return;
	}

	BufferCreateInfo info = {};
	info.size = params.width * params.height * params.depth * sizeof(vec2);
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.domain = BufferDomain::Device;
	auto input_buffer = device.create_buffer(info);
	auto output_buffer = device.create_buffer(info);

	FFT::Resource dst = {}, src = {};
	dst.buffer.buffer = output_buffer.get();
	dst.buffer.size = output_buffer->get_create_info().size;
	dst.buffer.row_stride = params.width;
	dst.buffer.layer_stride = params.width * params.height;
	src.buffer.buffer = input_buffer.get();
	src.buffer.size = input_buffer->get_create_info().size;
	src.buffer.row_stride = params.width;
	src.buffer.layer_stride = params.width * params.height;

	unsigned num_submits = (params.iterations + 9) / 10;

	for (unsigned i = 0; i < num_submits; i++)
	{
		auto cmd = device.request_command_buffer();
		auto begin_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		for (unsigned j = 0; j < 10; j++)
		{
			fft.execute(*cmd, dst, src);
			cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			             VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
		}
		auto end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		device.register_time_interval("GPU", std::move(begin_ts), std::move(end_ts), "FFT");
		device.submit(cmd);
		device.next_frame_context();
	}
}

int main()
{
	Global::init(Global::MANAGER_FEATURE_DEFAULT_BITS, 1);

	Context ctx;
	Context::SystemHandles handles;
	handles.filesystem = GRANITE_FILESYSTEM();
	ctx.set_system_handles(handles);

	if (!Context::init_loader(nullptr))
		return 1;
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return 1;

	Device device;
	device.set_context(ctx);

	BenchParams params = {};
	params.width = 1024;
	params.height = 1024;
	params.depth = 1;
	params.dimensions = 2;
	params.iterations = 10000;
	params.fp16 = true;
	params.mode = FFT::Mode::ForwardComplexToComplex;
	bench(device, params);
	log_bench(device);
}
