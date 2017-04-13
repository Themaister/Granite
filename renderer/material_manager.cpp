#include "material_manager.hpp"
#include "vulkan_events.hpp"
#include <string.h>

using namespace std;
using namespace Vulkan;

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
}

void MaterialFile::on_device_created(const Event &e)
{
	device = &e.as<DeviceCreatedEvent>().get_device();
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