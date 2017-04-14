#pragma once

#include "material.hpp"
#include "volatile_source.hpp"
#include "device.hpp"
#include "event.hpp"

namespace Granite
{
class MaterialFile : public Material, public Util::VolatileSource<MaterialFile>, public EventHandler
{
public:
	MaterialFile(const std::string &path);
	void update(const void *data, size_t size);

private:
	Vulkan::Device *device = nullptr;
	std::string paths[ecast(Material::Textures::Count)];

	void on_device_created(const Event &e);
	void on_device_destroyed(const Event &e);

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