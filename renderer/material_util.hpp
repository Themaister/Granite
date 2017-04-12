#pragma once

#include "material.hpp"
#include "event.hpp"

namespace Granite
{
class StockMaterials : public EventHandler
{
public:
	static StockMaterials &get();
	MaterialHandle get_checkerboard();

private:
	StockMaterials();
	void on_device_created(const Event &event);
	void on_device_destroyed(const Event &event);

	MaterialHandle checkerboard;
};
}