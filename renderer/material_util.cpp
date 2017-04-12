#include "material_util.hpp"
#include "device.hpp"
#include "vulkan_events.hpp"
#include "texture_manager.hpp"

using namespace Vulkan;

namespace Granite
{
StockMaterials &StockMaterials::get()
{
	static StockMaterials stock;
	return stock;
}

StockMaterials::StockMaterials()
{
	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
	                                                  &StockMaterials::on_device_created,
	                                                  &StockMaterials::on_device_destroyed,
	                                                  this);
}

MaterialHandle StockMaterials::get_checkerboard()
{
	return checkerboard;
}

void StockMaterials::on_device_created(const Event &event)
{
	auto &created = event.as<DeviceCreatedEvent>();
	auto &manager = created.get_device().get_texture_manager();
	checkerboard = Util::make_handle<Material>();
	checkerboard->textures[ecast(Material::Textures::Albedo)] = manager.request_texture("assets://textures/checkerboard.png");
	checkerboard->emissive = 0.0f;
	checkerboard->metallic = 0.0f;
	checkerboard->roughness = 1.0f;
}

void StockMaterials::on_device_destroyed(const Event &)
{
	checkerboard.reset();
}

}