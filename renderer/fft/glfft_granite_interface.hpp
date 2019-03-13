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

#pragma once

#include "command_buffer.hpp"
#include "glfft_interface.hpp"
#include <functional>

namespace Granite
{
class FFTInterface : public GLFFT::Context
{
public:
	FFTInterface(Vulkan::Device *device);
	FFTInterface() = default;

	std::unique_ptr<GLFFT::Texture> create_texture(const void *initial_data, unsigned width, unsigned height,
	                                               GLFFT::Format format) override;

	std::unique_ptr<GLFFT::Buffer> create_buffer(const void *initial_data, size_t size,
	                                             GLFFT::AccessMode access) override;

	std::unique_ptr<GLFFT::Program> compile_compute_shader(const char *source) override;

	GLFFT::CommandBuffer *request_command_buffer() override;
	void submit_command_buffer(GLFFT::CommandBuffer *cmd) override;
	void wait_idle() override;

	uint32_t get_vendor_id() override;
	uint32_t get_product_id() override;
	void log(const char *fmt, ...) override;
	double get_time() override;

	unsigned get_max_work_group_threads() override;
	unsigned get_max_shared_memory_size() override;

	const void *map(GLFFT::Buffer *buffer, size_t offset, size_t size) override;
	void unmap(GLFFT::Buffer *buffer) override;

	bool supports_texture_readback() override;
	void read_texture(void *buffer, GLFFT::Texture *texture) override;

	std::string load_shader(const char *path) override;

	bool supports_native_fp16() override;

private:
	Vulkan::Device *device = nullptr;
};

class FFTCommandBuffer : public GLFFT::CommandBuffer
{
public:
	friend class FFTInterface;
	FFTCommandBuffer(Vulkan::CommandBufferHandle cmd_)
	    : cmd_holder(std::move(cmd_))
	{
		cmd = cmd_holder.get();
	}

	FFTCommandBuffer(Vulkan::CommandBuffer *cmd_)
	    : cmd(cmd_)
	{
	}

	void barrier() override;
	void bind_program(GLFFT::Program *program) override;
	void bind_sampler(unsigned binding, GLFFT::Sampler *sampler) override;
	void bind_storage_texture(unsigned binding, GLFFT::Texture *texture) override;
	void bind_texture(unsigned binding, GLFFT::Texture *texture) override;
	void bind_storage_buffer(unsigned binding, GLFFT::Buffer *buffer) override;
	void bind_storage_buffer_range(unsigned binding, size_t offset, size_t length, GLFFT::Buffer *buffer) override;
	void dispatch(unsigned x, unsigned y, unsigned z) override;
	void push_constant_data(const void *data, size_t size) override;

private:
	Vulkan::CommandBuffer *cmd;
	Vulkan::CommandBufferHandle cmd_holder;
};

class FFTDeferredCommandBuffer : public GLFFT::CommandBuffer
{
public:
	void barrier() override;
	void bind_program(GLFFT::Program *program) override;
	void bind_sampler(unsigned binding, GLFFT::Sampler *sampler) override;
	void bind_storage_texture(unsigned binding, GLFFT::Texture *texture) override;
	void bind_texture(unsigned binding, GLFFT::Texture *texture) override;
	void bind_storage_buffer(unsigned binding, GLFFT::Buffer *buffer) override;
	void bind_storage_buffer_range(unsigned binding, size_t offset, size_t length, GLFFT::Buffer *buffer) override;
	void dispatch(unsigned x, unsigned y, unsigned z) override;
	void push_constant_data(const void *data, size_t size) override;

	void build(Vulkan::CommandBuffer &cmd);
	void reset_command_counter();
	void reset();

private:
	unsigned command_counter = 0;
	std::vector<std::vector<std::function<void (Vulkan::CommandBuffer &)>>> commands;
	void ensure_command_list();
	std::vector<std::function<void (Vulkan::CommandBuffer &)>> &get_command_list();
};

struct FFTBuffer : GLFFT::Buffer
{
	FFTBuffer(Vulkan::Buffer *handle)
		: buffer(handle)
	{
	}

	FFTBuffer(Vulkan::BufferHandle handle)
		: buffer_holder(std::move(handle))
	{
		buffer = buffer_holder.get();
	}

	Vulkan::Buffer *buffer;
	Vulkan::BufferHandle buffer_holder;
};

struct FFTTexture : GLFFT::Texture
{
	FFTTexture(Vulkan::ImageView *handle)
		: image(handle)
	{
	}

	FFTTexture(Vulkan::ImageHandle handle)
		: image_holder(std::move(handle))
	{
		image = &image_holder->get_view();
	}

	Vulkan::ImageView *image;
	Vulkan::ImageHandle image_holder;
};

} // namespace Granite