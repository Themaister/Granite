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

#include "breadcrumbs.hpp"
#include "shader.hpp"
#include "device.hpp"
#include "timer.hpp"
#include <time.h>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Vulkan
{
void CheckpointString::report(FILE *file)
{
	fprintf(file, "%s\n", str.c_str());
}

void CheckpointDispatch::report(FILE *file)
{
	fprintf(file, "Dispatch (%u, %u, %u)\n", x, y, z);
}

void CheckpointDraw::report(FILE *file)
{
	fprintf(file, "Draw (%u, %u, %d, %u)\n",
	        vertex_count, instance_count, vertex_offset, instance_offset);
}

void CheckpointDrawIndexed::report(FILE *file)
{
	fprintf(file, "DrawIndexed (%u, %u, %u, %d, %u)\n",
	        index_count, instance_count, first_index, vertex_offset, instance_offset);
}

void CheckpointMeshDispatch::report(FILE *file)
{
	fprintf(file, "MeshTasks (%u, %u, %u)\n", x, y, z);
}

void CheckpointIndirectBase::report(FILE *file)
{
	fprintf(file, "%s (#%016llx)\n", tag, static_cast<unsigned long long>(va));
}

void CheckpointMultiIndirectBase::report(FILE *file)
{
	fprintf(file, "%s (#%016llx), count %u, stride %u\n",
			tag,
			static_cast<unsigned long long>(va),
			count, stride);
}

void CheckpointShader::report(FILE *file)
{
	fprintf(file, "Shader (#%016llx)\n", static_cast<unsigned long long>(shader->get_hash()));
}

static void *nv_encode_checkpoint(uint32_t index, uint32_t counter)
{
	return reinterpret_cast<void *>(uintptr_t(index) + uintptr_t(counter) * BreadcrumbsTracker::MaxCommandBuffers);
}

static uint32_t nv_decode_context(void *opaque)
{
	return reinterpret_cast<uintptr_t>(opaque) % BreadcrumbsTracker::MaxCommandBuffers;
}

static uint32_t nv_decode_counter(void *opaque)
{
	return reinterpret_cast<uintptr_t>(opaque) / BreadcrumbsTracker::MaxCommandBuffers;
}

void BreadcrumbsTracker::init(Device *device_)
{
	device = device_;
	if (!device->get_device_features().supports_post_mortem)
		return;

	active = true;

	command_buffers.resize(MaxCommandBuffers);
	vacant_command_buffers.reserve(MaxCommandBuffers);
	for (uint32_t i = MaxCommandBuffers; i; i--)
		vacant_command_buffers.push_back(i - 1);

	if (device->get_device_features().supports_amd_buffer_marker)
	{
		BufferCreateInfo info = {};
		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		info.domain = BufferDomain::DebugReadback;
		info.size = MaxCommandBuffers * sizeof(uint32_t) * 2;
		info.misc = BUFFER_MISC_ZERO_INITIALIZE_BIT;
		amd_marker_buffer = device->create_buffer(info).release();
	}

	blocks.init(CheckpointObjectSize);
}

void BreadcrumbsTracker::deinit()
{
	for (auto &cmd : command_buffers)
		reset_command_buffer(cmd);
	if (amd_marker_buffer)
		amd_marker_buffer->release_reference();
}

BufferMarkerHandle BreadcrumbsTracker::allocate_command_buffer(VkCommandBuffer cmd)
{
	if (!active)
		return {};

	std::lock_guard<std::mutex> holder{lock};

	if (vacant_command_buffers.empty())
		return {};

	BufferMarkerHandle ret = { vacant_command_buffers.back() };
	vacant_command_buffers.pop_back();
	command_buffers[ret.index].cmd = cmd;
	return ret;
}

void BreadcrumbsTracker::free_command_buffer(BufferMarkerHandle handle)
{
	if (handle.index == BufferMarkerHandle::Invalid)
		return;

	std::lock_guard<std::mutex> holder{lock};
	assert(handle.index < MaxCommandBuffers);
	vacant_command_buffers.push_back(handle.index);
	reset_command_buffer(command_buffers[handle.index]);
}

void BreadcrumbsTracker::reset_command_buffer(CommandBuffer &cmd)
{
	for (auto &check : cmd.checkpoints)
	{
		if (check.iface)
		{
			check.iface->~CheckpointReportInterface();
			blocks.free(reinterpret_cast<uint8_t *>(check.iface));
		}
	}

	// Free the memory too to avoid extreme bloat.
	cmd = {};
}

void BreadcrumbsTracker::begin(BufferMarkerHandle handle)
{
	if (handle.index == BufferMarkerHandle::Invalid)
		return;

	auto &cmd = command_buffers[handle.index];

	cmd.counter++;
	cmd.checkpoints.push_back({ nullptr, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, cmd.counter });

	if (device->get_device_features().supports_nv_checkpoints)
	{
		cmd.checkpoints.push_back({ nullptr, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, cmd.counter });
		// A checkpoint is implicitly a top and a bottom marker.
		device->get_device_table().vkCmdSetCheckpointNV(cmd.cmd, nv_encode_checkpoint(handle.index, cmd.counter));
	}
	else if (device->get_device_features().supports_amd_buffer_marker)
	{
		device->get_device_table().vkCmdWriteBufferMarkerAMD(cmd.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		                                                     amd_marker_buffer->get_buffer(),
		                                                     (2 * handle.index + 0) * sizeof(uint32_t), cmd.counter);
	}
}

void BreadcrumbsTracker::signal(BufferMarkerHandle handle)
{
	if (handle.index == BufferMarkerHandle::Invalid)
		return;

	auto &cmd = command_buffers[handle.index];

	if (device->get_device_features().supports_nv_checkpoints)
	{
		cmd.counter++;
		cmd.checkpoints.push_back({ nullptr, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, cmd.counter });
		cmd.checkpoints.push_back({ nullptr, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, cmd.counter });
		device->get_device_table().vkCmdSetCheckpointNV(cmd.cmd, nv_encode_checkpoint(handle.index, cmd.counter));
	}
	else if (device->get_device_features().supports_amd_buffer_marker)
	{
		cmd.checkpoints.push_back({ nullptr, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, cmd.counter });
		device->get_device_table().vkCmdWriteBufferMarkerAMD(cmd.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
															amd_marker_buffer->get_buffer(),
															(2 * handle.index + 1) * sizeof(uint32_t), cmd.counter);

		cmd.counter++;
		cmd.checkpoints.push_back({ nullptr, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, cmd.counter });
		device->get_device_table().vkCmdWriteBufferMarkerAMD(cmd.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		                                                     amd_marker_buffer->get_buffer(),
		                                                     (2 * handle.index + 0) * sizeof(uint32_t), cmd.counter);
	}
}

void BreadcrumbsTracker::end(BufferMarkerHandle handle)
{
	if (handle.index == BufferMarkerHandle::Invalid)
		return;

	auto &cmd = command_buffers[handle.index];
	cmd.counter = UINT32_MAX;

	if (device->get_device_features().supports_nv_checkpoints)
	{
		device->get_device_table().vkCmdSetCheckpointNV(cmd.cmd, nv_encode_checkpoint(handle.index, cmd.counter));
	}
	else if (device->get_device_features().supports_amd_buffer_marker)
	{
		device->get_device_table().vkCmdWriteBufferMarkerAMD(cmd.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		                                                     amd_marker_buffer->get_buffer(),
		                                                     (2 * handle.index + 0) * sizeof(uint32_t), cmd.counter);

		device->get_device_table().vkCmdWriteBufferMarkerAMD(cmd.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		                                                     amd_marker_buffer->get_buffer(),
		                                                     (2 * handle.index + 1) * sizeof(uint32_t), cmd.counter);
	}

	cmd.checkpoints.push_back({ nullptr, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, cmd.counter });
	cmd.checkpoints.push_back({ nullptr, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, cmd.counter });

	// Avoid breadcrumbs spilling between command buffers.
	VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	VkMemoryBarrier2 bar = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
	dep.memoryBarrierCount = 1;
	dep.pMemoryBarriers = &bar;
	bar.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	bar.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	bar.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	bar.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
	device->get_device_table().vkCmdPipelineBarrier2(cmd.cmd, &dep);
}

void BreadcrumbsTracker::report_command_list(FILE *file, CommandBuffer &cmd, uint32_t top_marker, uint32_t bottom_marker)
{
	bool observed_begin_cmd = false;
	bool observed_end_cmd = false;

	fprintf(file, "\n=== Command Buffer ===\n");

	if (bottom_marker == 0)
	{
		fprintf(file, "=== Crash region BEGIN ===\n");
		observed_begin_cmd = true;
	}

	for (auto &check : cmd.checkpoints)
	{
		if (!observed_end_cmd && check.stages == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT && check.counter > top_marker)
		{
			// The command processor did not reach this checkpoint. Any command after this point cannot be the culprit.
			fprintf(file, "=== Crash region END ===\n");
			observed_end_cmd = true;
		}

		if (check.iface)
			check.iface->report(file);

		if (!observed_begin_cmd && check.stages == VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT && check.counter == bottom_marker)
		{
			// The GPU completed all commands up to this point and is the last counter that was completed.
			// Crash must be after this point.
			fprintf(file, "=== Crash region BEGIN ===\n");
			observed_begin_cmd = true;
		}
	}

	if (top_marker == UINT32_MAX)
		fprintf(file, "=== Crash region END ===\n");

	fprintf(file, "====================\n");
}

void BreadcrumbsTracker::report_command_list_amd(FILE *file, uint32_t index)
{
	auto &cmd = command_buffers[index];

	// Unused, cannot be the culprit.
	if (cmd.counter == 0)
		return;

	auto *ptr = static_cast<const uint32_t *>(device->map_host_buffer(*amd_marker_buffer, MEMORY_ACCESS_READ_BIT, 2 * sizeof(uint32_t) * index, sizeof(uint32_t) * 2));
	uint32_t top_marker = ptr[0];
	uint32_t bottom_marker = ptr[1];

	// The command buffer is done executing.
	if (top_marker == UINT32_MAX && bottom_marker == UINT32_MAX)
		return;

	// Never started executing properly. Cannot be a culprit.
	if (top_marker == 0 && bottom_marker == 0)
		return;

	// Edge case where we crashed before the first command of a recycled command buffer completed.
	if (top_marker > 0 && bottom_marker == UINT32_MAX)
		bottom_marker = 0;

	fprintf(file, "Reporting for command index %u, top marker %u, bottom marker %u\n", index, top_marker, bottom_marker);
	report_command_list(file, cmd, top_marker, bottom_marker);
	reported = true;
}

void BreadcrumbsTracker::poll_device_faults(FILE *file, uint64_t timeout)
{
	if (!device)
		return;

	// Need to observe the device lost properly first before we can query fault information.
	auto &table = device->get_device_table();

	const auto addr_type_to_str = [](VkDeviceFaultAddressTypeKHR type)
	{
		switch (type)
		{
		case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_KHR: return "None";
		case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_KHR: return "ReadInvalid";
		case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_KHR: return "WriteInvalid";
		case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_KHR: return "ExecuteInvalid";
		case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_KHR: return "IPUnknown";
		case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_KHR: return "IPInvalid";
		case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_KHR: return "IPFault";
		default: return "???";
		}
	};

	const auto report_address = [&](const char *tag, const VkDeviceFaultAddressInfoKHR &info)
	{
		fprintf(file, "  %s fault: %s\n", tag, addr_type_to_str(info.addressType));
		fprintf(file, "  %s address: #%016llx\n", tag,
				static_cast<unsigned long long>(info.reportedAddress));
		fprintf(file, "  %s precision: #%016llx\n", tag,
				static_cast<unsigned long long>(info.addressPrecision));
	};

	const auto report_vendor = [&](const VkDeviceFaultVendorInfoKHR &info)
	{
		fprintf(file, "  Vendor desc: %s\n", info.description);
		fprintf(file, "  Vendor fault code: %llu\n",
				static_cast<unsigned long long>(info.vendorFaultCode));
		fprintf(file, "  Vendor fault data: %llu\n",
				static_cast<unsigned long long>(info.vendorFaultData));
	};

	if (device->get_device_features().fault_features.deviceFault)
	{
		std::vector<VkDeviceFaultInfoKHR> faults;
		uint32_t count;

		VkResult vr = table.vkGetDeviceFaultReportsKHR(device->get_device(), timeout, &count, nullptr);

		if (vr < 0)
		{
			fprintf(file, "Failed to get fault reports.\n");
			return;
		}

		if (vr > 0)
			return;

		faults.resize(count);
		for (auto &fault : faults)
			fault.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR;

		vr = table.vkGetDeviceFaultReportsKHR(device->get_device(), 0, &count, faults.data());

		if (vr != VK_SUCCESS && vr != VK_INCOMPLETE)
		{
			fprintf(file, "Failed to get fault reports.\n");
			return;
		}

		for (auto &fault : faults)
		{
			fprintf(file, "=== Fault ===\n");
			fprintf(file, "  Desc: %s\n", fault.description);
			fprintf(file, "  groupID: %llu\n", static_cast<unsigned long long>(fault.groupId));

			if (fault.flags & VK_DEVICE_FAULT_FLAG_DEVICE_LOST_KHR)
				fprintf(file, "  Fault caused DEVICE_LOST\n");
			if (fault.flags & VK_DEVICE_FAULT_FLAG_WATCHDOG_TIMEOUT_KHR)
				fprintf(file, "  GPU Timeout\n");
			if (fault.flags & VK_DEVICE_FAULT_FLAG_OVERFLOW_KHR)
				fprintf(file, "  Fault buffer overflowed\n");

			if (fault.flags & VK_DEVICE_FAULT_FLAG_VENDOR_KHR)
				report_vendor(fault.vendorInfo);
			if (fault.flags & VK_DEVICE_FAULT_FLAG_MEMORY_ADDRESS_KHR)
				report_address("Memory", fault.faultAddressInfo);
			if (fault.flags & VK_DEVICE_FAULT_FLAG_INSTRUCTION_ADDRESS_KHR)
				report_address("Instruction ", fault.instructionAddressInfo);
		}
	}
}

// Reused from my vkd3d-proton impl.
static uint64_t align(uint64_t value, uint64_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

template <typename T, bool Hex = false>
static void check_format_string(std::string &formatted_str,
                                const char *shader_fmt_str, const char *msg,
                                size_t &format_offset, size_t format_length,
                                size_t &argument_offset, size_t argument_length)
{
	if (format_offset + strlen(shader_fmt_str) <= format_length &&
	    strncmp(msg + format_offset, shader_fmt_str, strlen(shader_fmt_str)) == 0)
	{
		T arg = 0;
		argument_offset = align(argument_offset, sizeof(T));
		if (argument_offset + sizeof(T) <= argument_length)
			memcpy(&arg, msg + argument_offset, sizeof(T));
		argument_offset += sizeof(T);
		std::stringstream ss;
		if (Hex)
			ss << std::hex;
		ss << arg;
		formatted_str += ss.str();
		format_offset += strlen(shader_fmt_str);
	}
}

static void shader_abort_print_message(FILE *file, const char *msg, size_t length)
{
    const char *term = static_cast<const char *>(memchr(msg, '\0', length));

    if (term && term != msg)
    {
		size_t argument_offset = align(term + 1 - msg, sizeof(uint32_t));
		size_t format_length = term - msg;
		size_t format_offset = 0;
		std::string buf;

		// Very basic formatting support. Enough to report what we care about.
		while (format_offset < format_length)
		{
			if (format_offset + 2 <= format_length && strncmp(msg + format_offset, "%%", 2) == 0)
			{
				buf.push_back('%');
				format_offset += 2;
			}

			check_format_string<uint32_t>(buf, "%u", msg, format_offset, format_length, argument_offset, length);
			check_format_string<uint32_t, true>(buf, "%x", msg, format_offset, format_length, argument_offset, length);
			check_format_string<int32_t>(buf, "%x", msg, format_offset, format_length, argument_offset, length);
			check_format_string<uint64_t>(buf, "%lu", msg, format_offset, format_length, argument_offset, length);
			check_format_string<uint64_t, true>(buf, "%lx", msg, format_offset, format_length, argument_offset, length);
			check_format_string<int64_t>(buf, "%ld", msg, format_offset, format_length, argument_offset, length);
			check_format_string<float>(buf, "%f", msg, format_offset, format_length, argument_offset, length);
			check_format_string<float>(buf, "%g", msg, format_offset, format_length, argument_offset, length);

			if (format_offset < format_length)
				buf.push_back(msg[format_offset++]);
		}

		fprintf(file, "ShaderAbort payload: %s\n", buf.c_str());
    }
    else
    {
        fprintf(file, "Message does not look like a printf.\n");
    }
}

static void shader_abort_print_message_sequence(FILE *file, const uint64_t *tokens, size_t length)
{
    // The buffer is laid out as raw length + payload pairs. Pairs are aligned to 64-bit.
    while (length >= sizeof(uint64_t))
    {
        uint64_t msg_length = tokens[0];
        uint64_t aligned_msg_length;
        length -= sizeof(uint64_t);
        tokens++;

        if (msg_length > length)
        {
            LOGE("Invalid message length.\n");
            return;
        }

        aligned_msg_length = align(msg_length, sizeof(*tokens));

        shader_abort_print_message(file, reinterpret_cast<const char *>(tokens), msg_length);
        tokens += aligned_msg_length / sizeof(*tokens);

        // The total length of buffer doesn't have to be aligned to 8 bytes.
        length -= std::min<uint64_t>(aligned_msg_length, length);
    }
}

void BreadcrumbsTracker::notify_device_hung()
{
	if (!active)
		return;

	std::lock_guard<std::mutex> holder{lock};
	if (reported)
		return;

	char path[256];
	std::time_t t = std::time(nullptr);
	std::tm gmt;

	// Date-time in C was always a great time :')
#ifdef _WIN32
	gmtime_s(&gmt, &t);
#else
	gmtime_r(&t, &gmt);
#endif

	// Windows does not like colons in path names, so %T breaks.
	strftime(path, sizeof(path), "granite-post-mortem-%Y-%m-%d-%H-%M-%S.txt", &gmt);

	LOGE("Device hung ... Attempting to grab post-mortem data to: %s\n", path);

	auto start_time = Util::get_current_time_nsecs();
	auto end_time = start_time + 5ll * 1000 * 1000 * 1000;

	FILE *file = fopen(path, "w");
	if (!file)
	{
		LOGE("Failed to open \"%s\", dumping to stderr instead.\n", path);
		file = stderr;
	}

	// Try to observe device lost properly.
	VkResult vr = VK_SUCCESS;
	while (vr != VK_ERROR_DEVICE_LOST && Util::get_current_time_nsecs() < end_time)
		vr = device->get_device_table().vkDeviceWaitIdle(device->get_device());

	if (vr == VK_ERROR_DEVICE_LOST)
		LOGE("Observed device lost after %.3f seconds of blocking.\n", 1e-9 * (Util::get_current_time_nsecs() - start_time));

	if (vr != VK_ERROR_DEVICE_LOST)
	{
		if (file != stderr)
			LOGE("Cannot observe device lost state, report may be incomplete ...\n");
		fprintf(file, "Cannot observe device lost state, report may be incomplete ...\n");
	}

	fprintf(file, "Post-mortem analysis ...\n");

	if (device->get_device_features().supports_nv_checkpoints)
	{
		auto &queues = device->get_queue_info().queues;
		for (uint32_t i = 0; i < QUEUE_INDEX_COUNT; i++)
		{
			if (queues[i] == VK_NULL_HANDLE || std::find(queues, queues + i, queues[i]) != queues + i)
				continue;

			auto &table = device->get_device_table();
			uint32_t count;
			table.vkGetQueueCheckpointDataNV(queues[i], &count, nullptr);

			if (count == 0)
				continue;

			std::vector<VkCheckpointDataNV> checkpoints(count);
			for (auto &check : checkpoints)
				check.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
			table.vkGetQueueCheckpointDataNV(queues[i], &count, checkpoints.data());

			uint32_t top_marker = 0;
			uint32_t bottom_marker = 0;
			uint32_t top_context = BufferMarkerHandle::Invalid;
			uint32_t bottom_context = BufferMarkerHandle::Invalid;

			for (auto &check : checkpoints)
			{
				uint32_t context = nv_decode_context(check.pCheckpointMarker);
				uint32_t counter = nv_decode_counter(check.pCheckpointMarker);

				if (check.stage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
				{
					top_marker = counter;
					top_context = context;
				}
				else if (check.stage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
				{
					bottom_marker = counter;
					bottom_context = context;
				}
			}

			if (top_context == BufferMarkerHandle::Invalid)
				fprintf(file, "Missing context, this should not happen.\n");
			else if (top_context != bottom_context || top_context == BufferMarkerHandle::Invalid)
				fprintf(file, "Mismatching contexts, this should not happen.\n");
			else
			{
				report_command_list(file, command_buffers[top_context], top_marker, bottom_marker);
				reported = true;
			}
		}
	}
	else if (device->get_device_features().supports_amd_buffer_marker)
	{
		for (uint32_t i = 0; i < MaxCommandBuffers; i++)
			report_command_list_amd(file, i);
	}

	poll_device_faults(file, UINT64_MAX);

	if (device->get_device_features().shader_abort_features.shaderAbort)
	{
		VkDeviceFaultShaderAbortMessageInfoKHR message_info = { VK_STRUCTURE_TYPE_DEVICE_FAULT_SHADER_ABORT_MESSAGE_INFO_KHR };
		VkDeviceFaultDebugInfoKHR vendor_info = { VK_STRUCTURE_TYPE_DEVICE_FAULT_DEBUG_INFO_KHR, &message_info };
		if (device->get_device_table().vkGetDeviceFaultDebugInfoKHR(device->get_device(), &vendor_info) == VK_SUCCESS &&
			message_info.messageDataSize)
		{
			std::unique_ptr<uint64_t []> message_data(new uint64_t[align(message_info.messageDataSize, 8) / 8]);
			message_info.pMessageData = message_data.get();
			if (device->get_device_table().vkGetDeviceFaultDebugInfoKHR(device->get_device(), &vendor_info) == VK_SUCCESS)
				shader_abort_print_message_sequence(file, message_data.get(), message_info.messageDataSize);
		}
	}

	if (device->get_device_features().fault_features.deviceFaultVendorBinary)
	{
		VkDeviceFaultDebugInfoKHR vendor_info = { VK_STRUCTURE_TYPE_DEVICE_FAULT_DEBUG_INFO_KHR };
		if (device->get_device_table().vkGetDeviceFaultDebugInfoKHR(device->get_device(), &vendor_info) == VK_SUCCESS &&
			vendor_info.vendorBinarySize)
		{
			std::unique_ptr<char []> vendor_data(new char[vendor_info.vendorBinarySize]);
			vendor_info.pVendorBinaryData = vendor_data.get();

			if (device->get_device_table().vkGetDeviceFaultDebugInfoKHR(device->get_device(), &vendor_info) == VK_SUCCESS)
			{
				if (vendor_info.vendorBinarySize >= sizeof(VkDeviceFaultVendorBinaryHeaderVersionOneKHR))
				{
					const auto *header = static_cast<const VkDeviceFaultVendorBinaryHeaderVersionOneKHR *>(
						vendor_info.pVendorBinaryData);

					if (header->headerVersion == VK_DEVICE_FAULT_VENDOR_BINARY_HEADER_VERSION_ONE_KHR &&
						header->headerSize <= vendor_info.vendorBinarySize)
					{
						fprintf(file, "\n\nVENDOR BINARY DUMP follows newline\n");
						fprintf(file, "vendorID: #%x\n", header->vendorID);
						fprintf(file, "driverVersion: #%x\n", header->driverVersion);
						fprintf(file, "deviceID: #%x\n", header->deviceID);
						fprintf(file, "apiVersion: #%x\n", header->apiVersion);

						if (header->applicationNameOffset)
						{
							fprintf(file, "applicationName: %s\n", vendor_data.get() + header->applicationNameOffset);
							fprintf(file, "applicationVersion: #%x\n", header->applicationVersion);
						}

						if (header->engineNameOffset)
						{
							fprintf(file, "engineName: %s\n", vendor_data.get() + header->engineNameOffset);
							fprintf(file, "engineVersion: #%x\n", header->engineVersion);
						}

						char cache_uuid[2 * VK_UUID_SIZE + 1];
						for (unsigned i = 0; i < VK_UUID_SIZE; i++)
							sprintf(cache_uuid + i * 2, "%02x", header->pipelineCacheUUID[i]);
						fprintf(file, "pipelineCacheUUID: %s\n", cache_uuid);

						size_t write_size = vendor_info.vendorBinarySize - header->headerSize;
						if (fwrite(vendor_data.get() + header->headerSize, 1, write_size, file) != write_size)
							LOGE("Failed to write fault file.\n");
					}
				}
			}
		}
	}

	fprintf(file, "... DONE\n");

	if (file != stderr)
		fclose(file);

	LOGE("Completed post-mortem analysis, will crash now.\n");
#ifdef _WIN32
	char msg[512];
	snprintf(msg, sizeof(msg), "GPU crashed, see post-mortem log in %s. Application will now terminate.", path);
	MessageBoxA(nullptr, msg, "Granite Post Mortem", MB_OK);
	TerminateProcess(GetCurrentProcess(), 1);
#endif
	std::terminate();
}
}
