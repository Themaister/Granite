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

#include "event.hpp"
#include "vulkan_headers.hpp"

namespace Vulkan
{
class Device;
class ShaderManager;
class WSIPlatform;

class DeviceCreatedEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(DeviceCreatedEvent)

	explicit DeviceCreatedEvent(Device *device_)
		: device(*device_)
	{}

	Device &get_device() const
	{
		return device;
	}

private:
	Device &device;
};

class DeviceShaderModuleReadyEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(DeviceShaderModuleReadyEvent)

	explicit DeviceShaderModuleReadyEvent(Device *device_, ShaderManager *manager_)
		: device(*device_), manager(*manager_)
	{}

	Device &get_device() const
	{
		return device;
	}

	ShaderManager &get_shader_manager() const
	{
		return manager;
	}

private:
	Device &device;
	ShaderManager &manager;
};

class DevicePipelineReadyEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(DevicePipelineReadyEvent)

	explicit DevicePipelineReadyEvent(Device *device_, ShaderManager *manager_)
		: device(*device_), manager(*manager_)
	{}

	Device &get_device() const
	{
		return device;
	}

	ShaderManager &get_shader_manager() const
	{
		return manager;
	}

private:
	Device &device;
	ShaderManager &manager;
};

class SwapchainParameterEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(SwapchainParameterEvent)

	SwapchainParameterEvent(Device *device_,
	                        unsigned width_, unsigned height_,
	                        float aspect_ratio_, unsigned count_,
	                        VkFormat format_, VkColorSpaceKHR color_space_,
	                        VkSurfaceTransformFlagBitsKHR transform_)
		: device(*device_), width(width_), height(height_),
		  aspect_ratio(aspect_ratio_), image_count(count_), format(format_), color_space(color_space_), transform(transform_)
	{}

	Device &get_device() const
	{
		return device;
	}

	unsigned get_width() const
	{
		return width;
	}

	unsigned get_height() const
	{
		return height;
	}

	float get_aspect_ratio() const
	{
		return aspect_ratio;
	}

	unsigned get_image_count() const
	{
		return image_count;
	}

	VkFormat get_format() const
	{
		return format;
	}

	VkColorSpaceKHR get_color_space() const
	{
		return color_space;
	}

	VkSurfaceTransformFlagBitsKHR get_prerotate() const
	{
		return transform;
	}

private:
	Device &device;
	unsigned width;
	unsigned height;
	float aspect_ratio;
	unsigned image_count;
	VkFormat format;
	VkColorSpaceKHR color_space;
	VkSurfaceTransformFlagBitsKHR transform;
};

class SwapchainIndexEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(SwapchainIndexEvent)

	SwapchainIndexEvent(Device *device_, unsigned index_)
		: device(*device_), index(index_)
	{}

	Device &get_device() const
	{
		return device;
	}

	unsigned get_index() const
	{
		return index;
	}

private:
	Device &device;
	unsigned index;
};

class ApplicationWSIPlatformEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(ApplicationWSIPlatformEvent)

	explicit ApplicationWSIPlatformEvent(WSIPlatform &platform_)
		: platform(platform_)
	{}

	WSIPlatform &get_platform() const
	{
		return platform;
	}

private:
	WSIPlatform &platform;
};

class ApplicationWindowFileDropEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(ApplicationWindowFileDropEvent)
	explicit ApplicationWindowFileDropEvent(std::string path_)
		: path(std::move(path_))
	{}

	const std::string &get_path() const
	{
		return path;
	}

private:
	std::string path;
};

class ApplicationWindowTextDropEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(ApplicationWindowTextDropEvent)
	explicit ApplicationWindowTextDropEvent(std::string str_)
		: str(std::move(str_))
	{}

	const std::string &get_text() const
	{
		return str;
	}

private:
	std::string str;
};
}
