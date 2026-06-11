/* Copyright (c) 2017-2026 Hans-Kristian Arntzen
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

#include "application.hpp"
#include "command_buffer.hpp"
#include "device.hpp"
#include "muglm/muglm_impl.hpp"
#include "os_filesystem.hpp"
#include <random>

using namespace Granite;
using namespace Vulkan;

static constexpr uint32_t Width = 256;
static constexpr uint32_t Height = 256;
// Since we don't want this to be a bandwidth test, eliminate BW as the bottleneck
// by executing the shader on blanks.
static constexpr uint32_t DispatchWidth = 4096;
static constexpr uint32_t DispatchHeight = 4096;
static constexpr uint32_t GroupWidth256 = 16;
static constexpr uint32_t GroupHeight256 = 16;

enum Methods
{
	MethodNaiveDirect = 0,
	MethodNaiveDirectDot2C = 1,
	MethodNaiveShared = 2,
	MethodNaiveSharedDot2C = 3,
	MethodCount
};

enum KernelSizes
{
	KernelSize3x3 = 3,
	KernelSize5x5 = 5,
	KernelSize7x7 = 7,
};

static const char *MethodPreNames[MethodCount] = {
	"Naive",
	"Naive",
	"Naive",
	"Naive",
};

static const char *MethodPostNames[MethodCount] = {
	"",
	"Dot2C",
	"Shared",
	"SharedDot2C",
};

static const uint32_t MethodWorkgroupSizes[MethodCount] = {
	256,
	256,
	64,
	64,
};

struct WMMATest : Granite::Application, Granite::EventHandler
{
	WMMATest()
	{
		EVENT_MANAGER_REGISTER_LATCH(WMMATest, on_device_create, on_device_destroy, DeviceCreatedEvent);
		get_wsi().set_present_mode(PresentMode::UnlockedMaybeTear);
	}

	ImageHandle output_image;
	ImageHandle input_image;

	ImageHandle create_image(Device &device, uint32_t width, uint32_t height, VkFormat format)
	{
		ImageCreateInfo info = ImageCreateInfo::immutable_2d_image(width, height, format);
		info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
		info.layout = ImageLayout::General;
		info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
		             VK_IMAGE_USAGE_STORAGE_BIT;
		return device.create_image(info);
	}

	static float get_reference_input(uint32_t x, uint32_t y)
	{
		return float((y * Width + x + 1) % 1023);
	}

	static std::vector<uint16_t> create_input()
	{
		std::vector<uint16_t> data;
		data.reserve(Width * Height);

		for (uint32_t y = 0; y < Height; y++)
			for (uint32_t x = 0; x < Width; x++)
				data.push_back(floatToHalf(get_reference_input(x, y)));

		return data;
	}

	void on_device_create(const DeviceCreatedEvent &e)
	{
		output_image = create_image(e.get_device(), Width, Height, VK_FORMAT_R16_SFLOAT);
		input_image = create_image(e.get_device(), Width, Height, VK_FORMAT_R16_SFLOAT);

		auto cmd = e.get_device().request_command_buffer();

		void *ptr = cmd->update_image(*input_image, {}, { Width, Height, 1 }, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
		auto input = create_input();
		memcpy(ptr, input.data(), input.size() * sizeof(input.front()));

		cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

		e.get_device().submit(cmd);
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
		output_image.reset();
		input_image.reset();
	}

	BufferHandle readback_image_async(CommandBuffer &cmd, const Image &image)
	{
		BufferCreateInfo bufinfo = {
			BufferDomain::CachedHost,
			image.get_width() * image.get_height() * sizeof(uint16_t),
			VK_BUFFER_USAGE_TRANSFER_DST_BIT
		};

		auto readback = get_wsi().get_device().create_buffer(bufinfo);
		cmd.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
		            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
		            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
		cmd.copy_image_to_buffer(*readback, image, 0, {}, {Width, Height, 1},
		                         0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
		cmd.barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);

		return readback;
	}

	struct Outputs
	{
		BufferHandle naive_3x3[MethodCount];
		BufferHandle naive_5x5[MethodCount];
		BufferHandle naive_7x7[MethodCount];
	} outputs;

	uint16_t kernel3x3[3][3] = {};
	uint16_t kernel5x5[5][5] = {};
	uint16_t kernel7x7[7][7] = {};

	uint16_t random_f16(std::default_random_engine &rnd)
	{
		float sign = rnd() & 1 ? -1.0f : 1.0f;
		return floatToHalf(sign * float(rnd() & 63) / 16.0f);
	}

	template <size_t KernelSize>
	void init_random_kernel(std::default_random_engine &rnd, uint16_t (&kernel)[KernelSize][KernelSize])
	{
		for (auto &row : kernel)
			for (auto &col : row)
				col = random_f16(rnd);
	}

	void init_kernels()
	{
#if 1
		std::default_random_engine rnd(1234);
		init_random_kernel(rnd, kernel3x3);
		init_random_kernel(rnd, kernel5x5);
		init_random_kernel(rnd, kernel7x7);
#else
		kernel3x3[0][0] = floatToHalf(1.0f);
#endif
	}

	template <size_t KernelSize>
	void upload_kernel(CommandBuffer &cmd, const uint16_t (&kernel)[KernelSize][KernelSize])
	{
		auto *weights = cmd.allocate_typed_constant_data<uvec4>(0, 2, 16);

		for (auto &row : kernel)
		{
			uvec4 tmp = {};
			memcpy(tmp.data, row, sizeof(row));
			*weights = tmp;
			weights++;
		}

		static_assert(KernelSize <= 8, "KernelSize is too large.");
		weights += 8 - KernelSize;

		// Odd kernel, shift over the kernel by one.
		for (auto &row : kernel)
		{
			uvec4 tmp = {};
			memcpy(reinterpret_cast<uint16_t *>(tmp.data) + 1, row, sizeof(row));
			*weights = tmp;
			weights++;
		}
	}

	void run_sub_tests(CommandBuffer &cmd, int kernel_size)
	{
		BufferHandle *readbacks = nullptr;
		switch (kernel_size)
		{
		case 3:
			readbacks = outputs.naive_3x3;
			break;

		case 5:
			readbacks = outputs.naive_5x5;
			break;

		case 7:
			readbacks = outputs.naive_7x7;
			break;

		default:
			return;
		}

		for (int method_index = 0; method_index < MethodCount; method_index++)
		{
			if ((method_index == MethodNaiveDirectDot2C || method_index == MethodNaiveSharedDot2C) &&
			    !cmd.get_device().get_device_features().shader_mixed_float_dot_product_features.shaderMixedFloatDotProductFloat16AccFloat32)
				continue;

			auto str = MethodPreNames[method_index] + std::to_string(kernel_size) + "x" + std::to_string(kernel_size) + MethodPostNames[method_index];
			cmd.set_specialization_constant(0, MethodWorkgroupSizes[method_index]);
			cmd.set_specialization_constant(1, method_index);

			auto start_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
			cmd.begin_region(str.c_str());
			cmd.dispatch(DispatchWidth / GroupWidth256, DispatchHeight / GroupHeight256, 1);
			cmd.end_region();
			cmd.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
						VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
			auto end_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
			cmd.get_device().register_time_interval("GPU", std::move(start_ts), std::move(end_ts), str);

			readbacks[method_index] = readback_image_async(cmd, *output_image);
		}
	}

	void run_3x3_tests(CommandBuffer &cmd)
	{
		cmd.set_specialization_constant(2, KernelSize3x3);
		upload_kernel(cmd, kernel3x3);
		run_sub_tests(cmd, KernelSize3x3);
	}

	void run_5x5_tests(CommandBuffer &cmd)
	{
		cmd.set_specialization_constant(2, KernelSize5x5);
		upload_kernel(cmd, kernel5x5);
		run_sub_tests(cmd, KernelSize5x5);
	}

	void run_7x7_tests(CommandBuffer &cmd)
	{
		cmd.set_specialization_constant(2, KernelSize7x7);
		upload_kernel(cmd, kernel7x7);
		run_sub_tests(cmd, KernelSize7x7);
	}

	template <size_t KernelSize>
	void report_reference(const uint16_t (&kernel)[KernelSize][KernelSize])
	{
		printf("\n====== Reference %zux%zu ======\n", KernelSize, KernelSize);

		for (uint32_t y = 0; y < 8; y++)
		{
			for (uint32_t x = 0; x < 8; x++)
			{
				float sum = 0.0f;
				for (uint32_t ky = 0; ky < KernelSize; ky++)
				{
					for (uint32_t kx = 0; kx < KernelSize; kx++)
					{
						auto input_value = get_reference_input(x + kx, y + ky);
						sum += input_value * halfToFloat(kernel[ky][kx]);
					}
				}
				printf("%8.3f ", halfToFloat(floatToHalf(sum)));
			}
			printf("\n");
		}

		printf("==================\n");
	}

	void report_image(const char *tag, const BufferHandle &buffer)
	{
		if (!buffer)
			return;

		assert(buffer.get_create_info().size == Width * Height * sizeof(uint16_t));
		auto *out_data = static_cast<uint16_t *>(get_wsi().get_device().map_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT));
		printf("\n====== %s ======\n", tag);
		for (uint32_t y = 0; y < 8; y++)
		{
			for (uint32_t x = 0; x < 8; x++)
				printf("%8.3f ", halfToFloat(out_data[y * Width + x]));
			printf("\n");
		}
		printf("==================\n");
	}

	void report_images(const BufferHandle *handles, int kernel_size)
	{
		for (int method_index = 0; method_index < MethodCount; method_index++)
		{
			auto tag = MethodPreNames[method_index] +
			           std::to_string(kernel_size) + "x" + std::to_string(kernel_size) +
			           MethodPostNames[method_index];
			report_image(tag.c_str(), handles[method_index]);
		}
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();

		cmd->set_program("assets://shaders/wmma.comp", {{ "MIXED_FLOAT_DOT_PRODUCT",
			int(device.get_device_features().shader_mixed_float_dot_product_features.shaderMixedFloatDotProductFloat16AccFloat32) }});

		cmd->set_specialization_constant_mask(0x7);
		//cmd->enable_subgroup_size_control(true);
		//cmd->set_subgroup_size_log2(true, 2, 6);
		cmd->set_texture(0, 0, input_image->get_view(), StockSampler::NearestClamp);
		cmd->set_storage_texture(0, 1, output_image->get_view());

		vec2 inv_resolution = { 1.0f / float(input_image->get_width()), 1.0f / float(input_image->get_height()) };
		cmd->push_constants(&inv_resolution, 0, sizeof(inv_resolution));

		init_kernels();
		run_3x3_tests(*cmd);
		run_5x5_tests(*cmd);
		run_7x7_tests(*cmd);

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(rp);
		cmd->end_render_pass();

		Fence fence;
		device.submit(cmd, &fence);

#if 0
		fence->wait();

		report_reference(kernel3x3);
		report_images(outputs.naive_3x3, KernelSize3x3);
		report_reference(kernel5x5);
		report_images(outputs.naive_5x5, KernelSize5x5);
		report_reference(kernel7x7);
		report_images(outputs.naive_7x7, KernelSize7x7);
#endif

		outputs = {};
	}
};

namespace Granite
{
Application *application_create(int, char **)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	try
	{
		auto *app = new WMMATest();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}