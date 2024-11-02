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

#include "fft.hpp"
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include "bitops.hpp"
#include <assert.h>

using namespace Vulkan;

namespace Granite
{
struct UBO
{
	uint32_t element_stride;
	uint32_t input_row_stride;
	uint32_t input_layer_stride;
	uint32_t output_row_stride;
	uint32_t output_layer_stride;
	uint32_t p;
};

struct TextureUBO
{
	float offset[2];
	float scale[2];
	int32_t storage_offset[2];
};

struct Iteration
{
	// Spec constants.
	uint32_t workgroup_size_x;
	uint32_t workgroup_size_y;
	uint32_t workgroup_size_z;
	float direction_word;
	uint32_t control_word;

	uint32_t dispatch_x;
	uint32_t dispatch_y;
	uint32_t dispatch_z;

	UBO ubo;

	ShaderProgramVariant *variant;
};

struct FFT::Impl
{
	bool plan(Device *device, const Options &options);
	void execute(CommandBuffer &cmd, const Resource &dst, const Resource &src);
	void execute_iteration(CommandBuffer &cmd, const Resource &dst, const Resource &src, size_t i);

	Device *device = nullptr;
	Options options;

	BufferHandle twiddle_buffer;
	BufferHandle tmp_buffer;
	BufferHandle output_tmp_buffer;
	std::vector<Iteration> iterations;

	void init_twiddle_buffer();
	void init_tmp_buffer();
	std::vector<unsigned> split_radices(unsigned N) const;
	void add_passes(const std::vector<unsigned> &splits, unsigned dim, unsigned &offset);
	void add_real_to_complex_pass(unsigned p, unsigned offset);
	void add_complex_to_real_pass(unsigned offset);
	bool workgroup_fits(unsigned wg_size_x_log2, unsigned wg_size_y_log2,
	                    unsigned split_first, unsigned split_second, unsigned split_third) const;
	void optimize_multi_fft(unsigned &multi_fft_x, unsigned &multi_fft_y,
	                        unsigned split_first, unsigned split_second, unsigned split_third,
	                        unsigned dim) const;
	void optimize_multi_fft_resolve(unsigned &multi_fft_x, unsigned &multi_fft_y) const;
	bool has_real_complex_resolve() const;
	const char *get_fp16_define() const;
};

static BufferHandle build_twiddle_buffer(Device &device, int dir, int N, FFT::DataType data_type)
{
	std::vector<vec2> values;
	std::vector<u16vec2> values_fp16;

	if (data_type == FFT::DataType::FP32)
	{
		values.reserve(N);
		values.emplace_back(0.0f);
	}
	else
	{
		values_fp16.reserve(N);
		values_fp16.emplace_back(0);
	}

	for (int n = 1; n < N; n *= 2)
	{
		for (int i = 0; i < n; i++)
		{
			double theta = muglm::pi<double>() * double(dir) * (double(i) / double(n));
			double c = muglm::cos(theta);
			double s = muglm::sin(theta);

			if (data_type == FFT::DataType::FP32)
				values.emplace_back(float(c), float(s));
			else
				values_fp16.push_back(floatToHalf(vec2(float(c), float(s))));
		}
	}

	const void *data;
	if (data_type == FFT::DataType::FP32)
		data = values.data();
	else
		data = values_fp16.data();

	BufferCreateInfo info = {};
	info.size = N * (data_type == FFT::DataType::FP32 ? sizeof(vec2) : sizeof(u16vec2));
	info.domain = BufferDomain::Device;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	auto buf = device.create_buffer(info, data);
	device.set_name(*buf, "twiddle-buffer");
	return buf;
}

static int mode_to_direction(FFT::Mode mode)
{
	if (mode == FFT::Mode::RealToComplex || mode == FFT::Mode::ForwardComplexToComplex)
		return -1;
	else
		return +1;
}

static unsigned compute_shared_elements(unsigned wg_size_x_log2, unsigned wg_size_y_log2, unsigned split)
{
	unsigned radix_stride = (1u << split) + 1u;
	return radix_stride * (1u << (wg_size_x_log2 + wg_size_y_log2));
}

bool FFT::Impl::workgroup_fits(unsigned wg_size_x_log2, unsigned wg_size_y_log2,
                               unsigned split_first, unsigned split_second, unsigned split_third) const
{
	// Never use more than half the resources, so we can run two workgroups in parallel.
	unsigned max_invocations_log2 =
			Util::floor_log2(device->get_gpu_properties().limits.maxComputeWorkGroupInvocations) - 1;
	// Minimum value here will be 64, which can fit the maximum 8x8x8 FFT.
	unsigned max_shared_size = device->get_gpu_properties().limits.maxComputeSharedMemorySize >> 1;
	// Minimum value here will be 8 KiB, which can fit 512 tap FFT.

	unsigned split = split_first + split_second + split_third;
	unsigned invocations_log2 = wg_size_x_log2 + wg_size_y_log2 + split_second + split_third;

	unsigned shared_size = compute_shared_elements(wg_size_x_log2, wg_size_y_log2, split);
	if (options.data_type == DataType::FP32)
		shared_size *= sizeof(vec2);
	else
		shared_size *= sizeof(u16vec2);

	if (shared_size > max_shared_size)
		return false;
	if (invocations_log2 > max_invocations_log2)
		return false;

	return true;
}

static const unsigned splits_table[9 + 1][3] = {
	{ 0, 0, 0 }, // Invalid
	{ 0, 0, 0 }, // Invalid
	{ 2, 0, 0 }, // Radix 4
	{ 3, 0, 0 }, // Radix 8
	{ 2, 2, 0 }, // Radix 16
	{ 3, 2, 0 }, // Radix 32
	{ 2, 2, 2 }, // Radix 64
	{ 3, 2, 2 }, // Radix 128
	{ 3, 3, 2 }, // Radix 256
	{ 3, 3, 3 }, // Radix 512
};

void FFT::Impl::optimize_multi_fft(unsigned &multi_fft_x, unsigned &multi_fft_y,
                                   unsigned split_first, unsigned split_second, unsigned split_third,
                                   unsigned dim) const
{
	// Attempt to increase the workgroup size.
	// First, we should bump the multi_fft_x to ensure coalesced buffer load/store.
	unsigned split = split_first + split_second + split_third;

	unsigned subgroup_size_log2 =
			Util::floor_log2(std::max(1u, device->get_device_features().vk11_props.subgroupSize));
	if (!subgroup_size_log2)
		subgroup_size_log2 = 5;

	// Load-store coalesce so that we read at least one cache line in one go.
	// RDNA is 128 bytes iirc, so target that.
	// Also optimize for shared banking, so workgroup X * Y should be big.
	unsigned ideal_size_x_log2 = 4;

	while (multi_fft_x < ideal_size_x_log2 || multi_fft_x + multi_fft_y < subgroup_size_log2)
	{
		unsigned x_shift = multi_fft_x + 1 + (dim == 0 ? split : 0);
		unsigned y_shift = multi_fft_y + 1 + (dim == 1 ? split : 0);

		bool x_dimension_fits_limits = workgroup_fits(multi_fft_x + 1, multi_fft_y, split_first, split_second, split_third);
		bool y_dimension_fits_limits = workgroup_fits(multi_fft_x, multi_fft_y + 1, split_first, split_second, split_third);
		bool x_dimension_is_aligned = (options.Nx & ((1u << x_shift) - 1u)) == 0;
		bool y_dimension_is_aligned = (options.Ny & ((1u << y_shift) - 1u)) == 0;

		if (x_dimension_fits_limits && x_dimension_is_aligned)
			multi_fft_x++;
		else if (y_dimension_fits_limits && y_dimension_is_aligned)
			multi_fft_y++;
		else
			break;
	}
}

void FFT::Impl::optimize_multi_fft_resolve(unsigned &multi_fft_x, unsigned &multi_fft_y) const
{
	unsigned subgroup_size_log2 =
			Util::floor_log2(std::max(1u, device->get_device_features().vk11_props.subgroupSize));
	if (!subgroup_size_log2)
		subgroup_size_log2 = 5;

	while (multi_fft_x + multi_fft_y < subgroup_size_log2)
	{
		unsigned x_shift = multi_fft_x + 1;
		unsigned y_shift = multi_fft_y + 1;

		bool x_dimension_is_aligned = (options.Nx & ((1u << x_shift) - 1u)) == 0;
		bool y_dimension_is_aligned = (options.Ny & ((1u << y_shift) - 1u)) == 0;

		if (x_dimension_is_aligned)
			multi_fft_x++;
		else if (y_dimension_is_aligned)
			multi_fft_y++;
		else
			break;
	}
}

const char *FFT::Impl::get_fp16_define() const
{
	if (device->get_device_features().vk12_features.shaderFloat16 &&
	    device->get_device_features().vk11_features.storageBuffer16BitAccess)
	{
		return "FFT_FULL_FP16";
	}
	else
		return "FFT_DATA_FP16";
}

void FFT::Impl::add_complex_to_real_pass(unsigned offset)
{
	auto &iter = iterations[offset];

	unsigned multi_fft_x = 0;
	unsigned multi_fft_y = 0;

	optimize_multi_fft_resolve(multi_fft_x, multi_fft_y);

	iter.workgroup_size_x = 1u << multi_fft_x;
	iter.workgroup_size_y = 1u << multi_fft_y;
	iter.workgroup_size_z = 1u;
	iter.direction_word = +1.0f;
	iter.control_word |= uint32_t(options.Ny > 1) << 17;
	iter.control_word |= uint32_t(options.Nz > 1) << 18;

	iter.ubo.element_stride = options.Nx; // Length in N complex numbers from 0 to Nyquist.
	iter.dispatch_x = options.Nx >> multi_fft_x;
	iter.dispatch_y = options.Ny >> multi_fft_y;
	iter.dispatch_z = options.Nz;

	iter.ubo.input_row_stride = options.Nx * 2;
	iter.ubo.input_layer_stride = options.Nx * options.Ny * 2;
	// TODO: Tight padding, but for now, just allocate assuming we preserve the full redundant spectrum.
	iter.ubo.output_row_stride = options.Nx;
	iter.ubo.output_layer_stride = options.Nx * options.Ny;

	auto *prog = device->get_shader_manager().register_compute("builtin://shaders/fft/fft_c2r.comp");

	std::vector<std::pair<std::string, int>> defines;
	if (options.data_type == DataType::FP16)
		defines.emplace_back(get_fp16_define(), 1);
	iter.variant = prog->register_variant(defines);

#if 0
	LOGI("Iteration C2R:\n");
	LOGI("  Workgroup (%u, %u, %u)\n", iter.workgroup_size_x, iter.workgroup_size_y, iter.workgroup_size_z);
	LOGI("  Dispatch (%u, %u, %u)\n", iter.dispatch_x, iter.dispatch_y, iter.dispatch_z);
#endif
}

void FFT::Impl::add_real_to_complex_pass(unsigned p, unsigned offset)
{
	auto &iter = iterations[offset];

	unsigned multi_fft_x = 0;
	unsigned multi_fft_y = 0;

	optimize_multi_fft_resolve(multi_fft_x, multi_fft_y);

	iter.workgroup_size_x = 1u << multi_fft_x;
	iter.workgroup_size_y = 1u << multi_fft_y;
	iter.workgroup_size_z = 1u;
	iter.direction_word = -1.0f;
	iter.control_word |= uint32_t(options.Ny > 1) << 17;
	iter.control_word |= uint32_t(options.Nz > 1) << 18;

	iter.ubo.element_stride = options.Nx; // Length in N complex numbers from 0 to Nyquist.
	iter.dispatch_x = options.Nx >> multi_fft_x;
	iter.dispatch_y = options.Ny >> multi_fft_y;
	iter.dispatch_z = options.Nz;

	iter.ubo.input_row_stride = options.Nx;
	iter.ubo.input_layer_stride = options.Nx * options.Ny;
	// TODO: Tight padding, but for now, just allocate assuming we preserve the full redundant spectrum.
	iter.ubo.output_row_stride = options.Nx * 2;
	iter.ubo.output_layer_stride = options.Nx * 2 * options.Ny;
	iter.ubo.p = p;

	auto *prog = device->get_shader_manager().register_compute("builtin://shaders/fft/fft_r2c.comp");

	std::vector<std::pair<std::string, int>> defines;
	if (options.data_type == DataType::FP16)
		defines.emplace_back(get_fp16_define(), 1);
	iter.variant = prog->register_variant(defines);

#if 0
	LOGI("Iteration R2C:\n");
	LOGI("  Workgroup (%u, %u, %u)\n", iter.workgroup_size_x, iter.workgroup_size_y, iter.workgroup_size_z);
	LOGI("  Dispatch (%u, %u, %u)\n", iter.dispatch_x, iter.dispatch_y, iter.dispatch_z);
#endif
}

void FFT::Impl::add_passes(const std::vector<unsigned> &split_iterations, unsigned dim, unsigned &offset)
{
	unsigned p = 1;

	if (dim == 0 && options.mode == Mode::ComplexToReal)
		add_complex_to_real_pass(offset++);

	for (auto split : split_iterations)
	{
		auto &iter = iterations[offset];
		iter = {};

		auto &splits = splits_table[split];

		unsigned multi_fft_x = 0;
		unsigned multi_fft_y = 0;

		optimize_multi_fft(multi_fft_x, multi_fft_y,
		                   splits[0], splits[1], splits[2],
		                   dim);

		iter.workgroup_size_x = 1u << multi_fft_x;
		iter.workgroup_size_y = 1u << multi_fft_y;
		iter.workgroup_size_z = 1u << (splits[1] + splits[2]);
		iter.direction_word = float(mode_to_direction(options.mode));
		iter.control_word |= splits[0] << 0;
		iter.control_word |= splits[1] << 4;
		iter.control_word |= splits[2] << 8;
		iter.control_word |= dim << 12;
		iter.control_word |= uint32_t(p == 1) << 16;
		iter.control_word |= uint32_t(options.Ny > 1) << 17;
		iter.control_word |= uint32_t(options.Nz > 1) << 18;
		iter.control_word |= uint32_t(offset == 0 &&
		                              options.input_resource == ResourceType::Texture &&
		                              options.mode == Mode::RealToComplex) << 19;
		iter.control_word |= uint32_t(offset + 1 == iterations.size() &&
		                              options.output_resource == ResourceType::Texture &&
		                              options.mode == Mode::ComplexToReal) << 20;

		if (dim == 0)
		{
			iter.ubo.element_stride = options.Nx >> split;
			iter.dispatch_x = options.Nx >> (multi_fft_x + split);
			iter.dispatch_y = options.Ny >> multi_fft_y;
			iter.dispatch_z = options.Nz;
		}
		else if (dim == 1)
		{
			iter.ubo.element_stride = options.Ny >> split;
			iter.dispatch_x = options.Nx >> multi_fft_x;
			iter.dispatch_y = options.Ny >> (multi_fft_y + split);
			iter.dispatch_z = options.Nz;
		}
		else
		{
			iter.ubo.element_stride = options.Nz >> split;
			iter.dispatch_x = options.Nx >> multi_fft_x;
			iter.dispatch_y = options.Ny >> multi_fft_y;
			iter.dispatch_z = options.Nz >> split;
		}

		// TODO: Tight padding, but for now, just allocate assuming we preserve the full redundant spectrum.
		bool real_complex_padding = dim >= 1 && has_real_complex_resolve();
		unsigned stride_scale = real_complex_padding ? 2 : 1;
		if (real_complex_padding)
			iter.dispatch_x += 1; // We only need to cover the Nyquist frequency, rest is redundant.

		iter.ubo.input_row_stride = options.Nx * stride_scale;
		iter.ubo.input_layer_stride = options.Nx * options.Ny * stride_scale;
		iter.ubo.output_row_stride = options.Nx * stride_scale;
		iter.ubo.output_layer_stride = options.Nx * options.Ny * stride_scale;
		iter.ubo.p = p;

#if 0
		LOGI("Iteration C2C:\n");
		LOGI("  Workgroup (%u, %u, %u)\n", iter.workgroup_size_x, iter.workgroup_size_y, iter.workgroup_size_z);
		LOGI("  Dispatch (%u, %u, %u)\n", iter.dispatch_x, iter.dispatch_y, iter.dispatch_z);
		LOGI("  Radix #1 = %u\n", 1u << splits[0]);
		LOGI("  Radix #2 = %u\n", 1u << splits[1]);
		LOGI("  Radix #3 = %u\n", 1u << splits[2]);
#endif

		auto *prog = device->get_shader_manager().register_compute("builtin://shaders/fft/fft.comp");

		std::vector<std::pair<std::string, int>> defines;
		if (offset == 0 && options.input_resource == ResourceType::Texture)
			defines.emplace_back("FFT_INPUT_TEXTURE", 1);
		if (offset + 1 == iterations.size() && options.output_resource == ResourceType::Texture)
			defines.emplace_back("FFT_OUTPUT_TEXTURE", 1);
		if (options.data_type == DataType::FP16)
			defines.emplace_back(get_fp16_define(), 1);

		iter.variant = prog->register_variant(defines);

		p <<= split;
		offset++;
	}

	if (dim == 0 && options.mode == Mode::RealToComplex)
		add_real_to_complex_pass(p, offset++);
}

std::vector<unsigned> FFT::Impl::split_radices(unsigned N) const
{
	if (N == 1)
		return {};

	std::vector<unsigned> splits;
	unsigned N_log2 = Util::trailing_zeroes(N);

	// This should be deduced based on limits.
	constexpr unsigned max_split = 9;

	while (N_log2 > max_split)
	{
		unsigned ideal_split = std::min(max_split, (N_log2 + 1) >> 1);
		assert(ideal_split >= 2);
		splits.push_back(ideal_split);
		N_log2 -= ideal_split;
	}

	splits.push_back(N_log2);
	return splits;
}

void FFT::Impl::init_twiddle_buffer()
{
	unsigned max_n = options.Nx * (has_real_complex_resolve() ? 2 : 1);
	if (options.dimensions >= 2 && options.Ny > max_n)
		max_n = options.Ny;
	if (options.dimensions >= 3 && options.Nz > max_n)
		max_n = options.Nz;

	int dir = mode_to_direction(options.mode);
	twiddle_buffer = build_twiddle_buffer(*device, dir, int(max_n), options.data_type);
}

void FFT::Impl::init_tmp_buffer()
{
	BufferCreateInfo info = {};
	info.domain = BufferDomain::Device;
	info.size = options.Nx * options.Ny * options.Nz;
	info.size *= options.data_type == DataType::FP32 ? sizeof(vec2) : sizeof(u16vec2);
	// TODO: Make it tightly packed, but need to consider workgroup size X when padding.
	// Not super important for our use cases ...
	if (has_real_complex_resolve())
		info.size *= 2;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	tmp_buffer = device->create_buffer(info);
	device->set_name(*tmp_buffer, "tmp-buffer");

	// We cannot use output buffer as a temporary buffer with Stockham autosort,
	// so need to ping-pong in two buffers.
	// ComplexToReal requires a larger temp buffer while transforming vertically.
	if (options.output_resource == ResourceType::Texture ||
	    (options.mode == Mode::ComplexToReal && options.dimensions > 1))
	{
		output_tmp_buffer = device->create_buffer(info);
		device->set_name(*output_tmp_buffer, "output-tmp-buffer");
	}
}

bool FFT::Impl::has_real_complex_resolve() const
{
	return options.mode == Mode::RealToComplex || options.mode == Mode::ComplexToReal;
}

bool FFT::Impl::plan(Device *device_, const Options &options_)
{
	device = device_;
	options = options_;

	std::vector<unsigned> splits[3];

	// 2D batch / 3D texture mode not supported.
	if (options.output_resource == ResourceType::Texture || options.input_resource == ResourceType::Texture)
	{
		if (options.Nz > 1)
			return false;
		if (has_real_complex_resolve() && options.dimensions < 2)
			return false;
	}

	// R2C and C2R is implemented as N/2 FFTs in X dimension with a resolve step to convert R2C/C2R into C2C.
	if (has_real_complex_resolve())
		options.Nx >>= 1;

	constexpr unsigned lowest_fft_radix = 4;

	if (!Util::is_pow2(options.Nx))
		return false;
	if (options.Nx < lowest_fft_radix)
		return false;
	splits[0] = split_radices(options.Nx);

	if (options.dimensions >= 2)
	{
		if (!Util::is_pow2(options.Ny))
			return false;
		if (options.Ny < lowest_fft_radix)
			return false;
		splits[1] = split_radices(options.Ny);
	}

	if (options.dimensions >= 3)
	{
		if (!Util::is_pow2(options.Nz))
			return false;
		if (options.Nz < lowest_fft_radix)
			return false;
		splits[2] = split_radices(options.Nz);
	}

	auto total_iterations = size_t(has_real_complex_resolve());
	for (auto &split : splits)
		total_iterations += split.size();
	iterations.resize(total_iterations);

	unsigned offset = 0;
	if (options.mode == Mode::ComplexToReal)
	{
		// For complex to real, we need to do 1D transforms last.
		// Just do the FFT with reversed dimensions.
		for (int i = 2; i >= 0; i--)
			add_passes(splits[i], i, offset);
	}
	else
	{
		for (int i = 0; i < 3; i++)
			add_passes(splits[i], i, offset);
	}

	init_twiddle_buffer();
	init_tmp_buffer();
	return true;
}

void FFT::Impl::execute_iteration(CommandBuffer &cmd, const Resource &dst, const Resource &src, size_t i)
{
	cmd.set_specialization_constant_mask(0x1f);

	auto &iter = iterations[i];

	cmd.set_program(iter.variant->get_program());
	cmd.set_specialization_constant(0, iter.workgroup_size_x);
	cmd.set_specialization_constant(1, iter.workgroup_size_y);
	cmd.set_specialization_constant(2, iter.workgroup_size_z);
	cmd.set_specialization_constant(3, iter.direction_word);
	cmd.set_specialization_constant(4, iter.control_word);

	bool dst_is_output = ((iterations.size() - i) & 1) == 1;
	auto ubo = iter.ubo;

	bool dst_is_texture = i + 1 == iterations.size() && options.output_resource == ResourceType::Texture;
	bool src_is_texture = i == 0 && options.input_resource == ResourceType::Texture;
	bool need_texture_ubo = dst_is_texture || src_is_texture;

	if (i == 0)
	{
		if (src_is_texture)
		{
			cmd.set_texture(0, 0, *src.image.view);
			if (src.image.sampler)
				cmd.set_sampler(0, 0, *src.image.sampler);
			else
				cmd.set_sampler(0, 0, src.image.stock_sampler);
		}
		else
		{
			cmd.set_storage_buffer(0, 0, *src.buffer.buffer, src.buffer.offset, src.buffer.size);
			ubo.input_row_stride = src.buffer.row_stride;
			ubo.input_layer_stride = src.buffer.layer_stride;
			if (options.mode == Mode::RealToComplex)
			{
				ubo.input_row_stride >>= 1;
				ubo.input_layer_stride >>= 1;
			}
		}
	}
	else if (dst_is_output)
		cmd.set_storage_buffer(0, 0, *tmp_buffer);
	else if (output_tmp_buffer)
		cmd.set_storage_buffer(0, 0, *output_tmp_buffer);
	else
		cmd.set_storage_buffer(0, 0, *dst.buffer.buffer, dst.buffer.offset, dst.buffer.size);

	if (dst_is_output)
	{
		if (dst_is_texture)
			cmd.set_storage_texture(0, 1, *dst.image.view);
		else if (i + 1 == iterations.size())
		{
			cmd.set_storage_buffer(0, 1, *dst.buffer.buffer, dst.buffer.offset, dst.buffer.size);
			ubo.output_row_stride = dst.buffer.row_stride;
			ubo.output_layer_stride = dst.buffer.layer_stride;
			if (options.mode == Mode::ComplexToReal)
			{
				ubo.output_row_stride >>= 1;
				ubo.output_layer_stride >>= 1;
			}
		}
		else if (output_tmp_buffer)
			cmd.set_storage_buffer(0, 1, *output_tmp_buffer);
		else
			cmd.set_storage_buffer(0, 1, *dst.buffer.buffer, dst.buffer.offset, dst.buffer.size);
	}
	else
		cmd.set_storage_buffer(0, 1, *tmp_buffer);

	cmd.set_storage_buffer(0, 2, *twiddle_buffer);
	memcpy(cmd.allocate_typed_constant_data<UBO>(0, 3, 1), &ubo, sizeof(ubo));
	if (need_texture_ubo)
	{
		TextureUBO texture_ubo = {};
		for (int j = 0; j < 2; j++)
		{
			texture_ubo.offset[j] = src.image.input_offset[j];
			texture_ubo.scale[j] = src.image.input_scale[j];
			texture_ubo.storage_offset[j] = dst.image.output_offset[j];
		}

		// We stride in terms of complex elements for R2C transforms. Simplifies shader logic a lot.
		if (options.mode == Mode::RealToComplex)
			texture_ubo.scale[0] *= 2.0f;

		memcpy(cmd.allocate_typed_constant_data<TextureUBO>(0, 4, 1), &texture_ubo, sizeof(texture_ubo));
	}
	cmd.dispatch(iter.dispatch_x, iter.dispatch_y, iter.dispatch_z);
	cmd.set_specialization_constant_mask(0);
}

void FFT::Impl::execute(CommandBuffer &cmd, const Resource &dst, const Resource &src)
{
	for (size_t i = 0, n = iterations.size(); i < n; i++)
	{
		execute_iteration(cmd, dst, src, i);

		if (i + 1 < n)
		{
			cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			            VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
			            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
		}
	}
}

FFT::FFT()
{
}

FFT::~FFT()
{
}

void FFT::release()
{
	impl.reset();
	impl->device = nullptr;
}

bool FFT::plan(Device *device, const Options &options)
{
	impl.reset(new Impl);
	return impl->plan(device, options);
}

void FFT::execute(CommandBuffer &cmd, const Resource &dst, const Resource &src)
{
	if (!impl)
		return;
	impl->execute(cmd, dst, src);
}

void FFT::execute_iteration(CommandBuffer &cmd, const Resource &dst, const Resource &src, unsigned iteration)
{
	if (!impl)
		return;
	return impl->execute_iteration(cmd, dst, src, iteration);
}

unsigned FFT::get_num_iterations() const
{
	if (!impl)
		return 0;
	return unsigned(impl->iterations.size());
}
}
