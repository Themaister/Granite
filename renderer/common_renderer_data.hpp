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

#pragma once

#include "buffer.hpp"
#include "image.hpp"
#include "event.hpp"
#include "asset_manager.hpp"
#include "application_wsi_events.hpp"
#include "application_events.hpp"
#include "global_managers_interface.hpp"

namespace Granite
{
class LightMesh : public EventHandler
{
public:
	LightMesh();

	Vulkan::BufferHandle spot_vbo;
	Vulkan::BufferHandle spot_ibo;
	unsigned spot_count = 0;

	Vulkan::BufferHandle point_vbo;
	Vulkan::BufferHandle point_ibo;
	unsigned point_count = 0;

private:
	void create_point_mesh(const Vulkan::DeviceCreatedEvent &e);
	void create_spot_mesh(const Vulkan::DeviceCreatedEvent &e);

	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &);
};

class CommonRendererData final : public CommonRendererDataInterface
{
public:
	LightMesh light_mesh;
	AssetID brdf_tables;
	void initialize_static_assets(AssetManager *iface, Filesystem *file_iface);
};
}