#include <unistd.h>

#include "context.hpp"
#include "device.hpp"
#include "logging.hpp"

using namespace Vulkan;

struct ExternalHandles
{
	ExternalHandle image[2];
	ExternalHandle timeline[2];
};

static ExternalHandles create_external_handles()
{
	if (!Context::init_loader(nullptr))
	{
		LOGE("Failed.\n");
		exit(EXIT_FAILURE);
	}

	Context ctx;
	Device device;
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		exit(EXIT_FAILURE);

	device.set_context(ctx);

	auto info = ImageCreateInfo::immutable_2d_image(1024, 1024, VK_FORMAT_R8_UNORM);
	info.misc |= IMAGE_MISC_EXTERNAL_MEMORY_BIT;
	info.external.memory_handle_type = ExternalHandle::get_opaque_memory_handle_type();
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;

	ExternalHandles handles = {};

	for (int i = 0; i < 2; i++)
	{
		auto img = device.create_image(info);
		handles.image[i] = img->export_handle();

		auto sem = device.request_semaphore_external(VK_SEMAPHORE_TYPE_TIMELINE,
		                                             ExternalHandle::get_opaque_semaphore_handle_type());
		handles.timeline[i] = sem->export_to_handle();
	}

	return handles;
}

static void run_parent(const ExternalHandles &handles)
{
	Context ctx;
	Device device;
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		exit(EXIT_FAILURE);

	device.set_context(ctx);

	auto info = ImageCreateInfo::immutable_2d_image(1024, 1024, VK_FORMAT_R8_UNORM);
	info.misc |= IMAGE_MISC_EXTERNAL_MEMORY_BIT;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;

	ImageHandle imgs[2];
	for (int i = 0; i < 2; i++)
	{
		info.external = handles.image[i];
		imgs[i] = device.create_image(info);
	}

	Semaphore sems[2];

	for (int i = 0; i < 2; i++)
	{
		sems[i] = device.request_semaphore_external(VK_SEMAPHORE_TYPE_TIMELINE, ExternalHandle::get_opaque_semaphore_handle_type());
		sems[i]->import_from_handle(handles.timeline[i]);
	}

	uint64_t acquire_value[2] = {};
	uint64_t pending_value[2] = {};

	QueryPoolHandle start_timestamps[4];
	QueryPoolHandle end_timestamps[4];

	for (uint32_t i = 0; i < 4; i++)
	{
		LOGI("Running iteration %u in parent ...\n", i);
		auto image_index = i & 1;

		if (acquire_value[image_index])
		{
			LOGI("Parent index %u: waiting %u\n", image_index, unsigned(acquire_value[image_index]));
			auto binary = device.request_timeline_semaphore_as_binary(*sems[image_index], acquire_value[image_index]);
			binary->signal_external();
			device.add_wait_semaphore(CommandBuffer::Type::Generic, std::move(binary), VK_PIPELINE_STAGE_2_CLEAR_BIT, true);
		}

		auto &img = *imgs[image_index];

		auto cmd = device.request_command_buffer(CommandBuffer::Type::Generic);

		auto start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

		cmd->image_barrier(img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                   VK_PIPELINE_STAGE_2_CLEAR_BIT, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT,
		                   VK_ACCESS_2_TRANSFER_WRITE_BIT);
		// Some dummy work.
		for (uint32_t j = 0; j < 1000; j++)
		{
			cmd->clear_image(img, {});
			cmd->barrier(VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			             VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
		}

		cmd->release_image_barrier(img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                           VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

		auto end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

		start_timestamps[i] = start_ts;
		end_timestamps[i] = end_ts;

		device.register_time_interval("GPU", std::move(start_ts), std::move(end_ts), "producer");
		device.submit(cmd);

		auto binary = device.request_timeline_semaphore_as_binary(*sems[image_index], ++pending_value[image_index]);
		LOGI("Parent index %u: signal %u\n", image_index, unsigned(pending_value[image_index]));
		device.submit_empty(CommandBuffer::Type::Generic, nullptr, binary.get());

		acquire_value[image_index] = ++pending_value[image_index];
		device.next_frame_context();
		LOGI("Submitted iteration %u in parent ...\n", i);
	}

	device.wait_idle();
	for (int i = 0; i < 4; i++)
	{
		LOGI("Range for parent iter %u: [%.3f ms, %.3f ms]\n",  i,
			double(device.convert_timestamp_to_absolute_nsec(*start_timestamps[i])) * 1e-6,
			double(device.convert_timestamp_to_absolute_nsec(*end_timestamps[i])) * 1e-6);
	}
}

static void run_child(const ExternalHandles &handles)
{
	Context ctx;
	Device device;
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		exit(EXIT_FAILURE);

	device.set_context(ctx);

	auto info = ImageCreateInfo::immutable_2d_image(1024, 1024, VK_FORMAT_R8_UNORM);
	info.misc |= IMAGE_MISC_EXTERNAL_MEMORY_BIT;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;

	ImageHandle imgs[2];
	for (int i = 0; i < 2; i++)
	{
		info.external = handles.image[i];
		imgs[i] = device.create_image(info);
	}

	Semaphore sems[2];

	for (int i = 0; i < 2; i++)
	{
		sems[i] = device.request_semaphore_external(VK_SEMAPHORE_TYPE_TIMELINE, ExternalHandle::get_opaque_semaphore_handle_type());
		sems[i]->import_from_handle(handles.timeline[i]);
	}

	uint64_t acquire_value[2] = { 1, 1 };
	uint64_t pending_value[2] = { 1, 1 };

	QueryPoolHandle start_timestamps[4];
	QueryPoolHandle end_timestamps[4];

	for (uint32_t i = 0; i < 4; i++)
	{
		auto image_index = i & 1;
		LOGI("Running iteration %u in child ...\n", i);

		LOGI("Child waiting semaphore index %u: %u\n", image_index, unsigned(acquire_value[image_index]));
		auto binary = device.request_timeline_semaphore_as_binary(*sems[image_index], acquire_value[image_index]);
		binary->signal_external();
		device.add_wait_semaphore(CommandBuffer::Type::AsyncCompute, std::move(binary), VK_PIPELINE_STAGE_2_CLEAR_BIT, true);

		auto &img = *imgs[image_index];

		auto cmd = device.request_command_buffer(CommandBuffer::Type::AsyncCompute);

		auto start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

		cmd->acquire_image_barrier(img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                           VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

		// Some dummy work.
		for (uint32_t j = 0; j < 1000; j++)
		{
			cmd->clear_image(img, {});
			cmd->barrier(VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			             VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
		}

		auto end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
		start_timestamps[i] = start_ts;
		end_timestamps[i] = end_ts;
		device.register_time_interval("GPU", std::move(start_ts), std::move(end_ts), "consumer");
		device.submit(cmd);

		binary = device.request_timeline_semaphore_as_binary(*sems[image_index], ++pending_value[image_index]);
		LOGI("Child signal semaphore index %u to %u\n", image_index, unsigned(pending_value[image_index]));

		device.submit_empty(CommandBuffer::Type::AsyncCompute, nullptr, binary.get());
		acquire_value[image_index] = ++pending_value[image_index];

		LOGI("Submitted iteration %u in child ...\n", i);
		device.next_frame_context();
	}

	device.wait_idle();
	for (int i = 0; i < 4; i++)
	{
		LOGI("Range for child iter %u: [%.3f ms, %.3f ms]\n",  i,
			double(device.convert_timestamp_to_absolute_nsec(*start_timestamps[i])) * 1e-6,
			double(device.convert_timestamp_to_absolute_nsec(*end_timestamps[i])) * 1e-6);
	}
}

int main()
{
	auto handles = create_external_handles();

	pid_t pid = fork();
	if (pid > 0)
	{
		// Parent
		run_parent(handles);
		LOGI("Parent exiting ...\n");
	}
	else if (pid == 0)
	{
		// Child
		run_child(handles);
		LOGI("Child exiting ...\n");
	}
	else
	{
		return EXIT_FAILURE;
	}
}
