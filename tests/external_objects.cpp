#include "context.hpp"
#include "device.hpp"
#include "logging.hpp"

#ifndef _WIN32
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace Vulkan;

static void close_native_handle(ExternalHandle::NativeHandle handle)
{
#ifdef _WIN32
	::CloseHandle(handle);
#else
	::close(handle);
#endif
}

static bool run_test(Device &producer, Device &consumer)
{
	BufferHandle readback_buffer;
	BufferHandle write_buffer;
	BufferHandle read_buffer;
	ImageHandle write_image;
	ImageHandle read_image;

	Semaphore write_timeline;
	Semaphore read_timeline;

	BufferCreateInfo info = {};
	info.size = 2 * 1024 * sizeof(uint32_t);
	info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	info.domain = BufferDomain::CachedHost;
	readback_buffer = consumer.create_buffer(info);
	info.size = sizeof(uint32_t);
	info.domain = BufferDomain::Device;
	info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	info.misc = BUFFER_MISC_EXTERNAL_MEMORY_BIT;
	write_buffer = producer.create_buffer(info);

	// OPAQUE timelines are not supported on AMD Windows <_<.
	// Fallback to binary if we have to.

	write_timeline = producer.request_semaphore_external(
		VK_SEMAPHORE_TYPE_TIMELINE,
		ExternalHandle::get_opaque_semaphore_handle_type());

	if (write_timeline)
	{
		auto write_timeline_handle = write_timeline->export_to_handle();
		if (write_timeline_handle)
		{
			// No reason for this to fail if we can export timeline ...
			read_timeline = consumer.request_semaphore_external(
			    VK_SEMAPHORE_TYPE_TIMELINE,
			    ExternalHandle::get_opaque_semaphore_handle_type());

			if (!read_timeline)
			{
				LOGE("Failed to create external timeline.\n");
				return false;
			}

			if (!read_timeline->import_from_handle(write_timeline_handle))
			{
				LOGE("Failed to import timeline.\n");
				return false;
			}
		}
		else
			write_timeline.reset();
	}

	if (!write_timeline)
		LOGW("External timelines not supported on this driver. Falling back to BINARY.\n");

	ImageCreateInfo image_info = ImageCreateInfo::immutable_2d_image(1, 1, VK_FORMAT_R32_UINT);
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.misc = IMAGE_MISC_EXTERNAL_MEMORY_BIT;
	write_image = producer.create_image(image_info);

	if (!write_buffer)
	{
		LOGE("Failed to create buffer.\n");
		return false;
	}

	if (!write_image)
	{
		LOGE("Failed to create image.\n");
		return false;
	}

	ExternalHandle write_buffer_export = write_buffer->export_handle();
	ExternalHandle write_image_export = write_image->export_handle();

	if (!write_buffer_export)
	{
		LOGE("Failed to export buffer memory.\n");
		return false;
	}

	if (!write_image_export)
	{
		LOGE("Failed to export image memory.\n");
		return false;
	}

	info.external = write_buffer_export;
	image_info.external = write_image_export;
	read_buffer = consumer.create_buffer(info);
	read_image = consumer.create_image(image_info);

	if (!read_buffer)
	{
		LOGE("Failed to create buffer.\n");
		return false;
	}

	if (!read_image)
	{
		LOGE("Failed to create image.\n");
		return false;
	}

	// Ping-pong between queues using external semaphore handles and external memory.
	// Same device, need external memory to test further.

	for (uint32_t i = 0; i < 1024; i++)
	{
		VkClearValue clear_value = {};
		clear_value.color.uint32[0] = i;

		// Produce
		auto fill_cmd = producer.request_command_buffer();
		fill_cmd->fill_buffer(*write_buffer, i, 0, sizeof(uint32_t));
		fill_cmd->image_barrier(*write_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
		fill_cmd->clear_image(*write_image, clear_value);
		fill_cmd->release_buffer_barrier(*write_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
		fill_cmd->release_image_barrier(*write_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
		producer.submit(fill_cmd);
		auto external = producer.request_semaphore_external(
		    VK_SEMAPHORE_TYPE_BINARY, ExternalHandle::get_opaque_semaphore_handle_type());
		producer.submit_empty(CommandBuffer::Type::Generic, nullptr, external.get());

		ExternalHandle handle = external->export_to_handle();
		if (!handle)
			break;

		// Consume
		auto import = consumer.request_semaphore_external(
			VK_SEMAPHORE_TYPE_BINARY, ExternalHandle::get_opaque_semaphore_handle_type());
		if (!import->import_from_handle(handle))
		{
			close_native_handle(handle.handle);
			break;
		}

		consumer.add_wait_semaphore(CommandBuffer::Type::AsyncTransfer, import, VK_PIPELINE_STAGE_TRANSFER_BIT, true);
		auto copy_cmd = consumer.request_command_buffer(CommandBuffer::Type::AsyncTransfer);
		const VkBufferCopy copy = { 0, 2 * i * sizeof(uint32_t), sizeof(uint32_t) };
		copy_cmd->acquire_buffer_barrier(*read_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
		copy_cmd->acquire_image_barrier(*read_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
		copy_cmd->copy_buffer(*readback_buffer, *read_buffer, &copy, 1);
		copy_cmd->copy_image_to_buffer(*readback_buffer, *read_image,
		                               (2 * i + 1) * sizeof(uint32_t), {}, {1, 1, 1}, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
		consumer.submit(copy_cmd);

		if (write_timeline && read_timeline)
		{
			auto consumer_done = consumer.request_timeline_semaphore_as_binary(*read_timeline, i + 1);
			consumer.submit_empty(CommandBuffer::Type::AsyncTransfer, nullptr, consumer_done.get());

			// Avoid WAR hazard.
			auto producer_begin = producer.request_timeline_semaphore_as_binary(*write_timeline, i + 1);
			producer_begin->signal_external();
			producer.add_wait_semaphore(CommandBuffer::Type::Generic, std::move(producer_begin),
			                            VK_PIPELINE_STAGE_TRANSFER_BIT, true);
		}
		else
		{
			// Binary path.

			external = consumer.request_semaphore_external(
				VK_SEMAPHORE_TYPE_BINARY, ExternalHandle::get_opaque_semaphore_handle_type());
			consumer.submit_empty(CommandBuffer::Type::AsyncTransfer, nullptr, external.get());

			handle = external->export_to_handle();
			if (!handle)
				break;

			import = producer.request_semaphore_external(
			    VK_SEMAPHORE_TYPE_BINARY, ExternalHandle::get_opaque_semaphore_handle_type());
			if (!import->import_from_handle(handle))
			{
				close_native_handle(handle.handle);
				break;
			}
			producer.add_wait_semaphore(CommandBuffer::Type::Generic, std::move(import),
			                            VK_PIPELINE_STAGE_TRANSFER_BIT, true);
		}

		producer.next_frame_context();
		consumer.next_frame_context();
	}

	producer.wait_idle();
	consumer.wait_idle();
	auto *ptr = static_cast<const uint32_t *>(consumer.map_host_buffer(*readback_buffer, MEMORY_ACCESS_READ_BIT));

	for (uint32_t i = 0; i < 1024; i++)
	{
		if (ptr[2 * i + 0] != i)
		{
			LOGE("Buffer: expected %u, got %u.\n", i, ptr[2 * i + 0]);
			return false;
		}

		if (ptr[2 * i + 1] != i)
		{
			LOGE("Image: expected %u, got %u.\n", i, ptr[2 * i + 1]);
			return false;
		}
	}

	LOGI("Success!\n");
	return true;
}

int main()
{
	if (!Context::init_loader(nullptr))
	{
		LOGE("Failed.\n");
		return EXIT_FAILURE;
	}

	Context ctx_producer;
	Context ctx_consumer;
	if (!ctx_producer.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;
	if (!ctx_consumer.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;

	Device dev_producer;
	Device dev_consumer;
	dev_producer.set_context(ctx_producer);
	dev_consumer.set_context(ctx_consumer);
	if (!run_test(dev_producer, dev_consumer))
		return EXIT_FAILURE;
}
