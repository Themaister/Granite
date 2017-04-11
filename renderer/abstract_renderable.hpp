#pragma once

namespace Granite
{
class RenderQueue;
class RenderContext;
class ShaderSuite;

class AbstractRenderable
{
public:
	virtual ~AbstractRenderable() = default;
	virtual void get_render_info(const RenderContext &context, RenderQueue &queue) = 0;

	virtual void get_depth_render_info(const RenderContext &context, RenderQueue &queue)
	{
		return get_render_info(context, queue);
	}
};
}