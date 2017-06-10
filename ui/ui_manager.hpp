#pragma once

#include "event.hpp"
#include "widget.hpp"
#include "flat_renderer.hpp"

namespace Granite
{
namespace UI
{
enum class FontSize
{
	Small = 0,
	Normal,
	Large,
	Count
};

class UIManager : public EventHandler
{
public:
	static UIManager &get();

	bool filter_input_event(const Event &e);

	void add_child(WidgetHandle handle);

	template <typename T, typename... P>
	inline T *add_child(P&&... p)
	{
		auto handle = Util::make_abstract_handle<Widget, T>(std::forward<P>(p)...);
		add_child(handle);
		return static_cast<T *>(handle.get());
	}

	void render(Vulkan::CommandBuffer &cmd);
	Font &get_font(FontSize size);

private:
	UIManager();
	FlatRenderer renderer;
	std::vector<WidgetHandle> widgets;
	std::unique_ptr<Font> fonts[Util::ecast(FontSize::Count)];
};
}
}