/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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

#include "global_managers.hpp"
#include "os_filesystem.hpp"
#include "device.hpp"
#include "context.hpp"

using namespace Granite;
using namespace Vulkan;

static int main_inner()
{
	if (!Context::init_loader(nullptr))
		return 1;

	Context ctx;
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return 1;

	Device dev;
	dev.set_context(ctx);

	auto cmd = dev.request_command_buffer();
	cmd->set_program("assets://shaders/sampler_precision.comp");

	BufferCreateInfo buf;
	buf.size = 4096 * 2 * sizeof(uint32_t);
	buf.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buf.domain = BufferDomain::CachedHost;
	auto ssbo = dev.create_buffer(buf);

	auto imageinfo = ImageCreateInfo::immutable_2d_image(4, 1, VK_FORMAT_R8_UNORM);
	const uint8_t pixels[] = { 0, 1, 2, 3 };
	ImageInitialData data = { pixels };
	auto img = dev.create_image(imageinfo, &data);

	cmd->set_texture(0, 0, img->get_view(), StockSampler::NearestClamp);
	cmd->set_storage_buffer(0, 1, *ssbo);
	cmd->dispatch(4096 / 64, 1, 1);
	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	dev.submit(cmd);
	dev.wait_idle();

	auto *ptr = static_cast<const uint32_t *>(dev.map_host_buffer(*ssbo, MEMORY_ACCESS_READ_BIT));
	for (unsigned i = 2048 - 32; i < 2048 + 32; i++)
	{
		LOGI("U = %u + %u / 2048\n", i / 2048, i % 2048);
		LOGI("  Point: %u\n", ptr[2 * i + 0]);
	}

	for (unsigned i = 1 * 2048 + 1024 - 32; i < 1 * 2048 + 1024 + 32; i++)
	{
		LOGI("U = %u + %u / 2048\n", i / 2048, i % 2048);
		LOGI("  Gather: %u\n", ptr[2 * i + 1]);
	}

	return 0;
}

int main()
{
	Global::init();
#ifdef ASSET_DIRECTORY
	const char *asset_dir = getenv("ASSET_DIRECTORY");
	if (!asset_dir)
		asset_dir = ASSET_DIRECTORY;

	Global::filesystem()->register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
#endif
	int ret = main_inner();
	Global::deinit();
	return ret;
}