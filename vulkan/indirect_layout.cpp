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

#include "indirect_layout.hpp"
#include "device.hpp"

namespace Vulkan
{
IndirectLayout::IndirectLayout(Device *device_,
                               const PipelineLayout *pipeline_layout, const IndirectLayoutToken *tokens,
                               uint32_t num_tokens, uint32_t stride)
	: device(device_)
{
	VkIndirectCommandsLayoutCreateInfoEXT info = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_EXT };
	info.indirectStride = stride;
	info.pipelineLayout = pipeline_layout ? pipeline_layout->get_layout() : VK_NULL_HANDLE;
	info.flags = VK_INDIRECT_COMMANDS_LAYOUT_USAGE_EXPLICIT_PREPROCESS_BIT_EXT |
	             VK_INDIRECT_COMMANDS_LAYOUT_USAGE_UNORDERED_SEQUENCES_BIT_EXT;

	Util::SmallVector<VkIndirectCommandsLayoutTokenEXT, 8> ext_tokens;
	Util::SmallVector<VkIndirectCommandsVertexBufferTokenEXT, 8> vbo_tokens;
	Util::SmallVector<VkIndirectCommandsPushConstantTokenEXT, 8> push_tokens;
	VkIndirectCommandsIndexBufferTokenEXT ibo_token;
	VkIndirectCommandsExecutionSetTokenEXT exec_token;
	ext_tokens.reserve(num_tokens);
	vbo_tokens.reserve(num_tokens);
	push_tokens.reserve(num_tokens);

	for (uint32_t i = 0; i < num_tokens; i++)
	{
		VkIndirectCommandsLayoutTokenEXT token = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT };
		switch (tokens[i].type)
		{
		case IndirectLayoutToken::Type::VBO:
			token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_EXT;
			vbo_tokens.emplace_back();
			token.data.pVertexBuffer = &vbo_tokens.back();
			vbo_tokens.back().vertexBindingUnit = tokens[i].data.vbo.binding;
			break;

		case IndirectLayoutToken::Type::IBO:
			token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_EXT;
			token.data.pIndexBuffer = &ibo_token;
			ibo_token.mode = VK_INDIRECT_COMMANDS_INPUT_MODE_VULKAN_INDEX_BUFFER_EXT;
			break;

		case IndirectLayoutToken::Type::PushConstant:
		case IndirectLayoutToken::Type::SequenceCount:
			token.type = tokens[i].type == IndirectLayoutToken::Type::PushConstant ?
			             VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT :
			             VK_INDIRECT_COMMANDS_TOKEN_TYPE_SEQUENCE_INDEX_EXT;

			push_tokens.emplace_back();
			token.data.pPushConstant = &push_tokens.back();
			VK_ASSERT(pipeline_layout->get_layout());
			push_tokens.back().updateRange.size = tokens[i].data.push.range;
			push_tokens.back().updateRange.offset = tokens[i].data.push.offset;
			push_tokens.back().updateRange.stageFlags =
					pipeline_layout->get_resource_layout().push_constant_range.stageFlags;
			break;

		case IndirectLayoutToken::Type::Draw:
			info.shaderStages |= VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_EXT;
			break;

		case IndirectLayoutToken::Type::DrawIndexed:
			token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_EXT;
			info.shaderStages |= VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			break;

		case IndirectLayoutToken::Type::Shader:
			token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT;
			token.data.pExecutionSet = &exec_token;
			break;

		case IndirectLayoutToken::Type::MeshTasks:
			token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_EXT;
			info.shaderStages |= VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			break;

		case IndirectLayoutToken::Type::Dispatch:
			token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT;
			info.shaderStages |= VK_SHADER_STAGE_COMPUTE_BIT;
			break;

		default:
			LOGE("Invalid token type.\n");
			break;
		}

		token.offset = tokens[i].offset;

		ext_tokens.push_back(token);
	}

	info.pTokens = ext_tokens.data();
	info.tokenCount = num_tokens;
	exec_token.shaderStages = info.shaderStages;
	stages = info.shaderStages;

	auto &table = device->get_device_table();
	if (table.vkCreateIndirectCommandsLayoutEXT(device->get_device(), &info, nullptr, &layout) != VK_SUCCESS)
	{
		LOGE("Failed to create indirect layout.\n");
	}
}

IndirectLayout::~IndirectLayout()
{
	device->get_device_table().vkDestroyIndirectCommandsLayoutEXT(device->get_device(), layout, nullptr);
}
}
