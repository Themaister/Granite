#pragma once

#include "aabb.hpp"
#include "intrusive.hpp"

namespace Granite
{
class RenderQueue;
class RenderContext;
class ShaderSuite;

enum class MeshDrawPipeline : unsigned
{
	Opaque,
	AlphaTest,
	AlphaBlend,
};

class AbstractRenderable : public Util::IntrusivePtrEnabled<AbstractRenderable>
{
public:
	virtual ~AbstractRenderable() = default;
	virtual void get_render_info(const RenderContext &context, RenderQueue &queue) = 0;

	virtual void get_depth_render_info(const RenderContext &context, RenderQueue &queue)
	{
		return get_render_info(context, queue);
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

	virtual MeshDrawPipeline get_mesh_draw_pipeline() const
	{
		return MeshDrawPipeline::Opaque;
	}
};
using AbstractRenderableHandle = Util::IntrusivePtr<AbstractRenderable>;
}