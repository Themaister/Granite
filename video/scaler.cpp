/* Copyright (c) 2017-2025 Hans-Kristian Arntzen
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

#include "scaler.hpp"
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include "muglm/matrix_helper.hpp"
#include "device.hpp"
#include "transforms.hpp"

using namespace Vulkan;

namespace Granite
{
void VideoScaler::reset()
{
	weights.reset();
}

void VideoScaler::set_program(Program *scale_)
{
	scale = scale_;
}

static float sinc(float v)
{
	v *= muglm::pi<float>();
	if (muglm::abs(v) < 0.0001f)
		return 1.0f;
	else
		return muglm::sin(v) / v;
}

static float hann(float v)
{
	// Raised cosine.
	assert(v >= -1.0f && v <= 1.0f);
	v = muglm::cos(0.5f * v * muglm::pi<float>());
	return v * v;
}

enum
{
	CONTROL_SKIP_RESCALE_BIT = 1 << 0,
	CONTROL_DOWNSCALING_BIT = 1 << 1,
	CONTROL_SAMPLED_DOWNSCALING_BIT = 1 << 2,
	CONTROL_CLAMP_COORD_BIT = 1 << 3,
	CONTROL_CHROMA_SUBSAMPLE_BIT = 1 << 4,
	CONTROL_PRIMARY_CONVERSION_BIT = 1 << 5,
	CONTROL_DITHER_BIT = 1 << 6
};

enum
{
	TRANSFER_IDENTITY = 0,
	TRANSFER_SRGB = 1, // The piece-wise linear approximation.
	TRANSFER_PQ = 2
};

void VideoScaler::update_weights(CommandBuffer &cmd, const RescaleInfo &info)
{
	constexpr int Phases = 256;
	constexpr int Taps = 8;

	if (!weights)
	{
		BufferCreateInfo weights_info = {};
		weights_info.size = Phases * Taps * sizeof(uint16_t) * 2;
		weights_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		weights_info.domain = BufferDomain::Device;
		weights = cmd.get_device().create_buffer(weights_info);
	}

	if (last_input_width == info.input->get_view_width() &&
	    last_input_height == info.input->get_view_height() &&
		last_output_width == info.output_planes[0]->get_view_width() &&
		last_output_height == info.output_planes[0]->get_view_height())
	{
		return;
	}

	last_input_width = info.input->get_view_width();
	last_input_height = info.input->get_view_height();
	last_output_width = info.output_planes[0]->get_view_width();
	last_output_height = info.output_planes[0]->get_view_height();

	float bw = muglm::clamp(float(info.output_planes[0]->get_view_width()) / float(info.input->get_view_width()), 0.5f, 1.0f);
	float bh = muglm::clamp(float(info.output_planes[0]->get_view_height()) / float(info.input->get_view_height()), 0.5f, 1.0f);

	float weights_data[2][Phases][Taps] = {};
	uint16_t weights_data16[2][Phases][Taps] = {};

	for (int phase = 0; phase < Phases; phase++)
	{
		float total_horiz = 0.0f;
		float total_vert = 0.0f;

		for (int tap = 0; tap < Taps; tap++)
		{
			constexpr int HalfTaps = Taps / 2;
			constexpr int TapOffset = HalfTaps - 1;
			float l = float(tap - TapOffset) - float(phase) / float(Phases);

			float w_horiz = hann(l / float(HalfTaps)) * sinc(bw * l);
			float w_vert = hann(l / float(HalfTaps)) * sinc(bh * l);

			total_horiz += w_horiz;
			total_vert += w_vert;

			weights_data[0][phase][tap] = w_horiz;
			weights_data[1][phase][tap] = w_vert;
		}

		for (auto &w : weights_data[0][phase])
			w /= total_horiz;
		for (auto &w : weights_data[1][phase])
			w /= total_vert;
	}

	for (int dim = 0; dim < 2; dim++)
		for (int phase = 0; phase < Phases; phase++)
			for (int tap = 0; tap < Taps; tap++)
				weights_data16[dim][phase][tap] = muglm::floatToHalf(weights_data[dim][phase][tap]);

	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
	memcpy(cmd.update_buffer(*weights, 0, sizeof(weights_data16)), weights_data16, sizeof(weights_data16));
	cmd.barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

static bool recognized_color_space(VkColorSpaceKHR space)
{
	switch (space)
	{
	case VK_COLOR_SPACE_HDR10_ST2084_EXT:
	case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
	case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
		return true;

	default:
		return false;
	}
}

void VideoScaler::rescale(CommandBuffer &cmd, const RescaleInfo &info)
{
	if (!recognized_color_space(info.input_color_space) || !recognized_color_space(info.output_color_space))
	{
		LOGE("Attempting to use unrecognized color space.\n");
		return;
	}

	if (info.num_output_planes > 1 && info.output_color_space == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)
	{
		LOGE("Only nonlinear output color spaces are supported for YCbCr.\n");
		return;
	}

	if (!scale)
	{
		LOGE("No scaling program has been set.\n");
		return;
	}

	struct Push
	{
		ivec2 resolution;
		vec2 scaling_to_input;
		vec2 inv_input_resolution;
		float dither_strength;
	};

	Push push = {};
	push.resolution.x = int(info.input->get_view_width());
	push.resolution.y = int(info.input->get_view_height());

	push.scaling_to_input.x = float(push.resolution.x) / float(info.output_planes[0]->get_view_width());
	push.scaling_to_input.y = float(push.resolution.y) / float(info.output_planes[0]->get_view_height());
	bool sampled_downscaling = push.scaling_to_input.x > 2.0f || push.scaling_to_input.y > 2.0f;
	// The filter doesn't have shared memory or kernel support to deal with ridiculous downsampling ratios,
	// do it in multiple stages if need be.
	push.scaling_to_input = muglm::min(vec2(2.0f), push.scaling_to_input);
	push.inv_input_resolution.x = 1.0f / (float(info.output_planes[0]->get_view_width()) * push.scaling_to_input.x);
	push.inv_input_resolution.y = 1.0f / (float(info.output_planes[0]->get_view_height()) * push.scaling_to_input.y);

	update_weights(cmd, info);

	uint32_t flags = 0;
	uint32_t eotf = TRANSFER_IDENTITY;
	uint32_t oetf = TRANSFER_IDENTITY;

	switch (info.input_color_space)
	{
	case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
		if (!format_is_srgb(info.input->get_format()))
			eotf = TRANSFER_SRGB;
		break;

	case VK_COLOR_SPACE_HDR10_ST2084_EXT:
		eotf = TRANSFER_PQ;
		break;

	default:
		break;
	}

	switch (info.output_color_space)
	{
	case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
		oetf = TRANSFER_SRGB;
		break;

	case VK_COLOR_SPACE_HDR10_ST2084_EXT:
		oetf = TRANSFER_PQ;
		break;

	default:
		break;
	}

	if (info.input->get_view_width() == info.output_planes[0]->get_view_width() &&
	    info.input->get_view_height() == info.output_planes[0]->get_view_height())
	{
		flags |= CONTROL_SKIP_RESCALE_BIT;
	}

	if (push.scaling_to_input.x > 1.0f || push.scaling_to_input.y > 1.0f)
		flags |= CONTROL_DOWNSCALING_BIT;
	if (sampled_downscaling)
		flags |= CONTROL_SAMPLED_DOWNSCALING_BIT;

	if (info.input_color_space != info.output_color_space)
		flags |= CONTROL_PRIMARY_CONVERSION_BIT;

	flags |= CONTROL_CLAMP_COORD_BIT;

	if (info.num_output_planes > 1 &&
	    info.output_planes[0]->get_view_width() > info.output_planes[1]->get_view_width())
	{
		flags |= CONTROL_CHROMA_SUBSAMPLE_BIT;
	}

	switch (info.output_planes[0]->get_format())
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
		flags |= CONTROL_DITHER_BIT;
		push.dither_strength = 1.0f / 255.0f;
		break;

	default:
		break;
	}

	if (oetf == eotf && (flags & CONTROL_SKIP_RESCALE_BIT) != 0)
	{
		eotf = TRANSFER_IDENTITY;
		oetf = TRANSFER_IDENTITY;
	}

	cmd.set_program(scale);
	cmd.set_specialization_constant_mask(0xf);
	cmd.set_specialization_constant(0, flags);
	cmd.set_specialization_constant(1, eotf);
	cmd.set_specialization_constant(2, oetf);
	cmd.set_specialization_constant(3, info.num_output_planes);
	cmd.enable_subgroup_size_control(true);
	cmd.set_subgroup_size_log2(true, 2, 6);

	cmd.set_texture(0, 0, *info.input);
	cmd.set_sampler(0, 1, StockSampler::LinearClamp);
	cmd.set_unorm_storage_texture(0, 2, *info.output_planes[0]);
	cmd.set_storage_buffer(0, 3, *weights);

	struct UBO
	{
		vec4 gamma_space_transform[3];
		vec4 primary_transform[3];
	};
	auto *ubo = cmd.allocate_typed_constant_data<UBO>(0, 4, 1);

	if (info.output_color_space == VK_COLOR_SPACE_HDR10_ST2084_EXT)
	{
		ubo->gamma_space_transform[0] = vec4(0.5f, -0.459786f, -0.0402143f, 0.5f);
		ubo->gamma_space_transform[1] = vec4(0.2627f, 0.678f, 0.0593f, 0.0f);
		ubo->gamma_space_transform[2] = vec4(-0.13963f, -0.36037f, 0.5f, 0.5f);
	}
	else
	{
		// Everything else is standard BT.709.
		ubo->gamma_space_transform[0] = vec4(0.5f, -0.454153f, -0.0458471f, 0.5f);
		ubo->gamma_space_transform[1] = vec4(0.2126f, 0.7152f, 0.0722f, 0.0f);
		ubo->gamma_space_transform[2] = vec4(-0.114572f, -0.385428f, 0.5f, 0.5f);
	}

	const Primaries bt709 = {
		{ 0.640f, 0.330f },
		{ 0.300f, 0.600f },
		{ 0.150f, 0.060f },
		{ 0.3127f, 0.3290f },
	};

	const Primaries bt2020 = {
		{ 0.708f, 0.292f },
		{ 0.170f, 0.797f },
		{ 0.131f, 0.046f },
		{ 0.3127f, 0.3290f },
	};

	if (info.input_color_space != info.output_color_space)
	{
		auto &output_primaries = info.output_color_space == VK_COLOR_SPACE_HDR10_ST2084_EXT ? bt2020 : bt709;
		auto &input_primaries = info.input_color_space == VK_COLOR_SPACE_HDR10_ST2084_EXT ? bt2020 : bt709;
		mat3 output_transform = inverse(compute_xyz_matrix(output_primaries));
		mat3 input_transform = compute_xyz_matrix(input_primaries);
		mat3 conv = output_transform * input_transform;

		float sdr_scale = 1.0f;
		if (info.input_color_space == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			sdr_scale = 200.0f;
		else if (info.input_color_space == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)
			sdr_scale = 80.0f;

		if (info.output_color_space == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)
			sdr_scale /= 80.0f;

		ubo->primary_transform[0] = vec4(sdr_scale * conv[0], 0.0f);
		ubo->primary_transform[1] = vec4(sdr_scale * conv[1], 0.0f);
		ubo->primary_transform[2] = vec4(sdr_scale * conv[2], 0.0f);
	}

	cmd.set_unorm_storage_texture(0, 5, *info.output_planes[info.num_output_planes >= 2 ? 1 : 0]);
	cmd.set_unorm_storage_texture(0, 6, *info.output_planes[info.num_output_planes >= 3 ? 2 : 0]);

	auto start_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch((info.output_planes[0]->get_view_width() + 7) / 8, (info.output_planes[0]->get_view_height() + 7) / 8, 1);
	auto end_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	cmd.get_device().register_time_interval("GPU", std::move(start_ts), std::move(end_ts), "scale");

	cmd.enable_subgroup_size_control(false);
	cmd.set_specialization_constant_mask(0);
}
}
