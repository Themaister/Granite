#include "ground.hpp"
#include "vulkan_events.hpp"
#include "device.hpp"

using namespace Vulkan;
using namespace std;

namespace Granite
{
void GroundPatch::set_scale(vec2 base, vec2 offset)
{
	this->base = base;
	this->offset = offset;
	aabb = AABB(vec3(base, -1.0f), vec3(base + offset, 2.0f));
}

GroundPatch::GroundPatch(Util::IntrusivePtr<Ground> ground)
	: ground(ground)
{
}

void GroundPatch::refresh(RenderContext &context, const CachedSpatialTransformComponent *transform)
{

}

void GroundPatch::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
                                  RenderQueue &queue) const
{
	ground->get_render_info(context, transform, queue, base, offset);
}

Ground::Ground(const string &heightmap, const string &normalmap)
	: heightmap_path(heightmap), normalmap_path(normalmap)
{
	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
                                                      &Ground::on_device_created,
                                                      &Ground::on_device_destroyed,
                                                      this);
}

void Ground::on_device_created(const Event &e)
{
	auto &device = e.as<DeviceCreatedEvent>().get_device();
	heights = device.get_texture_manager().request_texture(heightmap_path);
	normals = device.get_texture_manager().request_texture(normalmap_path);
}

void Ground::on_device_destroyed(const Event &)
{
	heights = nullptr;
	normals = nullptr;
}

void Ground::refresh(RenderContext &context)
{
}
}