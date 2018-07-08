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

#include "glfft_interface.hpp"
#include "command_buffer.hpp"

namespace Granite
{
class FFTInterface : public GLFFT::Context
{
public:
	FFTInterface(Vulkan::Device &device);

	std::unique_ptr<GLFFT::Texture> create_texture(const void *initial_data,
	                                               unsigned width, unsigned height,
	                                               GLFFT::Format format) override;

	std::unique_ptr<GLFFT::Buffer> create_buffer(const void *initial_data,
	                                             size_t size,
	                                             GLFFT::AccessMode access) override;

	std::unique_ptr<GLFFT::Program> compile_compute_shader(const char *source) override;

	GLFFT::CommandBuffer *request_command_buffer() override;
	void submit_command_buffer(GLFFT::CommandBuffer *cmd) override;
	void wait_idle() override;

	const char *get_renderer_string() override;
	void log(const char *fmt, ...) override;
	double get_time() override;

	unsigned get_max_work_group_threads() override;

	const void *map(GLFFT::Buffer *buffer, size_t offset, size_t size) override;
	void unmap(GLFFT::Buffer *buffer) override;

	bool supports_texture_readback() override;
	void read_texture(void *buffer, GLFFT::Texture *texture) override;

private:
	Vulkan::Device &device;
};

}