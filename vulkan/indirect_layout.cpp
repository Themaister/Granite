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

#include "indirect_layout.hpp"
#include "device.hpp"
#include "small_vector.hpp"

namespace Vulkan
{
IndirectLayout::IndirectLayout(Device *device_, const IndirectLayoutToken *tokens, uint32_t num_tokens, uint32_t stride)
	: device(device_)
{
	VkIndirectCommandsLayoutCreateInfoNV info = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_NV };
	info.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	info.pStreamStrides = &stride;
	info.streamCount = 1;

	Util::SmallVector<VkIndirectCommandsLayoutTokenNV, 8> nv_tokens;
	nv_tokens.reserve(num_tokens);

	for (uint32_t i = 0; i < num_tokens; i++)
	{
		VkIndirectCommandsLayoutTokenNV token = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV };
		switch (tokens[i].type)
		{
		case IndirectLayoutToken::Type::VBO:
			token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NV;
			token.vertexBindingUnit = tokens[i].data.vbo.binding;
			break;

		case IndirectLayoutToken::Type::IBO:
			token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_NV;
			break;

		case IndirectLayoutToken::Type::PushConstant:
			token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NV;
			token.pushconstantSize = tokens[i].data.push.range;
			token.pushconstantOffset = tokens[i].data.push.offset;
			token.pushconstantPipelineLayout = tokens[i].data.push.layout->get_layout();
			token.pushconstantShaderStageFlags = tokens[i].data.push.layout->get_resource_layout().push_constant_range.stageFlags;
			break;

		case IndirectLayoutToken::Type::Draw:
			token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_NV;
			break;

		case IndirectLayoutToken::Type::DrawIndexed:
			token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_NV;
			break;

		case IndirectLayoutToken::Type::Shader:
			token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_SHADER_GROUP_NV;
			break;

		case IndirectLayoutToken::Type::MeshTasks:
			token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV;
			break;

		case IndirectLayoutToken::Type::Dispatch:
			token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_NV;
			info.pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
			break;

		default:
			LOGE("Invalid token type.\n");
			break;
		}

		token.offset = tokens[i].offset;

		nv_tokens.push_back(token);
	}

	info.pTokens = nv_tokens.data();
	info.tokenCount = num_tokens;
	bind_point = info.pipelineBindPoint;

	auto &table = device->get_device_table();
	if (table.vkCreateIndirectCommandsLayoutNV(device->get_device(), &info, nullptr, &layout) != VK_SUCCESS)
	{
		LOGE("Failed to create indirect layout.\n");
	}
}

IndirectLayout::~IndirectLayout()
{
	device->get_device_table().vkDestroyIndirectCommandsLayoutNV(device->get_device(), layout, nullptr);
}
}
