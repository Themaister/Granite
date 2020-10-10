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

#pragma once

#include "buffer.hpp"
#include "image.hpp"
#include "event.hpp"
#include "application_wsi_events.hpp"
#include "application_events.hpp"

namespace Vulkan
{
class Texture;
}

namespace Granite
{
class PersistentFrameEvent : public EventHandler
{
public:
	PersistentFrameEvent();
	float frame_time = 0.0f;

private:
	bool on_frame_time(const FrameTickEvent &tick);
};

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

class SSAOLookupTables : public EventHandler
{
public:
	SSAOLookupTables();

	Vulkan::BufferHandle kernel;
	unsigned kernel_size = 0;

	Vulkan::ImageHandle noise;
	unsigned noise_resolution = 4;

private:
	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);
};

class BRDFTables : public EventHandler
{
public:
	BRDFTables();
	Vulkan::Texture *get_texture() const;

private:
	Vulkan::Texture *texture = nullptr;
	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);
};

class CommonRendererData
{
public:
	LightMesh light_mesh;
	PersistentFrameEvent frame_tick;
	SSAOLookupTables ssao_luts;
	BRDFTables brdf_tables;
};
}