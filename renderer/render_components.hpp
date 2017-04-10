#pragma once

#include "ecs.hpp"
#include "math.hpp"
#include "aabb.hpp"
#include "mesh.hpp"
#include "abstract_renderable.hpp"

namespace Granite
{
struct SpatialComponent : ComponentBase
{
	vec3 position;
};

struct BoundedComponent : ComponentBase
{
	AABB aabb;
};

struct BackgroundComponent : ComponentBase
{
	std::unique_ptr<AbstractRenderable> renderable;
};

struct SpatialTransformComponent : ComponentBase
{
	vec3 scale;
	vec3 translation;
	quat rotation;
};

struct CachedSpatialTransformComponent : ComponentBase
{
	mat4 world_transform;
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

struct StaticMeshComponent : ComponentBase
{
	StaticMesh mesh;
};

}