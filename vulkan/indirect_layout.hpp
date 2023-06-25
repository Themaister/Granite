/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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

#include "vulkan_headers.hpp"
#include "vulkan_common.hpp"
#include "cookie.hpp"

namespace Vulkan
{
class Device;
class PipelineLayout;

struct IndirectLayoutToken
{
	enum class Type
	{
		Invalid = 0,
		Shader,
		PushConstant,
		VBO,
		IBO,
		Draw,
		DrawIndexed,
		MeshTasks,
	};

	Type type = Type::Invalid;
	uint32_t offset = 0;

	union
	{
		struct
		{
			uint32_t offset;
			uint32_t range;
			const PipelineLayout *layout;
		} push;

		struct
		{
			uint32_t binding;
		} vbo;
	} data = {};
};

class IndirectLayout : public HashedObject<IndirectLayout>
{
public:
	IndirectLayout(Device *device, const IndirectLayoutToken *token, uint32_t num_tokens, uint32_t stride);
	~IndirectLayout();

	VkIndirectCommandsLayoutNV get_layout() const
	{
		return layout;
	}

private:
	friend class Device;

	Device *device;
	VkIndirectCommandsLayoutNV layout;
};
}
