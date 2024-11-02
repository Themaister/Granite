/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "vulkan_headers.hpp"
#include "device.hpp"
#include "utils/image_utils.hpp"
#include "cli_parser.hpp"
#include "global_managers_init.hpp"
#include "thread_group.hpp"

using namespace Vulkan;
using namespace Granite;
using namespace Util;

static void print_help()
{
	LOGE("Usage: [--reflection <path.gtx>] [--irradiance <path.gtx>] [--cube <path.gtx>] [--cube-scale <scale>] <equirect HDR>\n");
}

int main(int argc, char *argv[])
{
	CLICallbacks cbs;
	struct Args
	{
		std::string equirect;
		std::string cube;
		std::string reflection;
		std::string irradiance;
		float cube_scale = 1.0f;
	} args;

	Granite::Global::init();

	cbs.add("--help", [](CLIParser &parser) { print_help(); parser.end(); });
	cbs.add("--reflection", [&](CLIParser &parser) { args.reflection = parser.next_string(); });
	cbs.add("--irradiance", [&](CLIParser &parser) { args.irradiance = parser.next_string(); });
	cbs.add("--cube", [&](CLIParser &parser) { args.cube = parser.next_string(); });
	cbs.add("--cube-scale", [&](CLIParser &parser) { args.cube_scale = parser.next_double(); });
	cbs.default_handler = [&](const char *arg) { args.equirect = arg; };
	cbs.error_handler = [&]() { print_help(); };

	CLIParser parser(std::move(cbs), argc - 1, argv + 1);
	if (!parser.parse())
		return 1;
	else if (parser.is_ended_state())
		return 0;

	if (args.equirect.empty())
	{
		print_help();
		return 1;
	}

	Context::init_loader(nullptr);
	Context context;

	Context::SystemHandles handles;
	handles.filesystem = GRANITE_FILESYSTEM();
	handles.thread_group = GRANITE_THREAD_GROUP();
	context.set_system_handles(handles);

	if (!context.init_instance_and_device(nullptr, 0, nullptr, 0))
		return 1;

	Device device;
	device.set_context(context);
	device.init_external_swapchain({ ImageHandle(nullptr) });

	auto &textures = device.get_resource_manager();
	auto equirect = GRANITE_ASSET_MANAGER()->register_asset(*GRANITE_FILESYSTEM(), args.equirect,
	                                                        AssetClass::ImageColor);
	auto *view = textures.get_image_view_blocking(equirect);

	auto cube = convert_equirect_to_cube(device, *view, args.cube_scale);
	auto specular = convert_cube_to_ibl_specular(device, cube->get_view());
	auto diffuse = convert_cube_to_ibl_diffuse(device, cube->get_view());

	auto cmd = device.request_command_buffer();

	cmd->image_barrier(*cube, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

	cmd->image_barrier(*specular, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

	cmd->image_barrier(*diffuse, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

	device.submit(cmd);

	auto saved_cube = save_image_to_cpu_buffer(device, *cube, CommandBuffer::Type::Generic);
	auto saved_specular = save_image_to_cpu_buffer(device, *specular, CommandBuffer::Type::Generic);
	auto saved_diffuse = save_image_to_cpu_buffer(device, *diffuse, CommandBuffer::Type::Generic);

	if (!args.cube.empty())
		save_image_buffer_to_gtx(device, saved_cube, args.cube.c_str());
	if (!args.reflection.empty())
		save_image_buffer_to_gtx(device, saved_specular, args.reflection.c_str());
	if (!args.irradiance.empty())
		save_image_buffer_to_gtx(device, saved_diffuse, args.irradiance.c_str());
}
