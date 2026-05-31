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
#include <time.h>

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
#ifdef _MSC_VER
	gmtime_s(&gmt, &t);
#else
	gmtime_r(&t, &gmt);
#endif

	strftime(path, sizeof(path), "granite-post-mortem-%F-%T.txt", &gmt);

	LOGE("Device hung ... Attempting to grab post-mortem data to: %s\n", path);

	FILE *file = fopen(path, "w");
	if (!file)
	{
		LOGE("Failed to open \"%s\", dumping to stderr instead.\n", path);
		file = stderr;
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

	// Need to observe the device lost properly first before we can query fault information.
	auto &table = device->get_device_table();
	VkResult vr = VK_SUCCESS;
	if (device->get_device_features().fault_features_khr.deviceFault ||
		device->get_device_features().fault_features_ext.deviceFault)
		vr = table.vkDeviceWaitIdle(device->get_device());

	if (vr == VK_ERROR_DEVICE_LOST)
	{
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

		if (device->get_device_features().fault_features_khr.deviceFault)
		{
			std::vector<VkDeviceFaultInfoKHR> faults;
			uint32_t count;

			if (table.vkGetDeviceFaultReportsKHR(device->get_device(), UINT64_MAX, &count, nullptr) != VK_SUCCESS)
			{
				fprintf(file, "Failed to get fault reports.\n");
				return;
			}

			faults.resize(count);
			for (auto &fault : faults)
				fault.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR;

			if (table.vkGetDeviceFaultReportsKHR(device->get_device(), UINT64_MAX, &count, faults.data()) != VK_SUCCESS)
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
		else
		{
			VkDeviceFaultCountsEXT counts = { VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT };
			VkDeviceFaultInfoEXT fault = { VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT };

			if (table.vkGetDeviceFaultInfoEXT(device->get_device(), &counts, nullptr) != VK_SUCCESS)
			{
				fprintf(file, "Failed to get fault reports.\n");
				return;
			}

			std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
			std::vector<VkDeviceFaultVendorInfoEXT> vendor_infos(counts.vendorInfoCount);
			uint8_t *vendor_data = counts.vendorBinarySize ? new uint8_t[counts.vendorBinarySize] : nullptr;

			fault.pAddressInfos = addresses.data();
			fault.pVendorInfos = vendor_infos.data();
			fault.pVendorBinaryData = vendor_data;

			if (table.vkGetDeviceFaultInfoEXT(device->get_device(), &counts, &fault) != VK_SUCCESS)
			{
				fprintf(file, "Failed to get fault reports.\n");
				return;
			}

			for (uint32_t i = 0; i < counts.addressInfoCount; i++)
				report_address("Memory", addresses[i]);
			for (uint32_t i = 0; i < counts.vendorInfoCount; i++)
				report_vendor(vendor_infos[i]);

			delete[] vendor_data;
		}
	}

	fprintf(file, "... DONE\n");

	if (file != stderr)
		fclose(file);

	LOGE("Completed post-mortem analysis, will crash now.\n");
	std::terminate();
}
}
