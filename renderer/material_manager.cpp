#include "material_manager.hpp"
#include "vulkan_events.hpp"
#include <string.h>

#define RAPIDJSON_ASSERT(x) do { if (!(x)) throw "JSON error"; } while(0)
#include "rapidjson/document.h"

using namespace std;
using namespace Vulkan;
using namespace rapidjson;

namespace Granite
{
MaterialFile::MaterialFile(const std::string &path)
	: VolatileSource(path)
{
	init();
	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
                                                      &MaterialFile::on_device_created,
                                                      &MaterialFile::on_device_destroyed,
                                                      this);
}

void MaterialFile::update(const void *data, size_t size)
{
	try
	{
		string json(static_cast<const char *>(data), static_cast<const char *>(data) + size);

		Document doc;
		doc.Parse(json);

		auto &mat = doc["material"];

		if (mat.HasMember("albedo"))
		{
			auto &albedo = mat["albedo"];
			paths[ecast(Material::Textures::Albedo)].clear();
			if (albedo.IsString())
				paths[ecast(Material::Textures::Albedo)] = albedo.GetString();
			else if (albedo.IsArray())
			{
				for (unsigned i = 0; i < 4; i++)
					this->albedo[i] = albedo[i].GetFloat();
			}
			else
				this->albedo = vec4(1.0f, 0.5f, 0.5f, 1.0f);
		}
		else
			this->albedo = vec4(1.0f, 0.5f, 0.5f, 1.0f);

		if (mat.HasMember("normal"))
		{
			auto &normal = mat["normal"];
			paths[ecast(Material::Textures::Normal)].clear();
			if (normal.IsString())
				paths[ecast(Material::Textures::Normal)] = normal.GetString();
		}

		if (mat.HasMember("roughness"))
		{
			auto &roughness = mat["roughness"];
			paths[ecast(Material::Textures::Roughness)].clear();
			if (roughness.IsString())
				paths[ecast(Material::Textures::Roughness)] = roughness.GetString();
			else if (roughness.IsFloat())
				this->roughness = roughness.GetFloat();
			else
				this->roughness = 1.0f;

			assert(this->roughness >= 0.0f && this->roughness <= 1.0f);
		}
		else
			this->roughness = 1.0f;

		if (mat.HasMember("emissive"))
		{
			auto &emissive = mat["emissive"];
			if (emissive.IsFloat())
				this->emissive = emissive.GetFloat();
			else
				this->emissive = 0.0f;

			assert(this->emissive >= 0.0f);
		}
		else
			this->emissive = 0.0f;

		if (mat.HasMember("metallic"))
		{
			auto &metallic = mat["metallic"];
			paths[ecast(Material::Textures::Metallic)].clear();
			if (metallic.IsString())
				paths[ecast(Material::Textures::Metallic)] = metallic.GetString();
			else if (metallic.IsFloat())
				this->metallic = metallic.GetFloat();
			else
				this->metallic = 0.0f;

			assert(this->metallic >= 0.0f && this->metallic <= 1.0f);
		}
		else
			this->metallic = 0.0f;
	}
	catch (const char *)
	{
		LOGE("JSON error.\n");
	}

	if (device)
		init_textures();
}

void MaterialFile::init_textures()
{
	for (unsigned i = 0; i < ecast(Material::Textures::Count); i++)
	{
		if (!paths[i].empty())
			textures[i] = device->get_texture_manager().request_texture(paths[i]);
		else
			textures[i] = nullptr;
	}
}

void MaterialFile::on_device_created(const Event &e)
{
	device = &e.as<DeviceCreatedEvent>().get_device();
	init_textures();
}

void MaterialFile::on_device_destroyed(const Event &)
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