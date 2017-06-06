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

	virtual void add_child(const Util::IntrusivePtr<Widget> &widget) = 0;
	virtual Util::IntrusivePtr<Widget> remove_child(Widget &widget);

	template <typename T, typename... P>
	inline T *add_child(P&&... p)
	{
		auto handle = Util::make_abstract_handle<Widget, T>(std::forward<P>(p)...);
		add_child(handle);
		return handle.get();
	}

	virtual void set_minimum_geometry(ivec2 size) = 0;
	virtual void set_target_geometry(ivec2 size) = 0;
	virtual void set_parent_offset(ivec2 offset) = 0;
	virtual void set_alignment(Alignment alignment) = 0;
	virtual void set_internal_margin(ivec2 offset) = 0;
	virtual void set_outer_margin(ivec2 offset) = 0;
	virtual void set_size_is_flexible(bool enable) = 0;

protected:
	void notify_children_geometry_changed();
	virtual ivec2 get_minimum_geometry() = 0;
	virtual ivec2 get_target_geometry() = 0;
	virtual void render(FlatRenderer &renderer, float layer, ivec2 offset, ivec2 size) = 0;

private:
	Widget *parent = nullptr;
	std::vector<Util::IntrusivePtr<Widget>> children;
};

using WidgetHandle = Util::IntrusivePtr<Widget>;
}
}