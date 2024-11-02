/* Copyright (c) 2015-2024 Hans-Kristian Arntzen
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

#include "fft.h"
#include "device.hpp"
#include "context.hpp"
#include "global_managers_init.hpp"
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include "fft.hpp"
#include <random>

using namespace Granite;
using namespace Vulkan;

static void fill_random_inputs(vec2 *data, unsigned N)
{
	std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
	std::mt19937 rnd(10);
	for (unsigned i = 0; i < N; i++)
	{
		data[i].x = dist(rnd);
		data[i].y = dist(rnd);
	}
}

static void quantize_inputs(u16vec2 *outputs, const vec2 *inputs, unsigned N)
{
	for (unsigned i = 0; i < N; i++)
		outputs[i] = floatToHalf(inputs[i]);
}

static void fill_random_inputs(float *data, unsigned N)
{
	std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
	std::mt19937 rnd(10);
	for (unsigned i = 0; i < N; i++)
		data[i] = dist(rnd);
}

static bool validate_outputs(const float *a, const float *b, unsigned N)
{
	double power = 0.0;
	double squared_error = 0.0;

	for (unsigned i = 0; i < N; i++)
	{
		power += a[i] * a[i];
		float diff = b[i] - a[i];
		squared_error += diff * diff;
	}

	power /= N;
	squared_error /= N;
	if (std::isnan(squared_error) || squared_error > 1e-10 * power)
	{
		LOGE("Error! N = %u, SNR = %f dB.\n", N, 10.0 * log10(power / squared_error));
		return false;
	}
	else
		return true;
}

static bool validate_outputs(const vec2 *a, const vec2 *b, unsigned N)
{
	double power = 0.0;
	double squared_error = 0.0;

	for (unsigned i = 0; i < N; i++)
	{
		power += dot(a[i], a[i]);
		vec2 diff = b[i] - a[i];
		squared_error += dot(diff, diff);
	}

	power /= N;
	squared_error /= N;
	if (std::isnan(squared_error) || (squared_error > 1e-10 * power))
	{
		LOGE("Error! N = %u, SNR = %f dB.\n", N, 10.0 * log10(power / squared_error));
		return false;
	}
	else
		return true;
}

static bool validate_outputs_fp16(const vec2 *a, const u16vec2 *b, unsigned N)
{
	double power = 0.0;
	double squared_error = 0.0;

	for (unsigned i = 0; i < N; i++)
	{
		vec2 a_value = a[i];
		vec2 b_value = halfToFloat(b[i]);
		power += dot(a_value, a_value);
		vec2 diff = b_value - a_value;
		squared_error += dot(diff, diff);
	}

	power /= N;
	squared_error /= N;
	if (std::isnan(squared_error) || squared_error > 5e-4 * power)
	{
		LOGE("Error! N = %u, SNR = %f dB.\n", N, 10.0 * log10(power / squared_error));
		return false;
	}
	else
		return true;
}

static bool test_fft_2d(Device &device, unsigned Nx, unsigned Ny,
                        FFT::Mode mode, FFT::DataType data_type, unsigned batch_count,
                        bool texture_input, bool texture_output)
{
	bool fp16 = data_type == FFT::DataType::FP16;

	FFT fft;
	FFT::Options options = {};
	options.Nx = Nx;
	options.Ny = Ny;
	options.Nz = batch_count;
	options.dimensions = 2;
	options.mode = mode;
	options.data_type = data_type;
	options.input_resource = texture_input ? FFT::ResourceType::Texture : FFT::ResourceType::Buffer;
	options.output_resource = texture_output ? FFT::ResourceType::Texture : FFT::ResourceType::Buffer;
	if (!fft.plan(&device, options))
		return false;

	mufft_plan_2d *plan_2d;

	if (mode == FFT::Mode::RealToComplex)
		plan_2d = mufft_create_plan_2d_r2c(Nx, Ny, 0);
	else if (mode == FFT::Mode::ComplexToReal)
		plan_2d = mufft_create_plan_2d_c2r(Nx, Ny, 0);
	else
		plan_2d = mufft_create_plan_2d_c2c(Nx, Ny, mode == FFT::Mode::ForwardComplexToComplex ? -1 : +1, 0);

	const unsigned input_divider = mode == FFT::Mode::RealToComplex ? 2 : 1;
	const unsigned output_divider = mode == FFT::Mode::ComplexToReal ? 2 : 1;
	auto *input_data = static_cast<vec2 *>(mufft_alloc(Nx * Ny * batch_count * sizeof(vec2) / input_divider));
	auto *output_data = static_cast<vec2 *>(mufft_alloc(Nx * Ny * batch_count * sizeof(vec2)));

	fill_random_inputs(input_data, Nx * Ny * batch_count / input_divider);

	if (mode == FFT::Mode::ComplexToReal)
	{
		for (unsigned i = 0; i < Ny * batch_count; i++)
		{
			input_data[Nx * i].y = 0.0f;
			input_data[Nx * i + (Nx / 2)].y = 0.0f;
		}
	}

	auto *input_data_fp16 = static_cast<u16vec2 *>(mufft_alloc(Nx * Ny * batch_count * sizeof(u16vec2) / input_divider));
	if (fp16)
		quantize_inputs(input_data_fp16, input_data, Nx * Ny * batch_count / input_divider);

	BufferCreateInfo buffer_info = {};
	buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buffer_info.domain = BufferDomain::CachedHost;

	buffer_info.size = Nx * Ny * batch_count * (fp16 ? sizeof(u16vec2) : sizeof(vec2)) / input_divider;
	buffer_info.usage = texture_input ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	auto input_buffer = device.create_buffer(buffer_info,
	                                         fp16 ?
	                                         static_cast<const void *>(input_data_fp16) :
	                                         static_cast<const void *>(input_data));
	device.set_name(*input_buffer, "input-buffer");
	buffer_info.size = Nx * Ny * batch_count * (fp16 ? sizeof(u16vec2) : sizeof(vec2)) / output_divider;
	buffer_info.usage = texture_output ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	auto output_buffer = device.create_buffer(buffer_info, nullptr);
	device.set_name(*output_buffer, "output-buffer");

	ImageHandle input_texture;
	ImageHandle output_texture;
	ImageCreateInfo image_info = ImageCreateInfo::render_target(Nx, Ny, VK_FORMAT_UNDEFINED);
	image_info.format = mode == FFT::Mode::RealToComplex ?
	                    (fp16 ? VK_FORMAT_R16_SFLOAT : VK_FORMAT_R32_SFLOAT) :
	                    (fp16 ? VK_FORMAT_R16G16_SFLOAT : VK_FORMAT_R32G32_SFLOAT);
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (texture_input)
		input_texture = device.create_image(image_info);
	image_info.format = mode == FFT::Mode::ComplexToReal ?
	                    (fp16 ? VK_FORMAT_R16_SFLOAT : VK_FORMAT_R32_SFLOAT) :
	                    (fp16 ? VK_FORMAT_R16G16_SFLOAT : VK_FORMAT_R32G32_SFLOAT);
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (texture_output)
		output_texture = device.create_image(image_info);

	for (unsigned i = 0; i < batch_count; i++)
		mufft_execute_plan_2d(plan_2d, output_data + i * Nx * Ny / output_divider, input_data + i * Nx * Ny / input_divider);

#define RDOC 0
#if RDOC
	bool has_rdoc = Device::init_renderdoc_capture();
	if (has_rdoc)
		device.begin_renderdoc_capture();
#endif

	auto cmd = device.request_command_buffer();
	FFT::Resource dst = {};
	FFT::Resource src = {};

	if (input_texture)
	{
		cmd->image_barrier(*input_texture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                   VK_PIPELINE_STAGE_NONE, 0,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

		cmd->copy_buffer_to_image(*input_texture, *input_buffer,
		                          0, {}, { Nx, Ny, 1 },
		                          0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
		cmd->image_barrier(*input_texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

		src.image.view = &input_texture->get_view();
		src.image.stock_sampler = StockSampler::NearestClamp;
		src.image.input_offset[0] = 0.5f / float(image_info.width);
		src.image.input_offset[1] = 0.5f / float(image_info.height);
		src.image.input_scale[0] = 1.0f / float(image_info.width);
		src.image.input_scale[1] = 1.0f / float(image_info.height);
	}
	else
	{
		src.buffer.buffer = input_buffer.get();
		src.buffer.size = input_buffer->get_create_info().size;
		src.buffer.row_stride = Nx;
		src.buffer.layer_stride = Nx * Ny;
	}

	if (output_texture)
	{
		cmd->image_barrier(*output_texture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		                   VK_PIPELINE_STAGE_NONE, 0,
		                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
		dst.image.view = &output_texture->get_view();
	}
	else
	{
		dst.buffer.buffer = output_buffer.get();
		dst.buffer.size = output_buffer->get_create_info().size;
		dst.buffer.row_stride = Nx;
		dst.buffer.layer_stride = Nx * Ny;
	}

	fft.execute(*cmd, dst, src);

	if (output_texture)
	{
		cmd->image_barrier(*output_texture, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_READ_BIT);
		cmd->copy_image_to_buffer(*output_buffer, *output_texture,
		                          0, {}, { Nx, Ny, 1 },
		                          0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
		cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	}
	else
	{
		cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	}

	device.submit(cmd);
#if RDOC
	if (has_rdoc)
		device.end_renderdoc_capture();
#endif
	device.wait_idle();

	const void *mapped_data = device.map_host_buffer(*output_buffer, MEMORY_ACCESS_READ_BIT);
	auto *mapped_data_fp32 = static_cast<const vec2 *>(mapped_data);
	auto *mapped_data_fp16 = static_cast<const u16vec2 *>(mapped_data);

	unsigned complex_outputs = Nx;
	if (mode == FFT::Mode::RealToComplex)
		complex_outputs = (Nx / 2) + 1;
	else if (mode == FFT::Mode::ComplexToReal)
		complex_outputs = Nx / 2;

	for (unsigned i = 0; i < Ny * batch_count; i++)
	{
		if (fp16)
		{
			if (!validate_outputs_fp16(output_data + i * Nx / output_divider,
			                           mapped_data_fp16 + i * Nx / output_divider,
			                           complex_outputs))
			{
				LOGE("Failed at i = %u.\n", i);
				return false;
			}
		}
		else
		{
			if (!validate_outputs(output_data + i * Nx / output_divider,
			                      mapped_data_fp32 + i * Nx / output_divider,
			                      complex_outputs))
			{
				LOGE("Failed at i = %u.\n", i);
				return false;
			}
		}
	}

	mufft_free(input_data);
	mufft_free(input_data_fp16);
	mufft_free(output_data);
	mufft_free_plan_2d(plan_2d);

	return true;
}

static bool test_fft_1d_c2r(Device &device, unsigned N, unsigned batch_count)
{
	FFT fft;
	FFT::Options options = {};
	options.Nx = N;
	options.Ny = batch_count;
	options.mode = FFT::Mode::ComplexToReal;
	if (!fft.plan(&device, options))
		return false;

	mufft_plan_1d *plan_1d = mufft_create_plan_1d_c2r(N, 0);
	auto *input_data = static_cast<vec2 *>(mufft_alloc(N * batch_count * sizeof(vec2)));
	auto *output_data = static_cast<float *>(mufft_alloc(N * batch_count * sizeof(float)));
	fill_random_inputs(input_data, N * batch_count);

	for (unsigned i = 0; i < batch_count; i++)
	{
		input_data[i * N + 0].y = 0.0f;
		input_data[i * N + N / 2].y = 0.0f;
	}

	BufferCreateInfo buffer_info = {};
	buffer_info.size = N * batch_count * sizeof(vec2);
	buffer_info.domain = BufferDomain::CachedHost;
	buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	auto input_buffer = device.create_buffer(buffer_info, input_data);
	buffer_info.size = N * batch_count * sizeof(float);
	auto output_buffer = device.create_buffer(buffer_info, nullptr);

	for (unsigned i = 0; i < batch_count; i++)
		mufft_execute_plan_1d(plan_1d, output_data + N * i, input_data + N * i);

	auto cmd = device.request_command_buffer();
	FFT::Resource dst = {};
	FFT::Resource src = {};
	src.buffer.buffer = input_buffer.get();
	src.buffer.size = input_buffer->get_create_info().size;
	src.buffer.row_stride = N;
	dst.buffer.buffer = output_buffer.get();
	dst.buffer.size = output_buffer->get_create_info().size;
	dst.buffer.row_stride = N;

	fft.execute(*cmd, dst, src);

	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	device.submit(cmd);
	device.wait_idle();

	auto *mapped_data = static_cast<const float *>(device.map_host_buffer(*output_buffer, MEMORY_ACCESS_READ_BIT));
	for (unsigned i = 0; i < batch_count; i++)
		if (!validate_outputs(output_data + N * i, mapped_data + N * i, N))
			return false;

	mufft_free(input_data);
	mufft_free(output_data);
	mufft_free_plan_1d(plan_1d);

	return true;
}

static bool test_fft_1d_r2c(Device &device, unsigned N, unsigned batch_count)
{
	FFT fft;
	FFT::Options options = {};
	options.Nx = N;
	options.Ny = batch_count;
	options.mode = FFT::Mode::RealToComplex;
	if (!fft.plan(&device, options))
		return false;

	mufft_plan_1d *plan_1d = mufft_create_plan_1d_r2c(N, 0);
	auto *input_data = static_cast<float *>(mufft_alloc(N * batch_count * sizeof(float)));
	auto *output_data = static_cast<vec2 *>(mufft_alloc(N * batch_count * sizeof(vec2)));
	fill_random_inputs(input_data, N * batch_count);

	BufferCreateInfo buffer_info = {};
	buffer_info.size = N * batch_count * sizeof(float);
	buffer_info.domain = BufferDomain::CachedHost;
	buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	auto input_buffer = device.create_buffer(buffer_info, input_data);
	buffer_info.size = N * batch_count * sizeof(vec2);
	auto output_buffer = device.create_buffer(buffer_info, nullptr);

	for (unsigned i = 0; i < batch_count; i++)
		mufft_execute_plan_1d(plan_1d, output_data + N * i, input_data + N * i);

	auto cmd = device.request_command_buffer();
	FFT::Resource dst = {};
	FFT::Resource src = {};
	src.buffer.buffer = input_buffer.get();
	src.buffer.size = input_buffer->get_create_info().size;
	src.buffer.row_stride = N;
	dst.buffer.buffer = output_buffer.get();
	dst.buffer.size = output_buffer->get_create_info().size;
	dst.buffer.row_stride = N;

	fft.execute(*cmd, dst, src);

	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	device.submit(cmd);
	device.wait_idle();

	auto *mapped_data = static_cast<const vec2 *>(device.map_host_buffer(*output_buffer, MEMORY_ACCESS_READ_BIT));
	for (unsigned i = 0; i < batch_count; i++)
		if (!validate_outputs(output_data + N * i, mapped_data + N * i, (N / 2) + 1))
			return false;

	mufft_free(input_data);
	mufft_free(output_data);
	mufft_free_plan_1d(plan_1d);

	return true;
}

static bool test_fft_1d_c2c(Device &device, unsigned N, int dir, unsigned batch_count)
{
	FFT fft;
	FFT::Options options = {};
	options.Nx = N;
	options.Ny = batch_count;
	options.mode = dir < 0 ? FFT::Mode::ForwardComplexToComplex : FFT::Mode::InverseComplexToComplex;
	if (!fft.plan(&device, options))
		return false;

	mufft_plan_1d *plan_1d = mufft_create_plan_1d_c2c(N, dir, 0);
	auto *input_data = static_cast<vec2 *>(mufft_alloc(N * batch_count * sizeof(vec2)));
	auto *output_data = static_cast<vec2 *>(mufft_alloc(N * batch_count * sizeof(vec2)));

	fill_random_inputs(input_data, N * batch_count);

	BufferCreateInfo buffer_info = {};
	buffer_info.size = N * batch_count * sizeof(vec2);
	buffer_info.domain = BufferDomain::CachedHost;
	buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	auto input_buffer = device.create_buffer(buffer_info, input_data);
	auto output_buffer = device.create_buffer(buffer_info, nullptr);

	for (unsigned i = 0; i < batch_count; i++)
		mufft_execute_plan_1d(plan_1d, output_data + N * i, input_data + N * i);
	auto cmd = device.request_command_buffer();
	FFT::Resource dst = {};
	FFT::Resource src = {};
	src.buffer.buffer = input_buffer.get();
	src.buffer.size = input_buffer->get_create_info().size;
	src.buffer.row_stride = N;
	dst.buffer.buffer = output_buffer.get();
	dst.buffer.size = output_buffer->get_create_info().size;
	dst.buffer.row_stride = N;

	fft.execute(*cmd, dst, src);

	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	device.submit(cmd);
	device.wait_idle();

	auto *mapped_data = static_cast<const vec2 *>(device.map_host_buffer(*output_buffer, MEMORY_ACCESS_READ_BIT));
	if (!validate_outputs(output_data, mapped_data, N * batch_count))
		return false;

	mufft_free(input_data);
	mufft_free(output_data);
	mufft_free_plan_1d(plan_1d);

	return true;
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

	for (unsigned N = 8; N <= 16 * 1024 * 1024; N *= 2)
	{
		LOGI("Testing 1D C2R (N = %u).\n", N);
		if (!test_fft_1d_c2r(device, N, 1))
			return 1;
	}

	for (unsigned N = 8; N <= 16 * 1024 * 1024; N *= 2)
	{
		LOGI("Testing 1D R2C (N = %u).\n", N);
		if (!test_fft_1d_r2c(device, N, 1))
			return 1;
	}

	for (unsigned N = 2048; N <= 1024 * 1024; N *= 2)
	{
		LOGI("Testing 1D R2C batched (N = %u).\n", N);
		if (!test_fft_1d_r2c(device, N, 15))
			return 1;
		LOGI("Testing 1D R2C batched (N = %u).\n", N);
		if (!test_fft_1d_r2c(device, N, 16))
			return 1;
		LOGI("Testing 1D C2R batched (N = %u).\n", N);
		if (!test_fft_1d_c2r(device, N, 15))
			return 1;
		LOGI("Testing 1D C2R batched (N = %u).\n", N);
		if (!test_fft_1d_c2r(device, N, 16))
			return 1;
	}

	for (unsigned N = 4; N <= 16 * 1024 * 1024; N *= 2)
	{
		LOGI("Testing 1D C2C (Forward) (N = %u).\n", N);
		if (!test_fft_1d_c2c(device, N, -1, 1))
			return 1;
		LOGI("Testing 1D C2C (Inverse) (N = %u).\n", N);
		if (!test_fft_1d_c2c(device, N, +1, 1))
			return 1;
	}

	for (unsigned Ny = 4; Ny <= 8 * 1024; Ny *= 2)
	{
		for (unsigned Nx = 4; Nx <= 8 * 1024; Nx *= 2)
		{
			LOGI("Testing 2D C2C (Forward) (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ForwardComplexToComplex, FFT::DataType::FP32, 1, false, false))
				return 1;
			LOGI("Testing 2D C2C (Inverse) (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::InverseComplexToComplex, FFT::DataType::FP32, 1, false, false))
				return 1;

			LOGI("Testing 2D C2C (FP16) (Forward) (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ForwardComplexToComplex, FFT::DataType::FP16, 1, false, false))
				return 1;
			LOGI("Testing 2D C2C (FP16) (Inverse) (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::InverseComplexToComplex, FFT::DataType::FP16, 1, false, false))
				return 1;
		}
	}

	for (unsigned N = 8; N <= 1 * 1024 * 1024; N *= 2)
	{
		LOGI("Testing 1D C2C Batched (Forward) (N = %u).\n", N);
		if (!test_fft_1d_c2c(device, N, -1, 15))
			return 1;
		LOGI("Testing 1D C2C Batched (Inverse) (N = %u).\n", N);
		if (!test_fft_1d_c2c(device, N, +1, 16))
			return 1;
	}

	for (unsigned Ny = 4; Ny <= 8 * 1024; Ny *= 2)
	{
		for (unsigned Nx = 8; Nx <= 8 * 1024; Nx *= 2)
		{
			LOGI("Testing 2D R2C (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::RealToComplex, FFT::DataType::FP32, 1, false, false))
				return 1;
			LOGI("Testing 2D C2R (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ComplexToReal, FFT::DataType::FP32, 1, false, false))
				return 1;

			LOGI("Testing 2D R2C (FP16) (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::RealToComplex, FFT::DataType::FP16, 1, false, false))
				return 1;
			LOGI("Testing 2D C2R (FP16) (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ComplexToReal, FFT::DataType::FP16, 1, false, false))
				return 1;
		}
	}

	for (unsigned Ny = 4; Ny <= 1024; Ny *= 2)
	{
		for (unsigned Nx = 8; Nx <= 1024; Nx *= 2)
		{
			LOGI("Testing 2D R2C batched (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::RealToComplex, FFT::DataType::FP32, 7, false, false))
				return 1;
			LOGI("Testing 2D C2R batched (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ComplexToReal, FFT::DataType::FP32, 6, false, false))
				return 1;

			LOGI("Testing 2D R2C input texture (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::RealToComplex, FFT::DataType::FP32, 1, true, false))
				return 1;
			LOGI("Testing 2D R2C output texture (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::RealToComplex, FFT::DataType::FP32, 1, false, true))
				return 1;
			LOGI("Testing 2D C2R input texture (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ComplexToReal, FFT::DataType::FP32, 1, true, false))
				return 1;
			LOGI("Testing 2D C2R output texture (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ComplexToReal, FFT::DataType::FP32, 1, false, true))
				return 1;

			LOGI("Testing 2D R2C (FP16) input texture (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::RealToComplex, FFT::DataType::FP16, 1, true, false))
				return 1;
			LOGI("Testing 2D R2C (FP16) output texture (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::RealToComplex, FFT::DataType::FP16, 1, false, true))
				return 1;
			LOGI("Testing 2D C2R (FP16) input texture (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ComplexToReal, FFT::DataType::FP16, 1, true, false))
				return 1;
			LOGI("Testing 2D C2R (FP16) output texture (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ComplexToReal, FFT::DataType::FP16, 1, false, true))
				return 1;
		}
	}

	for (unsigned Ny = 4; Ny <= 1024; Ny *= 2)
	{
		for (unsigned Nx = 4; Nx <= 1024; Nx *= 2)
		{
			LOGI("Testing 2D C2C Image output (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::InverseComplexToComplex, FFT::DataType::FP32, 1, false, true))
				return 1;
			LOGI("Testing 2D C2C Image input (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ForwardComplexToComplex, FFT::DataType::FP32, 1, true, false))
				return 1;
			LOGI("Testing 2D C2C Image input + output (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ForwardComplexToComplex, FFT::DataType::FP32, 1, true, true))
				return 1;

			LOGI("Testing 2D C2C (FP16) Image output (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::InverseComplexToComplex, FFT::DataType::FP16, 1, false, true))
				return 1;
			LOGI("Testing 2D C2C (FP16) Image input (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ForwardComplexToComplex, FFT::DataType::FP16, 1, true, false))
				return 1;
			LOGI("Testing 2D C2C (FP16) Image input + output (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ForwardComplexToComplex, FFT::DataType::FP16, 1, true, true))
				return 1;

			LOGI("Testing 2D C2C Batched (Forward) (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::InverseComplexToComplex, FFT::DataType::FP32, 9, false, false))
				return 1;
			LOGI("Testing 2D C2C Batched (Inverse) (Nx = %u, Ny = %u).\n", Nx, Ny);
			if (!test_fft_2d(device, Nx, Ny, FFT::Mode::ForwardComplexToComplex, FFT::DataType::FP32, 14, false, false))
				return 1;
		}
	}
}