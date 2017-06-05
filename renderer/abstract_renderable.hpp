#pragma once

#include "aabb.hpp"
#include "intrusive.hpp"

namespace Granite
{
class RenderQueue;
class RenderContext;
class ShaderSuite;
class CachedSpatialTransformComponent;
struct SpriteTransformInfo;

enum class DrawPipeline : unsigned
{
	Opaque,
	AlphaTest,
	AlphaBlend,
};

class AbstractRenderable : public Util::IntrusivePtrEnabled<AbstractRenderable>
{
public:
	virtual ~AbstractRenderable() = default;
	virtual void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform, RenderQueue &queue) const = 0;

	virtual void get_depth_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform, RenderQueue &queue) const
	{
		return get_render_info(context, transform, queue);
	}

	virtual void get_sprite_render_info(const SpriteTransformInfo &, RenderQueue &) const
	{
	}

	virtual bool has_static_aabb() const
	{
		return false;
	}

	virtual const AABB &get_static_aabb() const
	{
		static const AABB aabb(vec3(0.0f), vec3(0.0f));
		return aabb;
	}

	virtual DrawPipeline get_mesh_draw_pipeline() const
	{
		return DrawPipeline::Opaque;
	}
};
using AbstractRenderableHandle = Util::IntrusivePtr<AbstractRenderable>;
}