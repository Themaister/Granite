/* Copyright (c) 2017-2025 Hans-Kristian Arntzen
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

#include "cookie.hpp"
#include "vulkan_common.hpp"
#include "memory_allocator.hpp"
#include "buffer.hpp"

namespace Vulkan
{
class RTAS;
class Device;

struct RTASDeleter
{
	void operator()(RTAS *rtas);
};

enum class BLASMode
{
	Static, // fast trace, compactable, not updateable
	Skinned // fast update, updateable
};

enum class BuildMode
{
	Build,
	Update
};

struct BottomRTASGeometry
{
	VkFormat format;
	VkDeviceAddress vbo;
	uint32_t num_vertices;
	uint32_t stride;

	VkDeviceAddress ibo;
	VkIndexType index_type;
	uint32_t num_primitives;

	VkDeviceAddress transform;
};

struct BottomRTASCreateInfo
{
	BLASMode mode;
	const BottomRTASGeometry *geometries;
	size_t count;
};

struct RTASInstance
{
	// One of the two.
	const VkAccelerationStructureInstanceKHR *instance;
	VkDeviceAddress bda;
};

struct TopRTASCreateInfo
{
	const RTASInstance *instances;
	size_t count;
};

class RTAS : public Util::IntrusivePtrEnabled<RTAS, RTASDeleter, HandleCounter>,
             public Cookie
{
public:
	friend struct RTASDeleter;
	~RTAS();

	inline VkAccelerationStructureKHR get_rtas() const
	{
		return rtas;
	}

	inline VkAccelerationStructureTypeKHR get_type() const
	{
		return type;
	}

	inline VkDeviceAddress get_device_address() const
	{
		return bda;
	}

	VkDeviceSize get_scratch_size(BuildMode mode) const;

private:
	friend class Util::ObjectPool<RTAS>;
	RTAS(Device *device, VkAccelerationStructureKHR rtas,
	     VkAccelerationStructureTypeKHR type, BufferHandle backing,
		 VkDeviceSize build_size, VkDeviceSize update_size);
	Device *device;
	VkAccelerationStructureKHR rtas;
	VkAccelerationStructureTypeKHR type;
	BufferHandle backing;
	VkDeviceSize build_size;
	VkDeviceSize update_size;
	VkDeviceAddress bda = 0;
};

using RTASHandle = Util::IntrusivePtr<RTAS>;
}

