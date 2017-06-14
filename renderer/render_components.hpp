#pragma once

#include "ecs.hpp"
#include "math.hpp"
#include "aabb.hpp"
#include "mesh.hpp"
#include "abstract_renderable.hpp"

namespace Granite
{
struct Transform
{
	vec3 scale = vec3(1.0f);
	vec3 translation = vec3(0.0f);
	quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
};

struct CachedTransform
{
	mat4 world_transform;
	mat4 normal_transform;
};

struct CachedSkinTransform
{
	std::vector<mat4> bone_world_transforms;
	std::vector<mat4> bone_normal_transforms;
};

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

struct PerFrameRefreshableTransform
{
	virtual ~PerFrameRefreshableTransform() = default;
	virtual void refresh(RenderContext &context, const CachedSpatialTransformComponent *transform) = 0;
};

struct PerFrameRefreshable
{
	virtual ~PerFrameRefreshable() = default;
	virtual void refresh(RenderContext &context) = 0;
};

struct PerFrameUpdateTransformComponent : ComponentBase
{
	PerFrameRefreshableTransform *refresh = nullptr;
};

struct PerFrameUpdateComponent : ComponentBase
{
	PerFrameRefreshable *refresh = nullptr;
};

struct CachedSpatialTransformComponent : ComponentBase
{
	AABB world_aabb;
	CachedTransform *transform = nullptr;
	CachedSkinTransform *skin_transform = nullptr;
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