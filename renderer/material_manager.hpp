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

#include "material.hpp"
#include "volatile_source.hpp"
#include "device.hpp"
#include "event.hpp"
#include "scene_formats.hpp"
#include "application_wsi_events.hpp"

namespace Granite
{
class MaterialFile : public Material, public Util::VolatileSource<MaterialFile>, public EventHandler
{
public:
	MaterialFile(const std::string &path);
	MaterialFile(const SceneFormats::MaterialInfo &info);
	void update(std::unique_ptr<File> file);

private:
	Vulkan::Device *device = nullptr;
	std::string paths[Util::ecast(Material::Textures::Count)];

	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);

	void init_textures();
};

class MaterialManager
{
public:
	MaterialHandle request_material(const std::string &path);
	static MaterialManager &get();

private:
	MaterialManager() = default;
	std::unordered_map<std::string, MaterialHandle> materials;
};
}