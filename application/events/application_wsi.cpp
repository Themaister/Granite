/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#include "application_wsi.hpp"
#include "application_wsi_events.hpp"
#include "application_events.hpp"
#include "global_managers.hpp"
#include "event.hpp"

namespace Granite
{
GraniteWSIPlatform::GraniteWSIPlatform()
{
	input_tracker.set_input_handler(this);
}

void GraniteWSIPlatform::event_swapchain_created(Vulkan::Device *device, unsigned width, unsigned height,
                                                 float aspect_ratio, size_t image_count, VkFormat format,
                                                 VkSurfaceTransformFlagBitsKHR transform)
{
	auto *em = GRANITE_EVENT_MANAGER();
	if (em)
		em->enqueue_latched<Vulkan::SwapchainParameterEvent>(device, width, height, aspect_ratio, image_count, format, transform);
}

void GraniteWSIPlatform::event_swapchain_destroyed()
{
	auto *em = GRANITE_EVENT_MANAGER();
	if (em)
		em->dequeue_all_latched(Vulkan::SwapchainParameterEvent::get_type_id());
}

void GraniteWSIPlatform::event_device_created(Vulkan::Device *device)
{
	auto *em = GRANITE_EVENT_MANAGER();
	if (em)
		em->enqueue_latched<Vulkan::DeviceCreatedEvent>(device);
}

void GraniteWSIPlatform::event_device_destroyed()
{
	auto *em = GRANITE_EVENT_MANAGER();
	if (em)
		em->dequeue_all_latched(Vulkan::DeviceCreatedEvent::get_type_id());
}

void GraniteWSIPlatform::event_swapchain_index(Vulkan::Device *device, unsigned index)
{
	auto *em = GRANITE_EVENT_MANAGER();
	if (em)
	{
		em->dequeue_all_latched(Vulkan::SwapchainIndexEvent::get_type_id());
		em->enqueue_latched<Vulkan::SwapchainIndexEvent>(device, index);
	}
}

void GraniteWSIPlatform::event_frame_tick(double, double)
{
}

void GraniteWSIPlatform::event_display_timing_stutter(uint32_t current_serial, uint32_t observed_serial,
                                                      unsigned dropped_frames)
{
	auto *em = GRANITE_EVENT_MANAGER();
	if (em)
		em->dispatch_inline(Vulkan::DisplayTimingStutterEvent{current_serial, observed_serial, dropped_frames});
}

template <typename T>
void GraniteWSIPlatform::dispatch_template(const T &t)
{
	if (auto *em = GRANITE_EVENT_MANAGER())
		em->dispatch_inline(t);
}

template <typename T>
void GraniteWSIPlatform::dispatch_template_filter(const T &t)
{
	auto *ui = Global::ui_manager();
	if (ui && !ui->filter_input_event(t))
		return;
	dispatch_template(t);
}

void GraniteWSIPlatform::dispatch(const TouchDownEvent &e)
{
	dispatch_template_filter(e);
}

void GraniteWSIPlatform::dispatch(const TouchUpEvent &e)
{
	dispatch_template_filter(e);
}

void GraniteWSIPlatform::dispatch(const TouchGestureEvent &e)
{
	dispatch_template_filter(e);
}

void GraniteWSIPlatform::dispatch(const JoypadButtonEvent &e)
{
	dispatch_template_filter(e);
}

void GraniteWSIPlatform::dispatch(const JoypadAxisEvent &e)
{
	dispatch_template_filter(e);
}

void GraniteWSIPlatform::dispatch(const KeyboardEvent &e)
{
	dispatch_template_filter(e);
}

void GraniteWSIPlatform::dispatch(const OrientationEvent &e)
{
	dispatch_template_filter(e);
}

void GraniteWSIPlatform::dispatch(const MouseButtonEvent &e)
{
	dispatch_template_filter(e);
}

void GraniteWSIPlatform::dispatch(const MouseMoveEvent &e)
{
	dispatch_template_filter(e);
}

void GraniteWSIPlatform::dispatch(const JoypadStateEvent &e)
{
	dispatch_template(e);
}

void GraniteWSIPlatform::dispatch(const InputStateEvent &e)
{
	dispatch_template(e);
}

void GraniteWSIPlatform::dispatch(const JoypadConnectionEvent &e)
{
	dispatch_template(e);
}
}
