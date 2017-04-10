#pragma once

#include "ecs.hpp"
#include "render_components.hpp"

namespace Granite
{
class Scene
{
public:
	EntityHandle create_spatial_node();

private:
	EntityPool pool;
	std::vector<EntityHandle> nodes;
};
}