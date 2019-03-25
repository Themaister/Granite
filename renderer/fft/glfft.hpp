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
#include "glfft_wisdom.hpp"
#include <limits>
#include <unordered_map>
#include <vector>

/// GLFFT doesn't try to preserve GL state in any way.
/// E.g. SHADER_STORAGE_BUFFER bindings, programs bound, texture bindings, etc.
/// Applications calling this library must expect that some GL state will be modified.
/// No rendering state associated with graphics will be modified.

namespace GLFFT
{

class FFT
{
public:
	/// @brief Creates a full FFT.
	///
	/// All buffer allocation done by GLFFT will be done in constructor.
	/// Will throw if invalid parameters are passed.
	///
	/// @param context       The graphics context.
	/// @param Nx            Number of samples in horizontal dimension.
	/// @param Ny            Number of samples in vertical dimension.
	/// @param type          The transform type.
	/// @param direction     Forward, inverse or inverse with convolution.
	///                      For real-to-complex and complex-to-real transforms, the
	///                      transform type must match.
	/// @param input_target  GL object type of input target. For real-to-complex with texture as input, ImageReal is used.
	/// @param output_target GL object type of output target. For complex-to-real with texture as output, ImageReal is used.
	/// @param cache         A program cache for caching the GLFFT programs created.
	/// @param options       FFT options such as performance related parameters and types.
	/// @param wisdom        GLFFT wisdom which can override performance related options
	///                      (options.performance is used as a fallback).
	FFT(Context *context, unsigned Nx, unsigned Ny, Type type, Direction direction, Target input_target,
	    Target output_target, std::shared_ptr<ProgramCache> cache, const FFTOptions &options,
	    const FFTWisdom &wisdom = FFTWisdom());

	/// @brief Creates a single stage FFT. Used mostly internally for benchmarking partial FFTs.
	///
	/// All buffer allocation done by GLFFT will be done in constructor.
	/// Will throw if invalid parameters are passed.
	///
	/// @param context       The graphics context.
	/// @param Nx            Number of samples in horizontal dimension.
	/// @param Ny            Number of samples in vertical dimension.
	/// @param radix         FFT radix to test.
	/// @param p             Accumulated p factor. If 1, "first pass" mode is tested, otherwise, generic FFT stages.
	/// @param mode          The transform mode.
	/// @param input_target  GL object type of input target. For real-to-complex with texture as input, ImageReal is used.
	/// @param output_target GL object type of output target. For complex-to-real with texture as output, ImageReal is used.
	/// @param cache         A program cache for caching the GLFFT programs created.
	/// @param options       FFT options such as performance related parameters and types.
	FFT(Context *context, unsigned Nx, unsigned Ny, unsigned radix, unsigned p, Mode mode, Target input_target,
	    Target output_target, std::shared_ptr<ProgramCache> cache, const FFTOptions &options);

	/// @brief Process the FFT.
	///
	/// The type of object passed here must match what FFT was initialized with.
	///
	/// @param cmd       Command buffer for issuing dispatch commands.
	/// @param output    Output buffer or image.
	///                  NOTE: For images, the texture must be using immutable storage, i.e. glTexStorage2D!
	/// @param input     Input buffer or texture.
	/// @param input_aux If using convolution transform type,
	///                  the content of input and input_aux will be multiplied together.
	void process(CommandBuffer *cmd, Resource *output, Resource *input, Resource *input_aux = nullptr);

	/// @brief Run process() multiple times, timing the results.
	///
	/// Mostly used internally by GLFFT wisdom, glfft_cli's bench, and so on.
	///
	/// @param bench_context                  The graphics context.
	/// @param output                   Output buffer or image.
	///                                 NOTE: For images, the texture must be using immutable storage, i.e. glTexStorage2D!
	/// @param input                    Input buffer or texture.
	/// @param warmup_iterations        Number of iterations to run to "warm" up GL, ensures we don't hit
	///                                 recompilations or similar when benching.
	/// @param iterations               Number of iterations to run the benchmark.
	///                                 Each iteration will ensure timing with a glFinish() followed by timing.
	/// @param dispatches_per_iteration Number of calls to process() we should do per iteration.
	/// @param max_time                 The max time the benchmark should run. Will be checked after each iteration is complete.
	///
	/// @returns Average GPU time per process() call.
	double bench(Context *bench_context, Resource *output, Resource *input, unsigned warmup_iterations, unsigned iterations,
	             unsigned dispatches_per_iteration, double max_time = std::numeric_limits<double>::max());

	/// @brief Returns cost for a process() call. Only used for debugging.
	double get_cost() const
	{
		return cost;
	}

	/// @brief Returns number of passes (glDispatchCompute) in a process() call.
	unsigned get_num_passes() const
	{
		return unsigned(passes.size());
	}

	/// @brief Returns Nx.
	unsigned get_dimension_x() const
	{
		return size_x;
	}
	/// @brief Returns Ny.
	unsigned get_dimension_y() const
	{
		return size_y;
	}

	/// @brief Sets offset and scale parameters for normalized texel coordinates when sampling textures.
	///
	/// By default, these values are 0.5 / size (samples in the center of texel (0, 0)).
	/// Scale is 1.0 / size, so it steps one texel for each coordinate in the FFT transform.
	/// Setting this to something custom is useful to get downsampling with GL_LINEAR -> FFT transform
	/// without having to downsample the texture first, then FFT.
	void set_texture_offset_scale(float offset_x, float offset_y, float scale_x, float scale_y)
	{
		texture.offset_x = offset_x;
		texture.offset_y = offset_y;
		texture.scale_x = scale_x;
		texture.scale_y = scale_y;
	}

	/// @brief Set binding range for input.
	///
	/// If input is an SSBO, set a custom binding range to be passed to glBindBufferRange.
	/// By default, the entire buffer is bound.
	void set_input_buffer_range(size_t offset, size_t size)
	{
		ssbo.input.offset = offset;
		ssbo.input.size = size;
	}

	/// @brief Set binding range for input_aux.
	///
	/// If input_aux is an SSBO, set a custom binding range to be passed to glBindBufferRange.
	/// By default, the entire buffer is bound.
	void set_input_aux_buffer_range(size_t offset, size_t size)
	{
		ssbo.input_aux.offset = offset;
		ssbo.input_aux.size = size;
	}

	/// @brief Set binding range for output.
	///
	/// If output buffer is an SSBO, set a custom binding range to be passed to glBindBufferRange.
	/// By default, the entire buffer is bound.
	void set_output_buffer_range(size_t offset, size_t size)
	{
		ssbo.output.offset = offset;
		ssbo.output.size = size;
	}

	/// @brief Set samplers for input textures.
	///
	/// Set sampler objects to be used for input and input_aux if textures are used as input.
	/// By default, sampler object 0 will be used (inheriting sampler parameters from the texture object itself).
	void set_samplers(Sampler *sampler0, Sampler *sampler1 = nullptr)
	{
		texture.samplers[0] = sampler0;
		texture.samplers[1] = sampler1;
	}

private:
	Context *context;

	struct Pass
	{
		Parameters parameters;

		unsigned workgroups_x;
		unsigned workgroups_y;
		unsigned uv_scale_x;
		unsigned stride;
		Program *program;
	};

	double cost = 0.0;

	std::unique_ptr<Buffer> temp_buffer;
	std::unique_ptr<Buffer> temp_buffer_image;
	std::vector<Pass> passes;
	std::shared_ptr<ProgramCache> cache;

	std::unique_ptr<Program> build_program(const Parameters &params);
	std::string load_shader_string(const char *path);

	Program *get_program(const Parameters &params);

	struct
	{
		float offset_x = 0.0f, offset_y = 0.0f, scale_x = 1.0f, scale_y = 1.0f;
		Sampler *samplers[2] = { nullptr, nullptr };
	} texture;

	struct
	{
		struct
		{
			size_t offset = 0;
			size_t size = 0;
		} input, input_aux, output;
	} ssbo;
	unsigned size_x, size_y;
};

} // namespace GLFFT
