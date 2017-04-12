#pragma once

#include "mesh.hpp"
#include "vulkan_events.hpp"

namespace Granite
{
class CubeMesh : public StaticMesh, public EventHandler
{
public:
	CubeMesh();

private:
	void on_device_created(const Event &event);
	void on_device_destroyed(const Event &event);
};
}