#include "image_widget.hpp"
#include "device.hpp"
#include "flat_renderer.hpp"
#include "widget.hpp"

using namespace Vulkan;

namespace Granite
{
namespace UI
{
Image::Image(const std::string &path)
	: path(path)
{
	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
	                                                  &Image::on_device_created,
	                                                  &Image::on_device_destroyed,
	                                                  this);
}

void Image::on_device_created(const Event &e)
{
	auto &device = e.as<DeviceCreatedEvent>().get_device();
	texture = device.get_texture_manager().request_texture(path);

	auto &create_info = texture->get_image()->get_create_info();
	geometry.minimum = vec2(create_info.width, create_info.height);
	geometry.target = vec2(create_info.width, create_info.height);
}

float Image::render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size)
{
	if (keep_aspect)
	{
		float target_aspect = geometry.target.x / geometry.target.y;
		float canvas_aspect = size.x / size.y;

		if (glm::abs(canvas_aspect / target_aspect - 1.0f) > 0.001f)
		{
			if (canvas_aspect > target_aspect)
			{
				float width = round(size.y * target_aspect);
				float bias = 0.5f * (size.x - width);
				offset.x = round(offset.x + bias);
				size.x = width;
			}
			else
			{
				float height = round(size.x / target_aspect);
				float bias = 0.5f * (size.y - height);
				offset.y = round(offset.y + bias);
				size.y = height;
			}
		}
	}

	renderer.render_textured_quad(texture->get_image()->get_view(), vec3(offset, layer), size,
	                              vec2(0.0f), vec2(geometry.target), true, vec4(1.0f), Vulkan::StockSampler::LinearClamp);
	return layer;
}

void Image::on_device_destroyed(const Event &)
{
	texture = nullptr;
}
}
}