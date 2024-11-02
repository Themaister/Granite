/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include <functional>
#include <string>
#include "logging.hpp"

namespace Util
{
class MessageQueueInterface : public LoggingInterface
{
public:
	virtual ~MessageQueueInterface() = default;
};
}

namespace Granite
{
class FilesystemInterface
{
public:
	virtual ~FilesystemInterface() = default;
	virtual bool load_text_file(const std::string &path, std::string &str) = 0;
};

class AssetManagerInterface
{
public:
	virtual ~AssetManagerInterface() = default;
};

class MaterialManagerInterface
{
public:
	virtual ~MaterialManagerInterface() = default;
	virtual void iterate(AssetManagerInterface *iface) = 0;
};

class ThreadGroupInterface
{
public:
	virtual ~ThreadGroupInterface() = default;
	virtual void start(unsigned foreground_count, unsigned background_count,
	                   const std::function<void()> &cb) = 0;
	virtual void set_thread_context() = 0;
};

class EventManagerInterface
{
public:
	virtual ~EventManagerInterface() = default;
};

class CommonRendererDataInterface
{
public:
	virtual ~CommonRendererDataInterface() = default;
};

class PhysicsSystemInterface
{
public:
	virtual ~PhysicsSystemInterface() = default;
};

class TouchDownEvent;
class TouchUpEvent;
class MouseMoveEvent;
class KeyboardEvent;
class OrientationEvent;
class TouchGestureEvent;
class MouseButtonEvent;
class JoypadButtonEvent;
class JoypadAxisEvent;

namespace UI
{
class UIManagerInterface
{
public:
	virtual ~UIManagerInterface() = default;
	virtual bool filter_input_event(const TouchDownEvent &e) = 0;
	virtual bool filter_input_event(const TouchUpEvent &e) = 0;
	virtual bool filter_input_event(const MouseMoveEvent &e) = 0;
	virtual bool filter_input_event(const KeyboardEvent &e) = 0;
	virtual bool filter_input_event(const OrientationEvent &e) = 0;
	virtual bool filter_input_event(const TouchGestureEvent &e) = 0;
	virtual bool filter_input_event(const MouseButtonEvent &e) = 0;
	virtual bool filter_input_event(const JoypadButtonEvent &e) = 0;
	virtual bool filter_input_event(const JoypadAxisEvent &e) = 0;
};
}

namespace Audio
{
class BackendInterface
{
public:
	virtual ~BackendInterface() = default;
	virtual bool start() = 0;
	virtual bool stop() = 0;
};

class MixerInterface
{
public:
	virtual ~MixerInterface() = default;
	virtual void event_start(EventManagerInterface &event_manager) = 0;
	virtual void event_stop(EventManagerInterface &event_manager) = 0;
};
}
}
