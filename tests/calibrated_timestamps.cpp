#define NOMINMAX
#include "device.hpp"
#include "command_buffer.hpp"
#include "context.hpp"
#include "timer.hpp"
#include <vector>
#include <thread>
#include <stdint.h>

using namespace Vulkan;
using namespace Granite;

int main()
{
	if (!Context::init_loader(nullptr))
		return EXIT_FAILURE;
	Context ctx;
	ctx.init_instance_and_device(nullptr, 0, nullptr, 0);
	Device dev;
	dev.set_context(ctx);

	struct Timestamps
	{
		QueryPoolHandle ts;
		int64_t reference;
	};
	std::vector<Timestamps> timestamps;

	constexpr int Iterations = 100;
	timestamps.reserve(Iterations);

	for (int i = 0; i < Iterations; i++)
	{
		auto cmd = dev.request_command_buffer();
		auto ts = cmd->write_timestamp(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
		Fence fence;
		dev.submit(cmd, &fence);
		// Expect some minor error, but nothing more than a new ms at most.
		fence->wait();
		timestamps.push_back({ std::move(ts), Util::get_current_time_nsecs() });
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		dev.next_frame_context();

		LOGI("Sampling iteration %d / %d done ...\n", i, Iterations);
	}

	for (auto &t : timestamps)
	{
		if (t.ts->is_signalled())
		{
			int64_t device_ns = dev.convert_timestamp_to_absolute_nsec(*t.ts);
			auto error_ns = t.reference - device_ns;
			LOGI("Got %lld us error for calibrated timestamp.\n",
				 static_cast<unsigned long long>(error_ns / 1000));
		}
	}
}
