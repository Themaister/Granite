/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once
#include "intrusive.hpp"
#include "math.hpp"
#include <vector>
#include "texture_manager.hpp"

namespace Granite
{
class MouseButtonEvent;
class FlatRenderer;

namespace UI
{
enum class Alignment
{
	TopLeft,
	TopCenter,
	TopRight,
	CenterLeft,
	Center,
	CenterRight,
	BottomLeft,
	BottomCenter,
	BottomCenterRight,
};

class Widget : public Util::IntrusivePtrEnabled<Widget>
{
public:
	virtual ~Widget() = default;

	virtual void add_child(Util::IntrusivePtr<Widget> widget);
	virtual Util::IntrusivePtr<Widget> remove_child(const Widget &widget);

	template <typename T, typename... P>
	inline T *add_child(P&&... p)
	{
		auto handle = Util::make_handle<T>(std::forward<P>(p)...);
		add_child(handle);
		return static_cast<T *>(handle.get());
	}

	Widget &get_child_widget(unsigned index)
	{
		assert(index < children.size());
		return *children[index].widget;
	}

	void set_minimum_geometry(vec2 size)
	{
		geometry.minimum = size;
		geometry_changed();
	}

	void set_target_geometry(vec2 size)
	{
		geometry.target = size;
		geometry_changed();
	}

	vec2 get_target_geometry() const
	{
		return geometry.target;
	}

	vec2 get_minimum_geometry() const
	{
		return geometry.minimum;
	}

	void set_margin(float pixels)
	{
		geometry.margin = pixels;
		geometry_changed();
	}

	float get_margin() const
	{
		return geometry.margin;
	}

	void set_size_is_flexible(bool enable)
	{
		geometry.flexible_size = enable;
		geometry_changed();
	}

	bool get_size_is_flexible() const
	{
		return geometry.flexible_size;
	}

	void set_visible(bool visible)
	{
		geometry.visible = visible;
		geometry_changed();
	}

	bool get_visible() const
	{
		return geometry.visible;
	}

	void set_background_color(vec4 color)
	{
		bg_color = color;
		needs_redraw = true;
	}

	void set_background_image(Vulkan::Texture *texture)
	{
		bg_image = texture;
		needs_redraw = true;
	}

	bool get_needs_redraw() const;
	void reconfigure_geometry();
	void reconfigure_geometry_to_canvas(vec2 offset, vec2 size);

	virtual float render(FlatRenderer & /* renderer */, float layer, vec2 /* offset */, vec2 /* size */)
	{
		return layer;
	}

	virtual Widget *on_mouse_button_pressed(vec2);

	virtual void on_mouse_button_released(vec2)
	{
	}

	virtual void on_mouse_button_move(vec2)
	{
	}

	void set_floating_position(vec2 pos)
	{
		floating_position = pos;
		geometry_changed();
	}

	vec2 get_floating_position() const
	{
		return floating_position;
	}

	bool is_floating() const
	{
		return floating;
	}

	void set_floating(bool state)
	{
		floating = state;
		geometry_changed();
	}

protected:
	void geometry_changed();

	vec2 floating_position = vec2(0.0f);
	vec4 bg_color = vec4(1.0f, 1.0f, 1.0f, 0.0f);
	Vulkan::Texture *bg_image = nullptr;
	bool needs_redraw = true;
	bool floating = false;

	struct
	{
		vec2 minimum = vec2(1);
		vec2 target = vec2(1);
		float margin = 0.0f;
		bool flexible_size = false;
		bool visible = true;
	} geometry;

	float render_children(FlatRenderer &renderer, float layer, vec2 offset);

	Widget *parent = nullptr;

	struct Child
	{
		vec2 offset;
		vec2 size;
		Util::IntrusivePtr<Widget> widget;
	};
	std::vector<Child> children;
	bool needs_reconfigure = false;

	virtual void reconfigure() = 0;
	virtual void reconfigure_to_canvas(vec2 offset, vec2 size) = 0;
};

using WidgetHandle = Util::IntrusivePtr<Widget>;
}
}