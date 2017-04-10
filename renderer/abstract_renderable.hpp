#pragma once

#include "render_queue.hpp"

namespace Granite
{
class AbstractRenderable
{
public:
	virtual ~AbstractRenderable() = default;
	virtual void get_render_info(RenderQueue &queue) = 0;
	virtual void get_depth_render_info(RenderQueue &queue) = 0;
};
}