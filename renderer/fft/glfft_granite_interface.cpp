/* Copyright (C) 2018 Hans-Kristian Arntzen <maister@archlinux.us>
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

#include "glfft_granite_interface.hpp"
#include "device.hpp"
#include "command_buffer.hpp"
#include "timer.hpp"
#include <stdarg.h>
using namespace std;

namespace Granite
{
struct FFTBuffer : GLFFT::Buffer
{
	Vulkan::BufferHandle buffer;
};

struct FFTTexture : GLFFT::Texture
{
	Vulkan::ImageHandle image;
};

struct FFTProgram : GLFFT::Program
{
	Vulkan::Program *program;
};

struct FFTSampler : GLFFT::Sampler
{
	FFTSampler(const Vulkan::Sampler &sampler)
		: sampler(sampler)
	{
	}

	const Vulkan::Sampler &sampler;
};

struct FFTCommandBuffer : GLFFT::CommandBuffer
{
	FFTCommandBuffer(Vulkan::CommandBufferHandle cmd)
		: cmd(move(cmd))
	{
	}

	void barrier() override;
	void barrier(GLFFT::Texture *) override;
	void barrier(GLFFT::Buffer *) override;
	void bind_program(GLFFT::Program *program) override;
	void bind_sampler(unsigned binding, GLFFT::Sampler *sampler) override;
	void bind_storage_texture(unsigned binding, GLFFT::Texture *texture) override;
	void bind_texture(unsigned binding, GLFFT::Texture *texture) override;
	void bind_storage_buffer(unsigned binding, GLFFT::Buffer *buffer) override;
	void bind_storage_buffer_range(unsigned binding, size_t offset, size_t length, GLFFT::Buffer *buffer) override;
	void dispatch(unsigned x, unsigned y, unsigned z) override;
	void push_constant_data(const void *data, size_t size) override;

	Vulkan::CommandBufferHandle cmd;
};

void FFTCommandBuffer::push_constant_data(const void *data, size_t size)
{
	cmd->push_constants(data, 0, size);
}

void FFTCommandBuffer::barrier()
{
	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void FFTCommandBuffer::barrier(GLFFT::Texture *)
{
	barrier();
}

void FFTCommandBuffer::barrier(GLFFT::Buffer *)
{
	barrier();
}

void FFTCommandBuffer::bind_program(GLFFT::Program *program)
{
	cmd->set_program(*static_cast<FFTProgram *>(program)->program);
}

void FFTCommandBuffer::bind_sampler(unsigned binding, GLFFT::Sampler *sampler)
{
	if (sampler)
		cmd->set_sampler(0, binding, static_cast<FFTSampler *>(sampler)->sampler);
}

void FFTCommandBuffer::bind_storage_texture(unsigned binding, GLFFT::Texture *texture)
{
	cmd->set_storage_texture(0, binding, static_cast<FFTTexture*>(texture)->image->get_view());
}

void FFTCommandBuffer::bind_texture(unsigned binding, GLFFT::Texture *texture)
{
	cmd->set_texture(0, binding, static_cast<FFTTexture *>(texture)->image->get_view(),
	                 Vulkan::StockSampler::NearestClamp);
}

void FFTCommandBuffer::bind_storage_buffer(unsigned binding, GLFFT::Buffer *buffer)
{
	cmd->set_storage_buffer(0, binding, *static_cast<FFTBuffer *>(buffer)->buffer);
}

void FFTCommandBuffer::bind_storage_buffer_range(unsigned binding, size_t offset, size_t range, GLFFT::Buffer *buffer)
{
	cmd->set_storage_buffer(0, binding, *static_cast<FFTBuffer *>(buffer)->buffer, offset, range);
}

void FFTCommandBuffer::dispatch(unsigned x, unsigned y, unsigned z)
{
	cmd->dispatch(x, y, z);
}

const void *FFTInterface::map(GLFFT::Buffer *buffer_, size_t offset, size_t)
{
	auto *buffer = static_cast<FFTBuffer *>(buffer_);
	return static_cast<uint8_t *>(device.map_host_buffer(*buffer->buffer,
	                                                     Vulkan::MEMORY_ACCESS_READ)) + offset;
}

void FFTInterface::wait_idle()
{
	device.wait_idle();
}

unique_ptr<GLFFT::Buffer> FFTInterface::create_buffer(const void *initial_data, size_t size,
                                                      GLFFT::AccessMode access)
{
	Vulkan::BufferCreateInfo info = {};
	info.size = size;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.domain = access == GLFFT::AccessMode::AccessStreamRead ?
	              Vulkan::BufferDomain::CachedHost :
	              Vulkan::BufferDomain::Device;

	auto buffer = make_unique<FFTBuffer>();
	buffer->buffer = device.create_buffer(info, initial_data);
	return buffer;
}

unique_ptr<GLFFT::Texture> FFTInterface::create_texture(const void *initial_data, unsigned width, unsigned height,
                                                        GLFFT::Format format)
{
	VkFormat fmt = VK_FORMAT_UNDEFINED;
	switch (format)
	{
	case GLFFT::Format::FormatR16Float:
		fmt = VK_FORMAT_R16_SFLOAT;
		break;
	case GLFFT::Format::FormatR16G16Float:
		fmt = VK_FORMAT_R16G16_SFLOAT;
		break;
	case GLFFT::Format::FormatR16G16B16A16Float:
		fmt = VK_FORMAT_R16G16B16A16_SFLOAT;
		break;
	case GLFFT::Format::FormatR32Float:
		fmt = VK_FORMAT_R32_SFLOAT;
		break;
	case GLFFT::Format::FormatR32G32Float:
		fmt = VK_FORMAT_R32G32_SFLOAT;
		break;
	case GLFFT::Format::FormatR32G32B32A32Float:
		fmt = VK_FORMAT_R32G32B32A32_SFLOAT;
		break;
	default:
		return {};
	}

	auto info = Vulkan::ImageCreateInfo::immutable_2d_image(width, height, fmt);
	info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	auto image = make_unique<FFTTexture>();

	Vulkan::ImageInitialData init = {};
	init.data = initial_data;
	image->image= device.create_image(info, initial_data ? &init : nullptr);
	return image;
}

unsigned FFTInterface::get_max_work_group_threads()
{
	return device.get_gpu_properties().limits.maxComputeWorkGroupInvocations;
}

const char *FFTInterface::get_renderer_string()
{
	return device.get_gpu_properties().deviceName;
}

double FFTInterface::get_time()
{
	return get_current_time_nsecs() * 1e-9;
}

bool FFTInterface::supports_texture_readback()
{
	return true;
}

unique_ptr<GLFFT::Program> FFTInterface::compile_compute_shader(const char *source)
{
	GLSLCompiler compiler;
	compiler.set_source(source, "compute.glsl");
	if (!compiler.preprocess())
		return {};
	compiler.set_stage(Stage::Compute);
	auto spirv = compiler.compile();
	if (spirv.empty())
	{
		LOGE("GLFFT: error: \n%s\n",
		     compiler.get_error_message().c_str());
		return {};
	}

	Vulkan::Shader *shader = device.request_shader(spirv.data(), spirv.size() * sizeof(uint32_t));
	Vulkan::Program *program = device.request_program(shader);
	auto prog = make_unique<FFTProgram>();
	prog->program = program;
	return prog;
}

void FFTInterface::unmap(GLFFT::Buffer *buffer_)
{
	auto *buffer = static_cast<FFTBuffer *>(buffer_);
	device.unmap_host_buffer(*buffer->buffer);
}

void FFTInterface::log(const char *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	char buffer[16 * 1024];
	vsprintf(buffer, fmt, va);
	LOGI("GLFFT: %s\n", buffer);
	va_end(va);
}

void FFTInterface::read_texture(void *buffer, GLFFT::Texture *texture)
{
	auto &image = *static_cast<FFTTexture *>(texture)->image;
	Vulkan::BufferCreateInfo info = {};
	info.size = Vulkan::TextureFormatLayout::format_block_size(image.get_format()) *
	            image.get_width() *
	            image.get_height();
	info.domain = Vulkan::BufferDomain::CachedHost;
	info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	auto readback = device.create_buffer(info, nullptr);

	auto cmd = device.request_command_buffer();
	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	cmd->copy_image_to_buffer(*readback, image, 0, {},
	                          { image.get_width(), image.get_height(), 1 },
	                          0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	device.submit(cmd);
	device.wait_idle();

	memcpy(buffer, device.map_host_buffer(*readback, Vulkan::MEMORY_ACCESS_READ), info.size);
	device.unmap_host_buffer(*readback);
}

GLFFT::CommandBuffer *FFTInterface::request_command_buffer()
{
	auto *cmd = new FFTCommandBuffer(device.request_command_buffer());
	return cmd;
}

void FFTInterface::submit_command_buffer(GLFFT::CommandBuffer *cmd_)
{
	auto *cmd = static_cast<FFTCommandBuffer *>(cmd_);
	device.submit(cmd->cmd);
	delete cmd;
}

FFTInterface::FFTInterface(Vulkan::Device &device)
	: device(device)
{
}

}