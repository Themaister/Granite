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

#pragma once

#include "slab_allocator.hpp"
#include "vulkan_headers.hpp"
#include "vulkan_common.hpp"
#include <mutex>
#include <assert.h>
#include <stdio.h>

namespace Vulkan
{
class Device;
class Buffer;
class Shader;

struct CheckpointReportInterface
{
	virtual void report(FILE *file) = 0;
	virtual ~CheckpointReportInterface() = default;
};

struct CheckpointString : CheckpointReportInterface
{
	CheckpointString(std::string str_) : str(std::move(str_)) {}
	void report(FILE *file) override;
	std::string str;
};

struct CheckpointDispatch : CheckpointReportInterface
{
	CheckpointDispatch(uint32_t x_, uint32_t y_, uint32_t z_)
		: x(x_), y(y_), z(z_) {}
	void report(FILE *file) override;
	uint32_t x, y, z;
};

struct CheckpointDraw : CheckpointReportInterface
{
	CheckpointDraw(uint32_t vertex_count_, uint32_t instance_count_, int32_t vertex_offset_, uint32_t instance_offset_)
		: vertex_count(vertex_count_), instance_count(instance_count_)
		, vertex_offset(vertex_offset_), instance_offset(instance_offset_) {}

	void report(FILE *file) override;
	uint32_t vertex_count;
	uint32_t instance_count;
	int32_t vertex_offset;
	uint32_t instance_offset;
};

struct CheckpointDrawIndexed : CheckpointReportInterface
{
	CheckpointDrawIndexed(uint32_t index_count_, uint32_t instance_count_, uint32_t first_index_, int32_t vertex_offset_, uint32_t instance_offset_)
		: index_count(index_count_), instance_count(instance_count_), first_index(first_index_)
		, vertex_offset(vertex_offset_), instance_offset(instance_offset_) {}

	void report(FILE *file) override;
	uint32_t index_count;
	uint32_t instance_count;
	uint32_t first_index;
	int32_t vertex_offset;
	uint32_t instance_offset;
};

struct CheckpointMeshDispatch : CheckpointReportInterface
{
	CheckpointMeshDispatch(uint32_t x_, uint32_t y_, uint32_t z_)
		: x(x_), y(y_), z(z_) {}
	void report(FILE *file) override;
	uint32_t x, y, z;
};

struct CheckpointIndirectBase : CheckpointReportInterface
{
	CheckpointIndirectBase(const char *tag_, VkDeviceAddress va_) : tag(tag_), va(va_) {}

	void report(FILE *file) override;

	const char *tag;
	VkDeviceAddress va;
};

struct CheckpointMultiIndirectBase : CheckpointReportInterface
{
	CheckpointMultiIndirectBase(const char *tag_, VkDeviceAddress va_,
		uint32_t count_, uint32_t stride_)
		: tag(tag_), va(va_), count(count_), stride(stride_) {}

	void report(FILE *file) override;

	const char *tag;
	VkDeviceAddress va;
	uint32_t count;
	uint32_t stride;
};

struct CheckpointMultiIndirectCountBase : CheckpointReportInterface
{
	CheckpointMultiIndirectCountBase(const char *tag_, VkDeviceAddress va_, VkDeviceAddress count_va_,
		uint32_t count_, uint32_t stride_)
		: tag(tag_), va(va_), count_va(count_va_), count(count_), stride(stride_) {}

	void report(FILE *file) override
	{
		fprintf(file, "%s (#%016llx), countVA (#%016llx), count %u, stride %u\n",
				tag,
				static_cast<unsigned long long>(va),
				static_cast<unsigned long long>(count_va),
				count, stride);
	}

	const char *tag;
	VkDeviceAddress va;
	VkDeviceAddress count_va;
	uint32_t count;
	uint32_t stride;
};

struct CheckpointShader : CheckpointReportInterface
{
	CheckpointShader(const Shader *shader_) : shader(shader_) {}
	void report(FILE *file) override;
	const Shader *shader;
};

// 1 second.
// It seems like if we have too long timeout, NV driver on Windows is broken
// and loses checkpoint information (?!?!).
// It seems like we have to call it before we start getting vkQueueSubmit() device losts,
// then it's too late.
static constexpr uint64_t PostMortemTimeout = 1ull * 1000 * 1000 * 1000;

class BreadcrumbsTracker
{
public:
	enum { CheckpointObjectSize = 64, MaxCommandBuffers = 8 * 1024 };
	void init(Device *device);
	void deinit();
	BufferMarkerHandle allocate_command_buffer(VkCommandBuffer cmd);
	void free_command_buffer(BufferMarkerHandle handle);

	void begin(BufferMarkerHandle handle);

	template <typename T, typename... Ts>
	void checkpoint(BufferMarkerHandle handle, Ts &&... ts)
	{
		if (!active)
			return;

		static_assert(sizeof(T) <= CheckpointObjectSize, "Object size is too large.");
		auto *raw = reinterpret_cast<T *>(blocks.allocate());
		new (raw) T(std::forward<Ts>(ts)...);

		Checkpoint checkpoint = {};
		checkpoint.iface = raw;
		assert(handle.index < command_buffers.size());
		command_buffers[handle.index].checkpoints.push_back(checkpoint);
	}

	template <typename T, typename... Ts>
	void checkpoint_with_signal(BufferMarkerHandle handle, Ts &&... ts)
	{
		checkpoint<T>(handle, std::forward<Ts>(ts)...);
		signal(handle);
	}

	void signal(BufferMarkerHandle handle);
	void end(BufferMarkerHandle handle);

	void notify_device_hung();

private:
	Device *device = nullptr;
	bool active = false;
	Buffer *amd_marker_buffer = nullptr;
	std::mutex lock;
	bool reported = false;

	struct Checkpoint
	{
		CheckpointReportInterface *iface;
		VkPipelineStageFlags2 stages;
		uint32_t counter;
	};

	struct CommandBuffer
	{
		std::vector<Checkpoint> checkpoints;
		uint32_t counter = 0;
		VkCommandBuffer cmd;
	};

	std::vector<CommandBuffer> command_buffers;
	std::vector<uint32_t> vacant_command_buffers;
	Util::ThreadSafeSlabAllocator blocks;

	void reset_command_buffer(CommandBuffer &cmd);
	void report_command_list_amd(FILE *file, uint32_t index);
	void report_command_list(FILE *file, CommandBuffer &cmd, uint32_t top_marker, uint32_t bottom_marker);
};
}
