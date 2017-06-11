#pragma once

#include "widget.hpp"
#include "texture_manager.hpp"
#include "vulkan_events.hpp"

namespace Granite
{
namespace UI
{
class Image : public Widget, public EventHandler
{
public:
	Image(const std::string &path);

	void set_keep_aspect_ratio(bool enable)
	{
		keep_aspect = enable;
	}

	bool get_keep_aspect_ratio() const
	{
		return keep_aspect;
	}

private:
	float render(FlatRenderer &renderer, float layout, vec2 offset, vec2 size) override;
	void on_device_created(const Event &e);
	void on_device_destroyed(const Event &e);
	std::string path;
	Vulkan::Texture *texture = nullptr;

	bool keep_aspect = true;
};
}
}