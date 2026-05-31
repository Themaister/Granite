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
#include "logging.hpp"
#include "vulkan_headers.hpp"
#include "vulkan_common.hpp"
#include <mutex>
#include <assert.h>

namespace Vulkan
{
class Device;
class Buffer;

struct CheckpointReportInterface
{
	virtual void report() = 0;
	virtual ~CheckpointReportInterface() = default;
};

struct CheckpointString : CheckpointReportInterface
{
	CheckpointString(std::string str_) : str(std::move(str_)) {}
	void report() override { LOGE("%s\n", str.c_str()); }
	std::string str;
};

class BreadcrumbsTracker
{
public:
	enum { CheckpointObjectSize = 64, MaxCommandBuffers = 8 * 1024 };
	void init(Device *device);
	~BreadcrumbsTracker();
	BufferMarkerHandle allocate_command_buffer(VkCommandBuffer cmd);
	void free_command_buffer(BufferMarkerHandle handle);

	void begin(BufferMarkerHandle handle);

	template <typename T, typename... Ts>
	void checkpoint(BufferMarkerHandle handle, Ts &&... ts)
	{
		if (!active)
			return;

		static_assert(sizeof(T) <= CheckpointObjectSize, "Object size is too large.");
		auto *raw = static_cast<T *>(blocks.allocate());
		new (raw) T(std::forward<Ts>(ts)...);

		Checkpoint checkpoint = {};
		checkpoint.iface = raw;
		assert(handle.index < command_buffers.size());
		command_buffers[handle.index].checkpoints.push_back(checkpoint);

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
	void report_command_list_amd(uint32_t index);
	void report_command_list(CommandBuffer &cmd, uint32_t top_marker, uint32_t bottom_marker);
};
}
