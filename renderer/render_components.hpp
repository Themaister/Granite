#pragma once

#include "ecs.hpp"
#include "math.hpp"
#include "aabb.hpp"
#include "mesh.hpp"
#include "abstract_renderable.hpp"

namespace Granite
{
struct BoundedComponent : ComponentBase
{
	AABB aabb;
};

struct UnboundedComponent : ComponentBase
{
};

struct RenderableComponent : ComponentBase
{
	AbstractRenderableHandle renderable;
};

struct SpatialTransformComponent : ComponentBase
{
	vec3 scale = vec3(1.0f);
	vec3 translation = vec3(0.0f);
	quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
};

struct CachedSpatialTransformComponent : ComponentBase
{
	mat4 world_transform;
	mat4 normal_transform;
	AABB world_aabb;
};

struct OpaqueComponent : ComponentBase
{
};

struct TransparentComponent : ComponentBase
{
};

struct CastsShadowComponent : ComponentBase
{
};

}