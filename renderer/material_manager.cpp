/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "material_manager.hpp"
#include "vulkan_events.hpp"
#include <string.h>

#define RAPIDJSON_ASSERT(x) do { if (!(x)) throw "JSON error"; } while(0)
#include "rapidjson/document.h"
#include "../importers/importers.hpp"

using namespace std;
using namespace Vulkan;
using namespace rapidjson;
using namespace Util;
using namespace Granite::Importer;

namespace Granite
{
MaterialFile::MaterialFile(const std::string &path)
	: VolatileSource(path)
{
	init();

	EVENT_MANAGER_REGISTER_LATCH(MaterialFile, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

MaterialFile::MaterialFile(const MaterialInfo &info)
{
	paths[ecast(Material::Textures::BaseColor)] = info.base_color;
	paths[ecast(Material::Textures::Normal)] = info.normal;
	paths[ecast(Material::Textures::MetallicRoughness)] = info.metallic_roughness;
	paths[ecast(Material::Textures::Occlusion)] = info.occlusion;
	base_color = info.uniform_base_color;
	emissive = info.uniform_emissive_color;
	metallic = info.uniform_metallic;
	roughness = info.uniform_roughness;
	pipeline = info.pipeline;
	two_sided = info.two_sided;
	lod_bias = info.lod_bias;
	sampler = info.sampler;
	bake();

	EVENT_MANAGER_REGISTER_LATCH(MaterialFile, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void MaterialFile::update(const void *data, size_t size)
{
	try
	{
		string json(static_cast<const char *>(data), static_cast<const char *>(data) + size);

		Document doc;
		doc.Parse(json);

		auto &mat = doc["material"];

		if (mat.HasMember("base_color"))
		{
			auto &base = mat["base_color"];
			paths[ecast(Material::Textures::BaseColor)].clear();
			if (base.IsString())
				paths[ecast(Material::Textures::BaseColor)] = base.GetString();
			else if (base.IsArray())
			{
				for (unsigned i = 0; i < 4; i++)
					this->base_color[i] = base[i].GetFloat();
			}
			else
				this->base_color = vec4(1.0f, 0.5f, 0.5f, 1.0f);
		}
		else
			this->base_color = vec4(1.0f, 0.5f, 0.5f, 1.0f);

		if (mat.HasMember("normal"))
		{
			auto &normal = mat["normal"];
			paths[ecast(Material::Textures::Normal)].clear();
			if (normal.IsString())
				paths[ecast(Material::Textures::Normal)] = normal.GetString();
		}

		if (mat.HasMember("metallic_roughness"))
		{
			auto &mr = mat["metallic_roughness"];
			paths[ecast(Material::Textures::MetallicRoughness)].clear();
			if (mr.IsString())
				paths[ecast(Material::Textures::MetallicRoughness)] = mr.GetString();
			else if (mr.IsArray())
			{
				this->metallic = mr[0].GetFloat();
				this->roughness = mr[1].GetFloat();
			}
			else
			{
				this->metallic = 1.0f;
				this->roughness = 1.0f;
			}

			assert(this->metallic >= 0.0f && this->metallic <= 1.0f);
			assert(this->roughness >= 0.0f && this->roughness <= 1.0f);
		}
		else
		{
			this->metallic = 1.0f;
			this->roughness = 1.0f;
		}

		if (mat.HasMember("emissive"))
		{
			auto &emissive = mat["emissive"];
			this->emissive = vec3(emissive[0].GetFloat(), emissive[1].GetFloat(), emissive[2].GetFloat());
		}
		else
			this->emissive = vec3(0.0f);
	}
	catch (const char *)
	{
		LOGE("JSON error.\n");
	}

	if (device)
		init_textures();
	bake();
}

void MaterialFile::init_textures()
{
	for (unsigned i = 0; i < ecast(Material::Textures::Count); i++)
	{
		VkFormat default_format;
		switch (static_cast<Material::Textures>(i))
		{
		case Material::Textures::BaseColor:
			default_format = VK_FORMAT_R8G8B8A8_SRGB;
			break;
		default:
			default_format = VK_FORMAT_R8G8B8A8_UNORM;
			break;
		}

		if (!paths[i].empty())
			textures[i] = device->get_texture_manager().request_texture(paths[i], default_format);
		else
			textures[i] = nullptr;
	}
	bake();
}

void MaterialFile::on_device_created(const DeviceCreatedEvent &created)
{
	device = &created.get_device();
	init_textures();
}

void MaterialFile::on_device_destroyed(const DeviceCreatedEvent &)
{
	device = nullptr;
	memset(textures, 0, sizeof(textures));
}

MaterialManager &MaterialManager::get()
{
	static MaterialManager manager;
	return manager;
}

MaterialHandle MaterialManager::request_material(const std::string &path)
{
	auto itr = materials.find(path);
	if (itr == end(materials))
	{
		auto handle = Util::make_abstract_handle<Material, MaterialFile>(path);
		materials[path] = handle;
		return handle;
	}
	else
		return itr->second;
}
}