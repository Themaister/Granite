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

#pragma once

#include "glfft_common.hpp"
#include "glfft_interface.hpp"
#include <string>
#include <unordered_map>
#include <utility>

namespace GLFFT
{

struct WisdomPass
{
	struct
	{
		unsigned Nx;
		unsigned Ny;
		unsigned radix;
		Mode mode;
		Target input_target;
		Target output_target;
		FFTOptions::Type type;
	} pass;

	double cost;

	bool operator==(const WisdomPass &other) const
	{
		return std::memcmp(&pass, &other.pass, sizeof(pass)) == 0;
	}
};

} // namespace GLFFT

namespace std
{
template <>
struct hash<GLFFT::WisdomPass>
{
	std::size_t operator()(const GLFFT::WisdomPass &params) const
	{
		std::size_t h = 0;
		hash<uint8_t> hasher;
		for (std::size_t i = 0; i < sizeof(params.pass); i++)
		{
			h ^= hasher(reinterpret_cast<const uint8_t *>(&params.pass)[i]);
		}

		return h;
	}
};
} // namespace std

namespace GLFFT
{

// Adds information which depends on the GPU vendor.
// This can speed up learning process, since there will be fewer "obviously wrong" settings to test.
struct FFTStaticWisdom
{
	enum Tristate
	{
		On = 1,
		Off = 0,
		DontCare = -1
	};

	unsigned min_workgroup_size = 1;
	unsigned min_workgroup_size_shared = 1;
	unsigned max_workgroup_size = 128; // GLES 3.1 mandates support for this.
	unsigned min_vector_size = 2;
	unsigned max_vector_size = 4;
	Tristate shared_banked = DontCare;
};

class FFTWisdom
{
public:
	std::pair<double, FFTOptions::Performance> learn_optimal_options(Context *ctx, unsigned Nx, unsigned Ny,
	                                                                 unsigned radix, Mode mode, Target input_target,
	                                                                 Target output_target,
	                                                                 const FFTOptions::Type &type);

	void learn_optimal_options_exhaustive(Context *ctx, unsigned Nx, unsigned Ny, Type type, Target input_target,
	                                      Target output_target, const FFTOptions::Type &fft_type);

	const std::pair<const WisdomPass, FFTOptions::Performance> *find_optimal_options(
	    unsigned Nx, unsigned Ny, unsigned radix, Mode mode, Target input_target, Target output_target,
	    const FFTOptions::Type &base_options) const;

	const FFTOptions::Performance &find_optimal_options_or_default(unsigned Nx, unsigned Ny, unsigned radix, Mode mode,
	                                                               Target input_target, Target output_target,
	                                                               const FFTOptions &base_options) const;

	void set_static_wisdom(FFTStaticWisdom static_wisdom_)
	{
		static_wisdom = static_wisdom_;
	}

	static FFTStaticWisdom get_static_wisdom_from_renderer(Context *context);
	static FFTOptions::Performance get_static_performance_options_from_renderer(Context *context);

	void set_bench_params(unsigned warmup, unsigned iterations, unsigned dispatches, double timeout)
	{
		params.warmup = warmup;
		params.iterations = iterations;
		params.dispatches = dispatches;
		params.timeout = timeout;
	}

	// Serialization interface.
	std::string archive() const;
	void extract(const char *json);

private:
	std::unordered_map<WisdomPass, FFTOptions::Performance> library;

	std::pair<double, FFTOptions::Performance> study(Context *context, const WisdomPass &pass,
	                                                 FFTOptions::Type options) const;

	double bench(Context *cmd, Resource *output, Resource *input, const WisdomPass &pass, const FFTOptions &options,
	             const std::shared_ptr<ProgramCache> &cache) const;

	FFTStaticWisdom static_wisdom;

	struct
	{
		unsigned warmup = 2;
		unsigned iterations = 20;
		unsigned dispatches = 50;
		double timeout = 1.0;
	} params;
};

} // namespace GLFFT
