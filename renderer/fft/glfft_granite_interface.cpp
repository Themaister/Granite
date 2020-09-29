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
#include "command_buffer.hpp"
#include "device.hpp"
#include "timer.hpp"
#include "compiler.hpp"
#include <stdarg.h>
using namespace std;

namespace Granite
{
struct FFTProgram : GLFFT::Program
{
	Vulkan::Program *program;
};

struct FFTSampler : GLFFT::Sampler
{
	explicit FFTSampler(const Vulkan::Sampler &sampler_)
	    : sampler(sampler_)
	{
	}

	const Vulkan::Sampler &sampler;
};

void FFTDeferredCommandBuffer::ensure_command_list()
{
	if (command_counter >= commands.size())
		commands.resize(command_counter + 1);
}

vector<function<void (Vulkan::CommandBuffer &)>> &FFTDeferredCommandBuffer::get_command_list()
{
	ensure_command_list();
	return commands[command_counter];
}

void FFTDeferredCommandBuffer::push_constant_data(const void *data, size_t size)
{
	std::vector<uint8_t> buffer(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
	get_command_list().push_back([buffer = move(buffer)](Vulkan::CommandBuffer &cmd) {
		cmd.push_constants(buffer.data(), 0, buffer.size());
	});
}

void FFTDeferredCommandBuffer::barrier()
{
	command_counter++;
}

void FFTDeferredCommandBuffer::dispatch(unsigned x, unsigned y, unsigned z)
{
	get_command_list().push_back([=](Vulkan::CommandBuffer &cmd) {
		cmd.dispatch(x, y, z);
	});
}

void FFTDeferredCommandBuffer::bind_storage_buffer(unsigned binding, GLFFT::Buffer *buffer)
{
	get_command_list().push_back(
			[binding, buffer = static_cast<FFTBuffer *>(buffer)->buffer](Vulkan::CommandBuffer &cmd) {
				cmd.set_storage_buffer(0, binding, *buffer);
			});
}

void FFTDeferredCommandBuffer::bind_program(GLFFT::Program *program)
{
	get_command_list().push_back(
			[program = static_cast<FFTProgram *>(program)->program](Vulkan::CommandBuffer &cmd) {
				cmd.set_program(program);
			});
}

void FFTDeferredCommandBuffer::bind_storage_texture(unsigned binding, GLFFT::Texture *texture)
{
	get_command_list().push_back(
			[binding, image = static_cast<FFTTexture *>(texture)->image](Vulkan::CommandBuffer &cmd) {
				cmd.set_storage_texture(0, binding, *image);
			});
}

void FFTDeferredCommandBuffer::bind_texture(unsigned binding, GLFFT::Texture *texture)
{
	get_command_list().push_back(
			[binding, image = static_cast<FFTTexture *>(texture)->image](Vulkan::CommandBuffer &cmd) {
				cmd.set_texture(0, binding, *image,
				                Vulkan::StockSampler::NearestClamp);
			});
}

void FFTDeferredCommandBuffer::bind_sampler(unsigned binding, GLFFT::Sampler *sampler)
{
	if (sampler)
	{
		get_command_list().push_back(
				[binding, sampler = &static_cast<FFTSampler *>(sampler)->sampler](Vulkan::CommandBuffer &cmd)
				{
					cmd.set_sampler(0, binding, *sampler);
				});
	}
}

void FFTDeferredCommandBuffer::bind_storage_buffer_range(unsigned binding, size_t offset, size_t length,
                                                         GLFFT::Buffer *buffer)
{
	get_command_list().push_back(
			[=, buffer = static_cast<FFTBuffer *>(buffer)->buffer](Vulkan::CommandBuffer &cmd) {
				cmd.set_storage_buffer(0, binding, *buffer, offset, length);
			});
}

void FFTDeferredCommandBuffer::build(Vulkan::CommandBuffer &cmd)
{
	for (auto &list : commands)
	{
		for (auto &c : list)
			c(cmd);

		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}
}

void FFTDeferredCommandBuffer::reset_command_counter()
{
	command_counter = 0;
}

void FFTDeferredCommandBuffer::reset()
{
	reset_command_counter();
	commands.clear();
}

void FFTCommandBuffer::push_constant_data(const void *data, size_t size)
{
	if (cmd)
		cmd->push_constants(data, 0, size);
}

void FFTCommandBuffer::barrier()
{
	if (cmd)
	{
		cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}
}

void FFTCommandBuffer::bind_program(GLFFT::Program *program)
{
	if (cmd)
		cmd->set_program(static_cast<FFTProgram *>(program)->program);
}

void FFTCommandBuffer::bind_sampler(unsigned binding, GLFFT::Sampler *sampler)
{
	if (cmd && sampler)
		cmd->set_sampler(0, binding, static_cast<FFTSampler *>(sampler)->sampler);
}

void FFTCommandBuffer::bind_storage_texture(unsigned binding, GLFFT::Texture *texture)
{
	if (cmd)
		cmd->set_storage_texture(0, binding, *static_cast<FFTTexture *>(texture)->image);
}

void FFTCommandBuffer::bind_texture(unsigned binding, GLFFT::Texture *texture)
{
	if (cmd)
	{
		cmd->set_texture(0, binding, *static_cast<FFTTexture *>(texture)->image,
		                 Vulkan::StockSampler::NearestClamp);
	}
}

void FFTCommandBuffer::bind_storage_buffer(unsigned binding, GLFFT::Buffer *buffer)
{
	if (cmd)
		cmd->set_storage_buffer(0, binding, *static_cast<FFTBuffer *>(buffer)->buffer);
}

void FFTCommandBuffer::bind_storage_buffer_range(unsigned binding, size_t offset, size_t range, GLFFT::Buffer *buffer)
{
	if (cmd)
		cmd->set_storage_buffer(0, binding, *static_cast<FFTBuffer *>(buffer)->buffer, offset, range);
}

void FFTCommandBuffer::dispatch(unsigned x, unsigned y, unsigned z)
{
	if (cmd)
		cmd->dispatch(x, y, z);
}

const void *FFTInterface::map(GLFFT::Buffer *buffer_, size_t offset, size_t)
{
	auto *buffer = static_cast<FFTBuffer *>(buffer_);
	return static_cast<uint8_t *>(device->map_host_buffer(*buffer->buffer, Vulkan::MEMORY_ACCESS_READ_BIT)) + offset;
}

void FFTInterface::wait_idle()
{
	device->wait_idle();
}

unique_ptr<GLFFT::Buffer> FFTInterface::create_buffer(const void *initial_data, size_t size, GLFFT::AccessMode access)
{
	Vulkan::BufferCreateInfo info = {};
	info.size = size;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.domain =
	    access == GLFFT::AccessMode::AccessStreamRead ? Vulkan::BufferDomain::CachedHost : Vulkan::BufferDomain::Device;

	auto buffer = make_unique<FFTBuffer>(device->create_buffer(info, initial_data));
	return unique_ptr<GLFFT::Buffer>(move(buffer));
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

	Vulkan::ImageInitialData init = {};
	init.data = initial_data;
	auto image = make_unique<FFTTexture>(device->create_image(info, initial_data ? &init : nullptr));
	image->image_holder->set_layout(Vulkan::Layout::General);
	return unique_ptr<GLFFT::Texture>(move(image));
}

unsigned FFTInterface::get_max_work_group_threads()
{
	return device->get_gpu_properties().limits.maxComputeWorkGroupInvocations;
}

unsigned FFTInterface::get_max_shared_memory_size()
{
	return device->get_gpu_properties().limits.maxComputeSharedMemorySize;
}

uint32_t FFTInterface::get_vendor_id()
{
	return device->get_gpu_properties().vendorID;
}

uint32_t FFTInterface::get_product_id()
{
	return device->get_gpu_properties().deviceID;
}

double FFTInterface::get_time()
{
	return Util::get_current_time_nsecs() * 1e-9;
}

bool FFTInterface::supports_texture_readback()
{
	return true;
}

unique_ptr<GLFFT::Program> FFTInterface::compile_compute_shader(const char *source)
{
	Util::Hasher hasher;
	hasher.string(source);
	Util::Hash hash = hasher.get();

	Util::Hash shader_hash;
	Vulkan::Shader *shader = nullptr;

	if (device->get_shader_manager().get_shader_hash_by_variant_hash(hash, shader_hash))
		shader = device->request_shader_by_hash(shader_hash);

	if (!shader)
	{
		// We don't have a shader, need to compile in runtime :(
		GLSLCompiler compiler;
		compiler.set_source(source, "compute.glsl");
		if (!compiler.preprocess())
			return {};
		compiler.set_stage(Stage::Compute);
		std::string error_message;
		auto spirv = compiler.compile(error_message);
		if (spirv.empty())
		{
			LOGE("GLFFT: error: \n%s\n", error_message.c_str());
			return {};
		}

		shader = device->request_shader(spirv.data(), spirv.size() * sizeof(uint32_t));

		// Register this mapping for next time, hopefully ... :)
		device->get_shader_manager().register_shader_hash_from_variant_hash(hash, shader->get_hash());
	}

	Vulkan::Program *program = device->request_program(shader);
	auto prog = make_unique<FFTProgram>();
	prog->program = program;
	return unique_ptr<GLFFT::Program>(move(prog));
}

void FFTInterface::unmap(GLFFT::Buffer *buffer_)
{
	auto *buffer = static_cast<FFTBuffer *>(buffer_);
	device->unmap_host_buffer(*buffer->buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
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
	auto &image = static_cast<FFTTexture *>(texture)->image->get_image();
	Vulkan::BufferCreateInfo info = {};
	info.size =
	    Vulkan::TextureFormatLayout::format_block_size(image.get_format(), 0) *
	    image.get_width() * image.get_height();
	info.domain = Vulkan::BufferDomain::CachedHost;
	info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	auto readback = device->create_buffer(info, nullptr);

	auto cmd = device->request_command_buffer();
	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
	             VK_ACCESS_TRANSFER_READ_BIT);
	cmd->copy_image_to_buffer(*readback, image, 0, {}, { image.get_width(), image.get_height(), 1 }, 0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
	             VK_ACCESS_HOST_READ_BIT);

	device->submit(cmd);
	device->wait_idle();

	memcpy(buffer, device->map_host_buffer(*readback, Vulkan::MEMORY_ACCESS_READ_BIT), info.size);
	device->unmap_host_buffer(*readback, Vulkan::MEMORY_ACCESS_READ_BIT);
}

string FFTInterface::load_shader(const char *path)
{
	string str;
	if (!Global::filesystem()->read_file_to_string(Path::join("builtin://shaders/fft", path), str))
		return "";
	return str;
}

GLFFT::CommandBuffer *FFTInterface::request_command_buffer()
{
	auto *cmd = new FFTCommandBuffer(device->request_command_buffer());
	return cmd;
}

void FFTInterface::submit_command_buffer(GLFFT::CommandBuffer *cmd_)
{
	auto *cmd = static_cast<FFTCommandBuffer *>(cmd_);
	assert(cmd->cmd_holder);
	device->submit(cmd->cmd_holder);
	delete cmd;
}

bool FFTInterface::supports_native_fp16()
{
	return device->get_device_features().storage_16bit_features.storageBuffer16BitAccess &&
	       device->get_device_features().float16_int8_features.shaderFloat16;
}

FFTInterface::FFTInterface(Vulkan::Device *device_)
    : device(device_)
{
}

} // namespace Granite
