#define NOMINMAX
#include "device.hpp"
#include "command_buffer.hpp"
#include "context.hpp"
#include "timer.hpp"
#include <vector>
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

	std::vector<uint32_t> data(1024 * 1024);
	auto info = ImageCreateInfo::immutable_2d_image(1024, 1024, VK_FORMAT_R8G8B8A8_UNORM);

	Util::Timer timer;
	timer.start();
	for (unsigned i = 0; i < 1024; i++)
	{
		ImageInitialData initial = { data.data(), 0, 0 };
		for (unsigned j = 0; j < 8; j++)
			dev.create_image(info, &initial);
		dev.next_frame_context();
	}
	LOGI("Copying over 32 GiB of data took %.3f s.\n", timer.end());
}
