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

#include <memory>
#include <string>

namespace GLFFT
{
class Context;

class Resource
{
public:
	virtual ~Resource() = default;

	// Non-movable, non-copyable to make things simpler.
	Resource(Resource &&) = delete;
	void operator=(const Resource &) = delete;

protected:
	Resource() = default;
};

class Texture : public Resource
{
};
class Sampler : public Resource
{
};
class Buffer : public Resource
{
};

class Program
{
public:
	virtual ~Program() = default;

protected:
	friend class Context;
	Program() = default;
};

enum AccessMode
{
	AccessStreamCopy,
	AccessStaticCopy,
	AccessStreamRead
};

enum Format
{
	FormatUnknown,
	FormatR16Float,
	FormatR16G16Float,
	FormatR16G16B16A16Float,
	FormatR32Float,
	FormatR32G32Float,
	FormatR32G32B32A32Float,
};

class CommandBuffer;

class Context
{
public:
	virtual ~Context() = default;

	virtual std::unique_ptr<Texture> create_texture(const void *initial_data, unsigned width, unsigned height,
	                                                Format format) = 0;

	virtual std::unique_ptr<Buffer> create_buffer(const void *initial_data, size_t size, AccessMode access) = 0;
	virtual std::unique_ptr<Program> compile_compute_shader(const char *source) = 0;

	virtual CommandBuffer *request_command_buffer() = 0;
	virtual void submit_command_buffer(CommandBuffer *cmd) = 0;
	virtual void wait_idle() = 0;

	virtual uint32_t get_vendor_id() = 0;
	virtual uint32_t get_product_id() = 0;
	virtual void log(const char *fmt, ...) = 0;
	virtual double get_time() = 0;

	virtual unsigned get_max_work_group_threads() = 0;
	virtual unsigned get_max_shared_memory_size() = 0;

	virtual const void *map(Buffer *buffer, size_t offset, size_t size) = 0;
	virtual void unmap(Buffer *buffer) = 0;

	virtual bool supports_texture_readback() = 0;
	virtual void read_texture(void *buffer, Texture *texture) = 0;

	virtual std::string load_shader(const char *path) = 0;

	virtual bool supports_native_fp16() = 0;

protected:
	Context() = default;
};

class CommandBuffer
{
public:
	virtual ~CommandBuffer() = default;

	virtual void bind_program(Program *program) = 0;
	virtual void bind_storage_texture(unsigned binding, Texture *texture) = 0;
	virtual void bind_texture(unsigned binding, Texture *texture) = 0;
	virtual void bind_sampler(unsigned binding, Sampler *sampler) = 0;
	virtual void bind_storage_buffer(unsigned binding, Buffer *texture) = 0;
	virtual void bind_storage_buffer_range(unsigned binding, size_t offset, size_t length, Buffer *texture) = 0;
	virtual void dispatch(unsigned x, unsigned y, unsigned z) = 0;
	virtual void barrier() = 0;

	enum
	{
		MaxConstantDataSize = 64
	};
	virtual void push_constant_data(const void *data, size_t size) = 0;

protected:
	CommandBuffer() = default;
};
} // namespace GLFFT
