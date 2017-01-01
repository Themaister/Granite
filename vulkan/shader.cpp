#include "shader.hpp"
#include "device.hpp"
#include "spirv_cross.hpp"

using namespace std;
using namespace spirv_cross;

namespace Vulkan
{
PipelineLayout::PipelineLayout(Device *device, const CombinedResourceLayout &layout)
    : Cookie(device)
    , device(device)
    , layout(layout)
{
	VkDescriptorSetLayout layouts[VULKAN_NUM_DESCRIPTOR_SETS] = {};
	unsigned num_sets = 0;
	for (unsigned i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
	{
		set_allocators[i] = device->request_descriptor_set_allocator(layout.sets[i]);
		layouts[i] = set_allocators[i]->get_layout();
		if (layout.descriptor_set_mask & (1u << i))
			num_sets = i + 1;
	}

	unsigned num_ranges = 0;
	VkPushConstantRange ranges[static_cast<unsigned>(ShaderStage::Count)];

	for (auto &range : layout.ranges)
	{
		if (range.size != 0)
		{
			bool unique = true;
			for (unsigned i = 0; i < num_ranges; i++)
			{
				// Try to merge equivalent ranges for multiple stages.
				if (ranges[i].offset == range.offset && ranges[i].size == range.size)
				{
					unique = false;
					ranges[i].stageFlags |= range.stageFlags;
					break;
				}
			}

			if (unique)
				ranges[num_ranges++] = range;
		}
	}

	memcpy(this->layout.ranges, ranges, num_ranges * sizeof(ranges[0]));
	this->layout.num_ranges = num_ranges;

	VkPipelineLayoutCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	if (num_sets)
	{
		info.setLayoutCount = num_sets;
		info.pSetLayouts = layouts;
	}

	if (num_ranges)
	{
		info.pushConstantRangeCount = num_ranges;
		info.pPushConstantRanges = ranges;
	}

	if (vkCreatePipelineLayout(device->get_device(), &info, nullptr, &pipe_layout) != VK_SUCCESS)
		LOG("Failed to create pipeline layout.\n");
}

PipelineLayout::~PipelineLayout()
{
	if (pipe_layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(device->get_device(), pipe_layout, nullptr);
}

Shader::Shader(VkDevice device, ShaderStage stage, const uint32_t *data, size_t size)
    : device(device)
    , stage(stage)
{
	VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	info.codeSize = size;
	info.pCode = data;

	if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
		LOG("Failed to create shader module.\n");

	vector<uint32_t> code(data, data + size / sizeof(uint32_t));
	Compiler compiler(move(code));

	auto resources = compiler.get_shader_resources();
	for (auto &image : resources.sampled_images)
	{
		auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		auto &type = compiler.get_type(image.base_type_id);
		if (type.image.dim == spv::DimBuffer)
			layout.sets[set].sampled_buffer_mask |= 1u << binding;
		else
			layout.sets[set].sampled_image_mask |= 1u << binding;
		layout.sets[set].stages |= 1u << static_cast<unsigned>(stage);
	}

	for (auto &image : resources.subpass_inputs)
	{
		auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		layout.sets[set].input_attachment_mask |= 1u << binding;
		layout.sets[set].stages |= 1u << static_cast<unsigned>(stage);
	}

	for (auto &image : resources.storage_images)
	{
		auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		layout.sets[set].storage_image_mask |= 1u << binding;
		layout.sets[set].stages |= 1u << static_cast<unsigned>(stage);
	}

	for (auto &buffer : resources.uniform_buffers)
	{
		auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
		layout.sets[set].uniform_buffer_mask |= 1u << binding;
		layout.sets[set].stages |= 1u << static_cast<unsigned>(stage);
	}

	for (auto &buffer : resources.storage_buffers)
	{
		auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
		layout.sets[set].storage_buffer_mask |= 1u << binding;
		layout.sets[set].stages |= 1u << static_cast<unsigned>(stage);
	}

	if (stage == ShaderStage::Vertex)
	{
		for (auto &attrib : resources.stage_inputs)
		{
			auto location = compiler.get_decoration(attrib.id, spv::DecorationLocation);
			layout.attribute_mask |= 1u << location;
		}
	}

	if (!resources.push_constant_buffers.empty())
	{
#ifdef VULKAN_DEBUG
		// The validation layers are too conservative here, but this is just a performance pessimization.
		size_t size =
		    compiler.get_declared_struct_size(compiler.get_type(resources.push_constant_buffers.front().base_type_id));
		layout.push_constant_offset = 0;
		layout.push_constant_range = size;
#else
		auto ranges = compiler.get_active_buffer_ranges(resources.push_constant_buffers.front().id);
		size_t minimum = ~0u;
		size_t maximum = 0;
		if (!ranges.empty())
		{
			for (auto &range : ranges)
			{
				minimum = min(minimum, range.offset);
				maximum = max(maximum, range.offset + range.range);
			}
			layout.push_constant_offset = minimum;
			layout.push_constant_range = maximum - minimum;
		}
#endif
	}
}

Shader::~Shader()
{
	if (module)
		vkDestroyShaderModule(device, module, nullptr);
}

void Program::set_shader(ShaderHandle handle)
{
	shaders[static_cast<unsigned>(handle->get_stage())] = handle;
}

Program::Program(Device *device)
    : Cookie(device)
    , device(device)
{
}

VkPipeline Program::get_graphics_pipeline(Hash hash) const
{
	auto itr = graphics_pipelines.find(hash);
	if (itr != end(graphics_pipelines))
		return itr->second;
	else
		return VK_NULL_HANDLE;
}

void Program::add_graphics_pipeline(Hash hash, VkPipeline pipeline)
{
	VK_ASSERT(graphics_pipelines[hash] == VK_NULL_HANDLE);
	graphics_pipelines[hash] = pipeline;
}

Program::~Program()
{
	if (compute_pipeline != VK_NULL_HANDLE)
		device->destroy_pipeline(compute_pipeline);

	for (auto &pipe : graphics_pipelines)
		device->destroy_pipeline(pipe.second);
}
}
