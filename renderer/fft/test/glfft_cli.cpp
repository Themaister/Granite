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

#include "glfft_cli.hpp"
#include "glfft.hpp"
#include "cli_parser.hpp"
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace GLFFT;
using namespace GLFFT::Internal;
using namespace Util;
using namespace std;

struct BenchArguments
{
	unsigned width = 0;
	unsigned height = 0;
	unsigned warmup = 2;
	unsigned iterations = 20;
	unsigned dispatches = 50;
	unsigned timeout = 1.0;
	Type type = ComplexToComplex;
	unsigned size_for_type = 2;
	const char *string_for_type = "C2C";
	bool fp16 = false;
	bool input_texture = false;
	bool output_texture = false;
};

// Rough estimate based on a canonical FFT implementation.
static double get_estimated_flops(unsigned width, unsigned height, Type type)
{
	double flops = width * height * (log2(float(width)) + log2(float(height))) * 5.0;

	switch (type)
	{
	case ComplexToComplexDual:
		flops *= 2.0;
		break;

	case RealToComplex:
	case ComplexToReal:
		flops *= 0.5;
		break;

	default:
		break;
	}

	return flops;
}

static double get_estimated_bw_per_pass(unsigned width, unsigned height, Type type, bool fp16)
{
	double bw = width * height * 4.0 * sizeof(float); // BW for reading the buffer and writing it back.

	switch (type)
	{
	case ComplexToComplexDual:
		bw *= 2.0;
		break;

	case RealToComplex:
	case ComplexToReal:
		bw *= 0.5;
		break;

	default:
		break;
	}

	if (fp16)
	{
		bw *= 0.5;
	}

	return bw;
}

static void run_benchmark(Context *context, const BenchArguments &args)
{
	auto cache = make_shared<ProgramCache>();

	FFTOptions options;
	options.type.input_fp16 = args.fp16;
	options.type.output_fp16 = args.fp16;
	options.type.fp16 = args.fp16;

	unique_ptr<Resource> output;
	unique_ptr<Resource> input;

	Target input_target = SSBO;
	Target output_target = SSBO;

	size_t buffer_size = sizeof(float) * (args.fp16 ? 1 : 2) * args.size_for_type * args.width * args.height;

	if (args.input_texture)
	{
		Format format = FormatUnknown;

		switch (args.type)
		{
		case ComplexToComplexDual:
			format = FormatR32G32B32A32Float;
			input_target = Image;
			break;

		case ComplexToComplex:
		case ComplexToReal:
			format = FormatR32G32Float;
			input_target = Image;
			break;

		case RealToComplex:
			format = FormatR32Float;
			input_target = ImageReal;
			break;
		}

		input = context->create_texture(nullptr, args.width, args.height, format);
	}
	else
	{
		vector<uint8_t> tmp(buffer_size);
		input = context->create_buffer(tmp.data(), buffer_size, AccessStaticCopy);
	}

	if (args.output_texture)
	{
		Format format = FormatUnknown;

		switch (args.type)
		{
		case ComplexToComplexDual:
			format = FormatR16G16B16A16Float;
			output_target = Image;
			break;

		case ComplexToComplex:
		case RealToComplex:
			format = FormatR16G16Float;
			output_target = Image;
			break;

		case ComplexToReal:
			format = FormatR32Float;
			output_target = ImageReal;
			break;
		}

		output = context->create_texture(nullptr, args.width, args.height, format);
	}
	else
	{
		output = context->create_buffer(nullptr, buffer_size, AccessStreamCopy);
	}

	FFTWisdom wisdom;
	wisdom.set_static_wisdom(FFTWisdom::get_static_wisdom_from_renderer(context));
	wisdom.set_bench_params(args.warmup, args.iterations, args.dispatches, args.timeout);
	wisdom.learn_optimal_options_exhaustive(context, args.width, args.height, args.type, input_target, output_target,
	                                        options.type);

	context->wait_idle();

	FFT fft(context, args.width, args.height, args.type, args.type == ComplexToReal ? Inverse : Forward, input_target,
	        output_target, cache, options, wisdom);

	double estimated_gflops = 1e-9 * get_estimated_flops(args.width, args.height, args.type);
	double estimated_bandwidth_gb =
	    1e-9 * fft.get_num_passes() * get_estimated_bw_per_pass(args.width, args.height, args.type, args.fp16);

	context->log("Test:\n");
	context->log("  %s -> %s\n", input_target == SSBO ? "SSBO" : "Texture", output_target == SSBO ? "SSBO" : "Image");
	context->log("  Size: %u x %u %s %s\n", args.width, args.height, args.string_for_type, args.fp16 ? "FP16" : "FP32");

	double dispatch_time = fft.bench(context, output.get(), input.get(), args.warmup, args.iterations, args.dispatches, args.timeout);
	context->log("  %8.3f ms\n", 1000.0 * dispatch_time);
	context->log("  %8.3f GFlop/s (estimated)\n", estimated_gflops / dispatch_time);
	context->log("  %8.3f GB/s global memory bandwidth (estimated)\n", estimated_bandwidth_gb / dispatch_time);
}

static void cli_help(Context *context, char *argv[])
{
	context->log("Usage: %s [test | bench | help] (args...)\n", argv[0]);
	context->log("       For help on various subsystems, e.g. %s test help\n", argv[0]);
}

static void cli_test_help(Context *context)
{
	context->log(
	    "Usage: test [--test testid] [--test-all] [--test-range testidmin testidmax] [--exit-on-fail] "
	    "[--minimum-snr-fp16 value-db] [--minimum-snr-fp32 value-db] [--epsilon-fp16 value] [--epsilon-fp32 value]\n"
	    "       --test testid: Run a specific test, indexed by number.\n"
	    "       --test-all: Run all tests.\n"
	    "       --test-range testidmin testidmax: Run specific tests between testidmin and testidmax, indexed by "
	    "number.\n"
	    "       --exit-on-fail: Exit immediately when a test does not pass.\n");
}

static int cli_test(Context *context, int argc, char *argv[])
{
	if (argc < 1)
	{
		cli_test_help(context);
		return EXIT_FAILURE;
	}

	TestSuiteArguments args;

	CLICallbacks cbs;
	cbs.add("help", [context](CLIParser &parser) {
		cli_test_help(context);
		parser.end();
	});
	cbs.add("--test", [&args](CLIParser &parser) {
		args.test_id_min = args.test_id_max = parser.next_uint();
		args.exhaustive = false;
	});
	cbs.add("--test-range", [&args](CLIParser &parser) {
		args.test_id_min = parser.next_uint();
		args.test_id_max = parser.next_uint();
		args.exhaustive = false;
	});
	cbs.add("--test-all", [&args](CLIParser &) { args.exhaustive = true; });
	cbs.add("--exit-on-fail", [&args](CLIParser &) { args.throw_on_fail = true; });
	cbs.add("--minimum-snr-fp16", [&args](CLIParser &parser) { args.min_snr_fp16 = parser.next_double(); });
	cbs.add("--minimum-snr-fp32", [&args](CLIParser &parser) { args.min_snr_fp32 = parser.next_double(); });
	cbs.add("--epsilon-fp16", [&args](CLIParser &parser) { args.epsilon_fp16 = parser.next_double(); });
	cbs.add("--epsilon-fp32", [&args](CLIParser &parser) { args.epsilon_fp32 = parser.next_double(); });

	cbs.error_handler = [context] { cli_test_help(context); };
	CLIParser parser(move(cbs), argc, argv);

	if (!parser.parse())
	{
		return EXIT_FAILURE;
	}
	else if (parser.is_ended_state())
	{
		return EXIT_SUCCESS;
	}

	run_test_suite(context, args);
	return EXIT_SUCCESS;
}

static void cli_bench_help(Context *context)
{
	context->log("Usage: bench [--width value] [--height value] [--warmup arg] [--iterations arg] [--dispatches arg] "
	             "[--timeout arg] [--type type] [--input-texture] [--output-texture] [--fp16]\n"
	             "--type type: ComplexToComplex, ComplexToComplexDual, ComplexToReal, RealToComplex\n");
}

static Type parse_type(const char *arg, BenchArguments &args)
{
	if (!strcmp(arg, "ComplexToComplex"))
	{
		args.size_for_type = 2;
		return ComplexToComplex;
	}
	else if (!strcmp(arg, "ComplexToComplexDual"))
	{
		args.size_for_type = 4;
		args.string_for_type = "C2C dual";
		return ComplexToComplexDual;
	}
	else if (!strcmp(arg, "RealToComplex"))
	{
		args.size_for_type = 2;
		args.string_for_type = "R2C";
		return RealToComplex;
	}
	else if (!strcmp(arg, "ComplexToReal"))
	{
		args.size_for_type = 2;
		args.string_for_type = "C2R";
		return ComplexToReal;
	}
	else
	{
		throw logic_error("Invalid argument to parse_type().\n");
	}
}

static int cli_bench(Context *context, int argc, char *argv[])
{
	if (argc < 1)
	{
		cli_bench_help(context);
		return EXIT_FAILURE;
	}

	BenchArguments args;

	CLICallbacks cbs;
	cbs.add("help", [context](CLIParser &parser) {
		cli_bench_help(context);
		parser.end();
	});
	cbs.add("--width", [&args](CLIParser &parser) { args.width = parser.next_uint(); });
	cbs.add("--height", [&args](CLIParser &parser) { args.height = parser.next_uint(); });
	cbs.add("--warmup", [&args](CLIParser &parser) { args.warmup = parser.next_uint(); });
	cbs.add("--iterations", [&args](CLIParser &parser) { args.iterations = parser.next_uint(); });
	cbs.add("--dispatches", [&args](CLIParser &parser) { args.dispatches = parser.next_uint(); });
	cbs.add("--timeout", [&args](CLIParser &parser) { args.timeout = parser.next_double(); });
	cbs.add("--fp16", [&args](CLIParser &) { args.fp16 = true; });
	cbs.add("--type", [&args](CLIParser &parser) { args.type = parse_type(parser.next_string(), args); });
	cbs.add("--input-texture", [&args](CLIParser &) { args.input_texture = true; });
	cbs.add("--output-texture", [&args](CLIParser &) { args.output_texture = true; });

	cbs.error_handler = [context] { cli_bench_help(context); };

	CLIParser parser(move(cbs), argc, argv);

	if (!parser.parse())
	{
		return EXIT_FAILURE;
	}
	else if (parser.is_ended_state())
	{
		return EXIT_SUCCESS;
	}

	run_benchmark(context, args);
	return EXIT_SUCCESS;
}

int GLFFT::cli_main(Context *context, int argc, char *argv[])
{
	// Do not leak exceptions beyond this function.
	try
	{
		if (argc < 2)
		{
			cli_help(context, argv);
			return EXIT_FAILURE;
		}

		if (!strcmp(argv[1], "test"))
		{
			return cli_test(context, argc - 2, argv + 2);
		}
		else if (!strcmp(argv[1], "bench"))
		{
			return cli_bench(context, argc - 2, argv + 2);
		}
		else if (!strcmp(argv[1], "help"))
		{
			cli_help(context, argv);
			return EXIT_SUCCESS;
		}
		else
		{
			cli_help(context, argv);
			return EXIT_FAILURE;
		}
	}
	catch (const std::exception &e)
	{
		context->log("Caught exception \"%s\" ...\n", e.what());
		return EXIT_FAILURE;
	}
}
