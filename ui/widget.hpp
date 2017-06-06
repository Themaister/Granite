#pragma once
#include "intrusive.hpp"
#include "math.hpp"
#include <vector>

namespace Granite
{
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

	virtual void add_child(const Util::IntrusivePtr<Widget> &widget);
	virtual Util::IntrusivePtr<Widget> remove_child(const Widget &widget);

	template <typename T, typename... P>
	inline T *add_child(P&&... p)
	{
		auto handle = Util::make_abstract_handle<Widget, T>(std::forward<P>(p)...);
		add_child(handle);
		return handle.get();
	}

	void set_minimum_geometry(ivec2 size)
	{
		geometry.minimum = size;
		geometry_changed();
	}

	void set_target_geometry(ivec2 size)
	{
		geometry.target = size;
		geometry_changed();
	}

	void set_size_is_flexible(bool enable)
	{
		geometry.flexible_size = enable;
		geometry_changed();
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

	bool get_needs_redraw() const;
	void reconfigure_geometry();

protected:
	void geometry_changed();
	virtual void render(FlatRenderer &renderer, float layer, ivec2 offset, ivec2 size) = 0;

	vec4 bg_color = vec4(1.0f, 1.0f, 1.0f, 0.0f);
	bool needs_redraw = true;

	struct
	{
		ivec2 minimum = ivec2(1);
		ivec2 target = ivec2(1);
		bool flexible_size = false;
		bool visible = true;
	} geometry;

	void render_children(FlatRenderer &renderer, float layer, ivec2 offset);

private:
	Widget *parent = nullptr;

	struct Child
	{
		ivec2 offset;
		ivec2 size;
		Util::IntrusivePtr<Widget> widget;
	};
	std::vector<Child> children;
	bool needs_reconfigure = false;

	virtual void reconfigure() = 0;
};

using WidgetHandle = Util::IntrusivePtr<Widget>;
}
}