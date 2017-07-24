/* Copyright (c) 2017 Hans-Kristian Arntzen
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