/* Copyright (C) 2015-2019 Hans-Kristian Arntzen <maister@archlinux.us>
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
#include "glfft.hpp"
#include "glfft_cli.hpp"
#include "glfft_common.hpp"
#include <cmath>
#include <complex>
#include <functional>
#include <random>
#include <stdexcept>
#include <stdlib.h>

using namespace std;
using namespace GLFFT;
using namespace GLFFT::Internal;

static normal_distribution<float> normal_dist{ 0.0f, 1.0f };
static default_random_engine engine;

struct mufft_deleter
{
	void operator()(void *ptr) const
	{
		mufft_free(ptr);
	}
};
using mufft_buffer = unique_ptr<void, mufft_deleter>;

mufft_buffer alloc(size_t size)
{
	void *ptr = mufft_alloc(size);
	if (ptr == nullptr)
	{
		throw bad_alloc();
	}
	return mufft_buffer(ptr);
}

using cfloat = complex<float>;

mufft_buffer create_input(unsigned N)
{
	auto buffer = alloc(N * sizeof(float));
	float *ptr = static_cast<float *>(buffer.get());

	for (unsigned i = 0; i < N; i++)
	{
		ptr[i] = normal_dist(engine);
	}

	return buffer;
}

static inline size_t type_to_input_size(Type type)
{
	switch (type)
	{
	case ComplexToComplex:
	case ComplexToReal:
		return sizeof(cfloat);

	case ComplexToComplexDual:
		return 2 * sizeof(cfloat);

	case RealToComplex:
		return sizeof(float);

	default:
		return 0;
	}
}

static inline size_t type_to_output_size(Type type)
{
	switch (type)
	{
	case ComplexToComplex:
	case ComplexToReal:
	case RealToComplex:
		return sizeof(cfloat);

	case ComplexToComplexDual:
		return 2 * sizeof(cfloat);

	default:
		return 0;
	}
}

static mufft_buffer create_reference(Type type, Direction direction, unsigned Nx, unsigned Ny, const void *buffer,
                                     size_t output_size)
{
	auto output = alloc(output_size);

	mufft_buffer input_interleaved;
	mufft_buffer output_interleaved;
	mufft_buffer input_convolved;
	auto out = static_cast<cfloat *>(output.get());
	auto in = static_cast<const cfloat *>(buffer);

	if (direction == InverseConvolve)
	{
		input_convolved = alloc(output_size);
		auto in_conv = static_cast<cfloat *>(input_convolved.get());

		direction = Inverse;
		for (unsigned i = 0; i < output_size / sizeof(cfloat); i++)
		{
			in_conv[i] = in[i] * in[i];
		}

		in = in_conv;
	}

	// muFFT doesn't support this type, so interleave manually, and do two separate FFTs.
	if (type == ComplexToComplexDual)
	{
		input_interleaved = alloc(output_size);
		output_interleaved = alloc(output_size);

		auto inter_in = static_cast<cfloat *>(input_interleaved.get());
		auto inter_out = static_cast<cfloat *>(output_interleaved.get());

		for (unsigned i = 0; i < Nx * Ny; i++)
		{
			inter_in[i] = in[2 * i + 0];
			inter_in[i + Nx * Ny] = in[2 * i + 1];
		}

		in = inter_in;
		out = inter_out;
	}

	if (Ny > 1)
	{
		mufft_plan_2d *plan = nullptr;
		switch (type)
		{
		case ComplexToComplex:
			plan = mufft_create_plan_2d_c2c(Nx, Ny, direction, 0);
			mufft_execute_plan_2d(plan, out, in);
			break;

		case ComplexToComplexDual:
			plan = mufft_create_plan_2d_c2c(Nx, Ny, direction, 0);
			mufft_execute_plan_2d(plan, out, in);
			mufft_execute_plan_2d(plan, out + Nx * Ny, in + Nx * Ny);
			break;

		case ComplexToReal:
			plan = mufft_create_plan_2d_c2r(Nx, Ny, 0);
			mufft_execute_plan_2d(plan, out, in);
			break;

		case RealToComplex:
			plan = mufft_create_plan_2d_r2c(Nx, Ny, 0);
			mufft_execute_plan_2d(plan, out, in);
			break;

		default:
			throw logic_error("Invalid type");
		}

		if (plan == nullptr)
		{
			throw bad_alloc();
		}

		mufft_free_plan_2d(plan);
	}
	else
	{
		mufft_plan_1d *plan = nullptr;
		switch (type)
		{
		case ComplexToComplex:
			plan = mufft_create_plan_1d_c2c(Nx, direction, 0);
			mufft_execute_plan_1d(plan, out, in);
			break;

		case ComplexToComplexDual:
			plan = mufft_create_plan_1d_c2c(Nx, direction, 0);
			mufft_execute_plan_1d(plan, out, in);
			mufft_execute_plan_1d(plan, out + Nx, in + Nx);
			break;

		case ComplexToReal:
			plan = mufft_create_plan_1d_c2r(Nx, 0);
			mufft_execute_plan_1d(plan, out, in);
			break;

		case RealToComplex:
			plan = mufft_create_plan_1d_r2c(Nx, 0);
			mufft_execute_plan_1d(plan, out, in);
			break;

		default:
			throw logic_error("Invalid type");
		}

		if (plan == nullptr)
		{
			throw bad_alloc();
		}

		mufft_free_plan_1d(plan);
	}

	// muFFT doesn't support this type, so interleave manually, and do two separate FFTs.
	if (type == ComplexToComplexDual)
	{
		for (unsigned i = 0; i < Nx * Ny; i++)
		{
			static_cast<cfloat *>(output.get())[2 * i + 0] = out[i];
			static_cast<cfloat *>(output.get())[2 * i + 1] = out[i + Nx * Ny];
		}
	}

	// Normalize manually.
	out = static_cast<cfloat *>(output.get());
	for (unsigned i = 0; i < output_size / sizeof(cfloat); i++)
	{
		out[i] /= Nx * Ny;
	}

	return output;
}

static mufft_buffer readback(Context *context, Buffer *buffer, size_t size)
{
	auto buf = alloc(size);

	const void *ptr = context->map(buffer, 0, size);
	if (ptr == nullptr)
	{
		throw bad_alloc();
	}

	memcpy(buf.get(), ptr, size);
	context->unmap(buffer);
	return buf;
}

static bool validate_surface(Context *context, const float *a, const float *b, unsigned Nx, unsigned Ny,
                             unsigned stride, float epsilon, float min_snr)
{
	float max_diff = 0.0f;
	double signal = 0.0;
	double noise = 0.0;
	bool valid = true;

	for (unsigned y = 0; y < Ny; y++, a += stride, b += stride)
	{
		for (unsigned x = 0; x < Nx; x++)
		{
			float diff = fabs(a[x] - b[x]);
			if (!(diff < epsilon))
			{
				valid = false;
			}

			max_diff = max(diff, max_diff);

			signal += b[x] * b[x];
			noise += diff * diff;
		}
	}

	double snr = 10.0 * log10(signal / noise);
	if (snr < min_snr)
	{
		context->log("Too low SNR: %8.3f dB\n", snr);
		valid = false;
	}
	context->log("\tMax diff: %10.6g (reference: %10.6g), SNR: %8.3f dB (reference: %8.3f)\n", max_diff, epsilon, snr,
	             min_snr);

	if (!valid)
	{
		context->log("Surface is not valid!\n");
	}
	return valid;
}

static void validate(Context *context, Type type, const float *a, const float *b, unsigned Nx, unsigned Ny,
                     float epsilon, float min_snr)
{
	unsigned stride = 0;
	unsigned x = 0;
	unsigned y = 0;

	switch (type)
	{
	case ComplexToComplex:
		x = Nx * 2;
		y = Ny;
		stride = x;
		break;

	case ComplexToComplexDual:
		x = Nx * 4;
		y = Ny;
		stride = x;
		break;

	case RealToComplex:
		x = Nx + 2;
		y = Ny;
		stride = Nx * 2;
		break;

	case ComplexToReal:
		x = Nx;
		y = Ny;
		stride = x;
		break;

	default:
		throw logic_error("Invalid type");
	}

	if (!validate_surface(context, a, b, x, y, stride, epsilon, min_snr))
	{
		throw logic_error("Failed to validate surface.");
	}
}

static const char *direction_to_str(Direction direction)
{
	switch (direction)
	{
	case Forward:
		return "forward";
	case Inverse:
		return "inverse";
	case InverseConvolve:
		return "inverse convolve";
	default:
		return "?";
	}
}

static const char *type_to_str(Type type)
{
	switch (type)
	{
	case ComplexToComplex:
		return "C2C";
	case ComplexToComplexDual:
		return "C2C dual";
	case RealToComplex:
		return "R2C";
	case ComplexToReal:
		return "C2R";
	default:
		return "?";
	}
}

// Based on GLM implementation.
static uint16_t fp32_to_fp16(float v_)
{
	union {
		float f;
		uint32_t u;
	} u;

	u.f = v_;
	uint32_t v = u.u;

	int s = (v >> 16) & 0x00008000;
	int e = ((v >> 23) & 0x000000ff) - (127 - 15);
	int m = v & 0x007fffff;

	if (e <= 0)
	{
		if (e < -10)
		{
			return s;
		}

		m = (m | 0x00800000) >> (1 - e);
		if (m & 0x00001000)
		{
			m += 0x00002000;
		}

		return s | (m >> 13);
	}
	else if (e == 0xff - (127 - 15))
	{
		if (m == 0)
		{
			return s | 0x7c00;
		}
		else
		{
			m >>= 13;
			return s | 0x7c00 | m | (m == 0);
		}
	}
	else
	{
		if (m & 0x00001000)
		{
			m += 0x00002000;
			if (m & 0x00800000)
			{
				m = 0;
				e += 1;
			}
		}

		if (e > 30)
		{
			return s | 0x7c00;
		}

		return s | (e << 10) | (m >> 13);
	}
}

static inline float fp16_to_fp32(uint16_t v)
{
	float sign = v & 0x8000 ? -1.0f : 1.0f;
	int m = v & 0x3ff;
	int e = (v >> 10) & 0x1f;

	// Straight out of GLES spec.
	if (e == 0 && m == 0)
	{
		return sign * 0.0f;
	}
	else if (e == 0 && m != 0)
	{
		return sign * exp2(-14.0f) * (m / 1024.0f);
	}
	else if (e > 0 && e < 31)
	{
		return sign * exp2(e - 15.0f) * (1.0f + m / 1024.0f);
	}
	else if (e == 31)
	{
		return sign * numeric_limits<float>::infinity();
	}
	else
	{
		return numeric_limits<float>::quiet_NaN();
	}
}

static mufft_buffer convert_fp32_fp16(const float *input, unsigned N)
{
	auto buffer = alloc(N * sizeof(uint16_t));
	auto ptr = static_cast<uint16_t *>(buffer.get());

	for (unsigned i = 0; i < N; i++)
		ptr[i] = fp32_to_fp16(input[i]);

	return buffer;
}

static mufft_buffer convert_fp16_fp32(const uint16_t *input, unsigned N)
{
	auto buffer = alloc(N * sizeof(float));
	auto ptr = static_cast<float *>(buffer.get());

	for (unsigned i = 0; i < N; i++)
	{
		auto v = fp16_to_fp32(input[i]);
		ptr[i] = v;
	}

	return buffer;
}

static void run_test_ssbo(Context *context, const TestSuiteArguments &args, unsigned Nx, unsigned Ny, Type type,
                          Direction direction, const FFTOptions &options, const shared_ptr<ProgramCache> &cache)
{
	context->log("Running SSBO -> SSBO FFT, %04u x %04u\n\t%7s transform\n\t%8s\n\tbanked shared %s\n\tvector size "
	             "%u\n\twork group (%u, %u)\n\tinput fp16 %s\n\toutput fp16 %s ...\n",
	             Nx, Ny, direction_to_str(direction), type_to_str(type),
	             options.performance.shared_banked ? "yes" : "no", options.performance.vector_size,
	             options.performance.workgroup_size_x, options.performance.workgroup_size_y,
	             options.type.input_fp16 ? "yes" : "no", options.type.output_fp16 ? "yes" : "no");

	unique_ptr<Buffer> test_input;
	unique_ptr<Buffer> test_output;

	size_t input_size = Nx * Ny * type_to_input_size(type);
	size_t output_size = Nx * Ny * type_to_output_size(type);

	auto input = create_input(input_size / sizeof(float));
	auto output = create_reference(type, direction, Nx, Ny, input.get(), output_size);

	if (options.type.input_fp16)
	{
		input = convert_fp32_fp16(static_cast<const float *>(input.get()), input_size / sizeof(float));
	}

	test_input = context->create_buffer(input.get(), input_size >> unsigned(options.type.input_fp16), AccessStreamCopy);
	test_output = context->create_buffer(nullptr, output_size >> unsigned(options.type.output_fp16), AccessStreamRead);

	FFT fft(context, Nx, Ny, type, direction, SSBO, SSBO, cache, options);

	auto *cmd = context->request_command_buffer();
	fft.process(cmd, test_output.get(), test_input.get(), test_input.get());
	cmd->barrier();
	context->submit_command_buffer(cmd);
	context->wait_idle();

	auto output_data = readback(context, test_output.get(), output_size >> unsigned(options.type.output_fp16));
	if (options.type.output_fp16)
	{
		output_data = convert_fp16_fp32(static_cast<const uint16_t *>(output_data.get()), output_size / sizeof(float));
	}

	float epsilon = options.type.output_fp16 || options.type.input_fp16 ? args.epsilon_fp16 : args.epsilon_fp32;
	float min_snr = options.type.output_fp16 || options.type.input_fp16 ? args.min_snr_fp16 : args.min_snr_fp32;
	if (direction == InverseConvolve)
	{
		epsilon *= 1.5f;
	}
	validate(context, type, static_cast<const float *>(output_data.get()), static_cast<const float *>(output.get()), Nx,
	         Ny, epsilon, min_snr);

	context->log("... Success!\n");
}

static void run_test_texture(Context *context, const TestSuiteArguments &args, unsigned Nx, unsigned Ny, Type type,
                             Direction direction, const FFTOptions &options, const shared_ptr<ProgramCache> &cache)
{
	context->log("Running Texture -> SSBO FFT, %04u x %04u\n\t%7s transform\n\t%8s\n\tbanked shared %s\n\tvector size "
	             "%u\n\twork group (%u, %u)\n\tinput fp16 %s\n\toutput fp16 %s ...\n",
	             Nx, Ny, direction_to_str(direction), type_to_str(type),
	             options.performance.shared_banked ? "yes" : "no", options.performance.vector_size,
	             options.performance.workgroup_size_x, options.performance.workgroup_size_y,
	             options.type.input_fp16 ? "yes" : "no", options.type.output_fp16 ? "yes" : "no");

	unique_ptr<Texture> test_input;
	unique_ptr<Buffer> test_output;

	size_t input_size = Nx * Ny * type_to_input_size(type);
	size_t output_size = Nx * Ny * type_to_output_size(type);

	auto input = create_input(input_size / sizeof(float));
	auto output = create_reference(type, direction, Nx, Ny, input.get(), output_size);

	Format format = FormatUnknown;

	switch (type)
	{
	case ComplexToComplexDual:
		format = FormatR32G32B32A32Float;
		break;

	case ComplexToComplex:
	case ComplexToReal:
		format = FormatR32G32Float;
		break;

	case RealToComplex:
		format = FormatR32Float;
		break;
	}

	test_input = context->create_texture(input.get(), Nx, Ny, format);

	test_output = context->create_buffer(nullptr, output_size >> unsigned(options.type.output_fp16), AccessStreamRead);

	FFT fft(context, Nx, Ny, type, direction, type == RealToComplex ? ImageReal : Image, SSBO, cache, options);
	fft.set_texture_offset_scale(0.5f / Nx, 0.5f / Ny, 1.0f / Nx, 1.0f / Ny);

	auto *cmd = context->request_command_buffer();
	fft.process(cmd, test_output.get(), test_input.get(), test_input.get());
	cmd->barrier();
	context->submit_command_buffer(cmd);
	context->wait_idle();

	auto output_data = readback(context, test_output.get(), output_size >> unsigned(options.type.output_fp16));
	if (options.type.output_fp16)
	{
		output_data = convert_fp16_fp32(static_cast<const uint16_t *>(output_data.get()), output_size / sizeof(float));
	}

	float epsilon = options.type.output_fp16 || options.type.input_fp16 ? args.epsilon_fp16 : args.epsilon_fp32;
	float min_snr = options.type.output_fp16 || options.type.input_fp16 ? args.min_snr_fp16 : args.min_snr_fp32;
	if (direction == InverseConvolve)
	{
		epsilon *= 1.5f;
	}
	validate(context, type, static_cast<const float *>(output_data.get()), static_cast<const float *>(output.get()), Nx,
	         Ny, epsilon, min_snr);

	context->log("... Success!\n");
}

static mufft_buffer readback_texture(Context *context, Texture *tex, unsigned components, unsigned Nx, unsigned Ny)
{
	unsigned count = Nx * Ny * components;
	auto fp16_buffer = alloc(count * sizeof(uint16_t));

	context->read_texture(fp16_buffer.get(), tex);
	return convert_fp16_fp32(static_cast<uint16_t *>(fp16_buffer.get()), count);
}

static void run_test_image(Context *context, const TestSuiteArguments &args, unsigned Nx, unsigned Ny, Type type,
                           Direction direction, const FFTOptions &options, const shared_ptr<ProgramCache> &cache)
{
	context->log("Running SSBO -> Image FFT, %04u x %04u\n\t%7s transform\n\t%8s\n\tbanked shared %s\n\tvector size "
	             "%u\n\twork group (%u, %u)\n\tinput fp16 %s\n\toutput fp16 %s ...\n",
	             Nx, Ny, direction_to_str(direction), type_to_str(type),
	             options.performance.shared_banked ? "yes" : "no", options.performance.vector_size,
	             options.performance.workgroup_size_x, options.performance.workgroup_size_y,
	             options.type.input_fp16 ? "yes" : "no", options.type.output_fp16 ? "yes" : "no");

	unique_ptr<Buffer> test_input;

	size_t input_size = Nx * Ny * type_to_input_size(type);
	size_t output_size = Nx * Ny * type_to_output_size(type);

	auto input = create_input(input_size / sizeof(float));
	auto output = create_reference(type, direction, Nx, Ny, input.get(), output_size);

	if (options.type.input_fp16)
	{
		input = convert_fp32_fp16(static_cast<const float *>(input.get()), input_size / sizeof(float));
	}

	test_input = context->create_buffer(input.get(), input_size >> unsigned(options.type.input_fp16), AccessStreamCopy);

	Format format = FormatUnknown;
	unsigned components = 0;
	switch (type)
	{
	case ComplexToComplexDual:
		format = FormatR16G16B16A16Float;
		components = 4;
		break;

	case ComplexToComplex:
	case RealToComplex:
		format = FormatR16G16Float;
		components = 2;
		break;

	case ComplexToReal:
		format = FormatR16Float;
		components = 1;
		break;
	}

	// Upload a blank buffer to make debugging easier.
	vector<float> blank(Nx * Ny * components * sizeof(float));
	unique_ptr<Texture> tex = context->create_texture(blank.data(), Nx, Ny, format);

	FFT fft(context, Nx, Ny, type, direction, SSBO, type != ComplexToReal ? Image : ImageReal, cache, options);

	auto *cmd = context->request_command_buffer();
	fft.process(cmd, tex.get(), test_input.get(), test_input.get());
	cmd->barrier();
	context->submit_command_buffer(cmd);
	context->wait_idle();

	auto output_data = readback_texture(context, tex.get(), components, Nx, Ny);

	float epsilon = args.epsilon_fp16;
	float min_snr = args.min_snr_fp16;
	if (direction == InverseConvolve)
	{
		epsilon *= 1.5f;
	}

	validate(context, type, static_cast<const float *>(output_data.get()), static_cast<const float *>(output.get()), Nx,
	         Ny, epsilon, min_snr);

	context->log("... Success!\n");
}

static void enqueue_test(Context *context, vector<function<void()>> &tests, const TestSuiteArguments &args, unsigned Nx,
                         unsigned Ny, Type type, Direction direction, Target input_target, Target output_target,
                         const FFTOptions &options, const shared_ptr<ProgramCache> &cache)
{
	if (input_target == SSBO && output_target == SSBO)
	{
		tests.push_back([=] { run_test_ssbo(context, args, Nx, Ny, type, direction, options, cache); });
	}
	else if (input_target == SSBO && output_target == Image)
	{
		if (context->supports_texture_readback())
		{
			tests.push_back([=] { run_test_image(context, args, Nx, Ny, type, direction, options, cache); });
		}
		else
		{
			throw logic_error("run_test_image() not supported on interface.");
		}
	}
	else if (input_target == Image && output_target == SSBO)
	{
		tests.push_back([=] { run_test_texture(context, args, Nx, Ny, type, direction, options, cache); });
	}
	else
	{
		throw logic_error("Invalid target type.");
	}
}

static void test_fp32_fp16_convert()
{
	auto input = create_input(256);
	auto fp16_input = convert_fp32_fp16(static_cast<const float *>(input.get()), 256);
	auto output = convert_fp16_fp32(static_cast<const uint16_t *>(fp16_input.get()), 256);

	for (unsigned i = 0; i < 256; i++)
	{
		float fp32 = static_cast<const float *>(input.get())[i];
		float fp16 = static_cast<const float *>(output.get())[i];
		float diff = fabs(fp16 - fp32);
		if (diff > 0.001f)
		{
			throw logic_error("Failed to validate FP32 -> FP16 -> FP32 roundtrip conversion.");
		}
	}
}

void GLFFT::Internal::run_test_suite(Context *context, const TestSuiteArguments &args)
{
	// Sanity test, should never fail.
	test_fp32_fp16_convert();

	FFTOptions options;
	options.type.normalize = true;

	vector<function<void()>> tests;
	auto cache = make_shared<ProgramCache>();

	// Very exhaustive. Lots of overlap in tests which could be avoided to speed up the tests.
	for (unsigned i = 0; i < 64; i++)
	{
		options.type.input_fp16 = i & 1;
		options.type.output_fp16 = i & 2;
		options.type.fp16 = (i & 3) == 3;

		options.performance.shared_banked = i & 4;
		options.performance.vector_size = 0;
		unsigned N_mult = 1;
		switch (i & (8 | 16))
		{
		case 8:
			options.performance.vector_size = 2;
			N_mult = 1;
			break;

		case 16:
			options.performance.vector_size = 4;
			N_mult = 2;
			break;

		case 24:
			options.performance.vector_size = 8;
			N_mult = 4;
			break;
		}

		if (options.performance.vector_size == 0)
		{
			continue;
		}

		// Pointless to test for now ...
		if (options.performance.vector_size == 8)
		{
			continue;
		}

		bool big_workgroup = i & 32;
		options.performance.workgroup_size_x = big_workgroup ? 8 : 4;
		options.performance.workgroup_size_y = big_workgroup ? 4 : 1;

		for (unsigned N = N_mult * (big_workgroup ? 128 : 32); N <= 1024; N <<= 1)
		{
			// Texture -> SSBO
			enqueue_test(context, tests, args, N, N / 2, ComplexToComplex, Forward, Image, SSBO, options, cache);
			enqueue_test(context, tests, args, N, N / 2, ComplexToComplex, Inverse, Image, SSBO, options, cache);
			enqueue_test(context, tests, args, N, N / 2, ComplexToComplex, InverseConvolve, Image, SSBO, options,
			             cache);

			enqueue_test(context, tests, args, 2 * N, N, ComplexToReal, Inverse, Image, SSBO, options, cache);
			enqueue_test(context, tests, args, 2 * N, N, ComplexToReal, InverseConvolve, Image, SSBO, options, cache);
			enqueue_test(context, tests, args, 4 * N, N, RealToComplex, Forward, Image, SSBO, options, cache);

			if (options.performance.vector_size >= 4)
			{
				enqueue_test(context, tests, args, N, N, ComplexToComplexDual, Forward, Image, SSBO, options, cache);
				enqueue_test(context, tests, args, N, N, ComplexToComplexDual, Inverse, Image, SSBO, options, cache);
				enqueue_test(context, tests, args, N, N, ComplexToComplexDual, InverseConvolve, Image, SSBO, options,
				             cache);
			}

			if (!big_workgroup)
			{
				enqueue_test(context, tests, args, N, 1, ComplexToComplex, Forward, Image, SSBO, options, cache);
				enqueue_test(context, tests, args, N, 1, ComplexToComplex, Inverse, Image, SSBO, options, cache);
				enqueue_test(context, tests, args, N, 1, ComplexToComplex, InverseConvolve, Image, SSBO, options,
				             cache);
			}

			// SSBO -> SSBO
			enqueue_test(context, tests, args, N, N / 2, ComplexToComplex, Forward, SSBO, SSBO, options, cache);
			enqueue_test(context, tests, args, 2 * N, N, RealToComplex, Forward, SSBO, SSBO, options, cache);
			enqueue_test(context, tests, args, N, N / 2, ComplexToComplex, Inverse, SSBO, SSBO, options, cache);
			enqueue_test(context, tests, args, 4 * N, N, ComplexToReal, Inverse, SSBO, SSBO, options, cache);
			enqueue_test(context, tests, args, N, N, ComplexToComplex, InverseConvolve, SSBO, SSBO, options, cache);
			enqueue_test(context, tests, args, 2 * N, N, ComplexToReal, InverseConvolve, SSBO, SSBO, options, cache);

			if (options.performance.vector_size >= 4)
			{
				enqueue_test(context, tests, args, N, N, ComplexToComplexDual, Forward, SSBO, SSBO, options, cache);
				enqueue_test(context, tests, args, N, N, ComplexToComplexDual, Inverse, SSBO, SSBO, options, cache);
				enqueue_test(context, tests, args, N, N, ComplexToComplexDual, InverseConvolve, SSBO, SSBO, options,
				             cache);
			}

			if (!big_workgroup)
			{
				enqueue_test(context, tests, args, N, 1, ComplexToComplex, Forward, SSBO, SSBO, options, cache);
				enqueue_test(context, tests, args, 4 * N, 1, RealToComplex, Forward, SSBO, SSBO, options, cache);
				enqueue_test(context, tests, args, N, 1, ComplexToComplex, Inverse, SSBO, SSBO, options, cache);
				enqueue_test(context, tests, args, 2 * N, 1, ComplexToReal, Inverse, SSBO, SSBO, options, cache);
				enqueue_test(context, tests, args, N, 1, ComplexToComplex, InverseConvolve, SSBO, SSBO, options, cache);
				enqueue_test(context, tests, args, 2 * N, 1, ComplexToReal, InverseConvolve, SSBO, SSBO, options,
				             cache);

				if (options.performance.vector_size >= 4)
				{
					enqueue_test(context, tests, args, N, 1, ComplexToComplexDual, Forward, SSBO, SSBO, options, cache);
					enqueue_test(context, tests, args, 2 * N, 1, ComplexToComplexDual, Inverse, SSBO, SSBO, options,
					             cache);
					enqueue_test(context, tests, args, N, 1, ComplexToComplexDual, InverseConvolve, SSBO, SSBO, options,
					             cache);
				}
			}

			// SSBO -> Image
			if (context->supports_texture_readback())
			{
				if (N == 1024)
				{
					enqueue_test(context, tests, args, N, N / 2, ComplexToComplex, Forward, SSBO, Image, options,
					             cache);
					enqueue_test(context, tests, args, N, N / 2, ComplexToComplexDual, Forward, SSBO, Image, options,
					             cache);
					enqueue_test(context, tests, args, 2 * N, N, RealToComplex, Forward, SSBO, Image, options, cache);
					enqueue_test(context, tests, args, N, N / 2, ComplexToComplex, Inverse, SSBO, Image, options,
					             cache);
					enqueue_test(context, tests, args, N, N, ComplexToComplexDual, Inverse, SSBO, Image, options,
					             cache);
					enqueue_test(context, tests, args, 2 * N, N, ComplexToReal, Inverse, SSBO, Image, options, cache);
					enqueue_test(context, tests, args, N, N, ComplexToComplex, InverseConvolve, SSBO, Image, options,
					             cache);
					enqueue_test(context, tests, args, N, N, ComplexToComplexDual, InverseConvolve, SSBO, Image,
					             options, cache);
					enqueue_test(context, tests, args, 2 * N, N, ComplexToReal, InverseConvolve, SSBO, Image, options,
					             cache);

					if (!big_workgroup)
					{
						enqueue_test(context, tests, args, N, 1, ComplexToComplex, Forward, SSBO, Image, options,
						             cache);
						enqueue_test(context, tests, args, N, 1, ComplexToComplexDual, Forward, SSBO, Image, options,
						             cache);
						enqueue_test(context, tests, args, N, 1, ComplexToReal, Inverse, SSBO, Image, options, cache);
						enqueue_test(context, tests, args, N, 1, RealToComplex, Forward, SSBO, Image, options, cache);
					}
				}
			}
		}
	}

	context->log("Enqueued %u tests!\n", unsigned(tests.size()));

	unsigned successful_tests = 0;
	vector<unsigned> failed_tests;

	if (!args.exhaustive)
	{
		for (unsigned i = args.test_id_min; i <= args.test_id_max; i++)
		{
			// Throws on range error.
			try
			{
				tests.at(i)();
				successful_tests++;
			}
			catch (...)
			{
				failed_tests.push_back(i);
				if (args.throw_on_fail)
				{
					throw;
				}
			}
		}
	}
	else
	{
		unsigned index = 0;
		for (auto &test : tests)
		{
			try
			{
				context->log("Running test #%u!\n", index);
				test();
				successful_tests++;
			}
			catch (const std::exception &e)
			{
				context->log("Failed test #%u (%s)!\n", index, e.what());
				if (args.throw_on_fail)
					throw;
				failed_tests.push_back(index);
			}
			catch (...)
			{
				context->log("Failed test #%u!\n", index);
				if (args.throw_on_fail)
					throw;
				failed_tests.push_back(index);
			}
			index++;
		}
	}

	if (args.throw_on_fail)
	{
		context->log("Successfully ran tests!\n");
	}
	else
	{
		context->log("%u successful tests.\n", successful_tests);
		context->log("Failed tests: ===\n");
		for (auto failed : failed_tests)
		{
			context->log("    %u\n", failed);
		}
		context->log("=================\n");
	}

	context->log("%u entries in shader cache!\n", unsigned(cache->cache_size()));
}
