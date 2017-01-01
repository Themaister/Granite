#pragma once

#include "buffer.hpp"
#include "image.hpp"
#include "intrusive.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "shader.hpp"
#include "vulkan.hpp"

namespace Vulkan
{

enum CommandBufferDirtyBits
{
	COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT = 1 << 0,
	COMMAND_BUFFER_DIRTY_PIPELINE_BIT = 1 << 1,

	COMMAND_BUFFER_DIRTY_VIEWPORT_BIT = 1 << 2,
	COMMAND_BUFFER_DIRTY_SCISSOR_BIT = 1 << 3,
	COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT = 1 << 4,
	COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT = 1 << 5,

	COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT = 1 << 6,

	COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT = 1 << 7,

	COMMAND_BUFFER_DYNAMIC_BITS = COMMAND_BUFFER_DIRTY_VIEWPORT_BIT | COMMAND_BUFFER_DIRTY_SCISSOR_BIT |
	                              COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT |
	                              COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT
};
using CommandBufferDirtyFlags = uint32_t;

class Device;
class CommandBuffer : public IntrusivePtrEnabled<CommandBuffer>
{
public:
	CommandBuffer(Device *device, VkCommandBuffer cmd, VkPipelineCache cache);
	VkCommandBuffer get_command_buffer()
	{
		return cmd;
	}

	bool swapchain_touched() const
	{
		return uses_swapchain;
	}

	void clear_image(const Image &image, const VkClearValue &value);
	void clear_quad(unsigned attachment, const VkClearRect &rect, const VkClearValue &value,
	                VkImageAspectFlags = VK_IMAGE_ASPECT_COLOR_BIT);

	void copy_buffer(const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset,
	                 VkDeviceSize size);
	void copy_buffer(const Buffer &dst, const Buffer &src);

	void copy_buffer_to_image(const Image &image, const Buffer &buffer, VkDeviceSize buffer_offset,
	                          const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
	                          unsigned slice_height, const VkImageSubresourceLayers &subresrouce);

	void copy_image_to_buffer(const Buffer &dst, const Image &src, VkDeviceSize buffer_offset, const VkOffset3D &offset,
	                          const VkExtent3D &extent, unsigned row_length, unsigned slice_height,
	                          const VkImageSubresourceLayers &subresrouce);

	void full_barrier();
	void pixel_barrier();
	void barrier(VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	             VkAccessFlags dst_access);

	void buffer_barrier(const Buffer &buffer, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
	                    VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);

	void image_barrier(const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
	                   VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	                   VkAccessFlags dst_access);
	void image_barrier(const Image &image, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
	                   VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);

	void blit_image(const Image &dst, const Image &src, const VkOffset3D &dst_offset, const VkOffset3D &dst_extent,
	                const VkOffset3D &src_offset, const VkOffset3D &src_extent, unsigned dst_level, unsigned src_level,
	                unsigned dst_base_layer = 0, uint32_t src_base_layer = 0, unsigned num_layers = 1,
	                VkFilter filter = VK_FILTER_LINEAR);

	void generate_mipmap(const Image &image);

	void begin_render_pass(const RenderPassInfo &info);
	void end_render_pass();

	void set_program(Program &program);
	void set_buffer_view(unsigned set, unsigned binding, const BufferView &view);
	void set_input_attachment(unsigned set, unsigned binding, const ImageView &view);
	void set_texture(unsigned set, unsigned binding, const ImageView &view);
	void set_texture(unsigned set, unsigned binding, const ImageView &view, const Sampler &sampler);
	void set_texture(unsigned set, unsigned binding, const ImageView &view, StockSampler sampler);
	void set_storage_texture(unsigned set, unsigned binding, const ImageView &view);
	void set_sampler(unsigned set, unsigned binding, const Sampler &sampler);
	void set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer);
	void set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
	                        VkDeviceSize range);
	void set_storage_buffer(unsigned set, unsigned binding, const Buffer &buffer);
	void set_storage_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
	                        VkDeviceSize range);
	void push_constants(const void *data, VkDeviceSize offset, VkDeviceSize range);

	void *allocate_constant_data(unsigned set, unsigned binding, VkDeviceSize size);
	void *allocate_vertex_data(unsigned binding, VkDeviceSize size, VkDeviceSize stride,
	                           VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);
	void *allocate_index_data(VkDeviceSize size, VkIndexType index_type);

	void *update_buffer(const Buffer &buffer, VkDeviceSize offset, VkDeviceSize size);
	void *update_image(const Image &image, const VkOffset3D &offset, const VkExtent3D &extent, uint32_t row_length,
	                   uint32_t image_height, const VkImageSubresourceLayers &subresource);
	void *update_image(const Image &image, uint32_t row_length = 0, uint32_t image_height = 0);

	void set_viewport(const VkViewport &viewport);
	void set_scissor(const VkRect2D &rect);

	void set_vertex_attrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset);
	void set_vertex_binding(uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride,
	                        VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);
	void set_index_buffer(const Buffer &buffer, VkDeviceSize offset, VkIndexType index_type);

	void draw(uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0,
	          uint32_t first_instance = 0);
	void draw_indexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0,
	                  int32_t vertex_offset = 0, uint32_t first_instance = 0);

	void dispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z);

	void set_opaque_state();
	void set_quad_state();

#define SET_STATIC_STATE(value)                               \
	do                                                        \
	{                                                         \
		if (static_state.state.value != value)                \
		{                                                     \
			static_state.state.value = value;                 \
			set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT); \
		}                                                     \
	} while (0)

#define SET_POTENTIALLY_STATIC_STATE(value)                   \
	do                                                        \
	{                                                         \
		if (potential_static_state.value != value)            \
		{                                                     \
			potential_static_state.value = value;             \
			set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT); \
		}                                                     \
	} while (0)

	inline void set_depth_test(bool depth_test, bool depth_write)
	{
		SET_STATIC_STATE(depth_test);
		SET_STATIC_STATE(depth_write);
	}

	inline void set_depth_compare(VkCompareOp depth_compare)
	{
		SET_STATIC_STATE(depth_compare);
	}

	inline void set_blend_enable(bool blend_enable)
	{
		SET_STATIC_STATE(blend_enable);
	}

	inline void set_blend_factors(VkBlendFactor src_color_blend, VkBlendFactor src_alpha_blend,
	                              VkBlendFactor dst_color_blend, VkBlendFactor dst_alpha_blend)
	{
		SET_STATIC_STATE(src_color_blend);
		SET_STATIC_STATE(dst_color_blend);
		SET_STATIC_STATE(src_alpha_blend);
		SET_STATIC_STATE(dst_alpha_blend);
	}

	inline void set_blend_op(VkBlendOp color_blend_op, VkBlendOp alpha_blend_op)
	{
		SET_STATIC_STATE(color_blend_op);
		SET_STATIC_STATE(alpha_blend_op);
	}

	inline void set_depth_bias(bool depth_bias_enable)
	{
		SET_STATIC_STATE(depth_bias_enable);
	}

	inline void set_stencil_test(bool stencil_test)
	{
		SET_STATIC_STATE(stencil_test);
	}

	inline void set_stencil_front_ops(VkCompareOp stencil_front_compare_op, VkStencilOp stencil_front_pass,
	                                  VkStencilOp stencil_front_fail, VkStencilOp stencil_front_depth_fail)
	{
		SET_STATIC_STATE(stencil_front_compare_op);
		SET_STATIC_STATE(stencil_front_pass);
		SET_STATIC_STATE(stencil_front_fail);
		SET_STATIC_STATE(stencil_front_depth_fail);
	}

	inline void set_stencil_back_ops(VkCompareOp stencil_back_compare_op, VkStencilOp stencil_back_pass,
	                                 VkStencilOp stencil_back_fail, VkStencilOp stencil_back_depth_fail)
	{
		SET_STATIC_STATE(stencil_back_compare_op);
		SET_STATIC_STATE(stencil_back_pass);
		SET_STATIC_STATE(stencil_back_fail);
		SET_STATIC_STATE(stencil_back_depth_fail);
	}

	inline void set_stencil_ops(VkCompareOp stencil_compare_op, VkStencilOp stencil_pass, VkStencilOp stencil_fail,
	                            VkStencilOp stencil_depth_fail)
	{
		set_stencil_front_ops(stencil_compare_op, stencil_pass, stencil_fail, stencil_depth_fail);
		set_stencil_back_ops(stencil_compare_op, stencil_pass, stencil_fail, stencil_depth_fail);
	}

	inline void set_primitive_topology(VkPrimitiveTopology topology)
	{
		SET_STATIC_STATE(topology);
	}

	inline void set_primitive_restart(bool primitive_restart)
	{
		SET_STATIC_STATE(primitive_restart);
	}

	inline void set_multisample_state(bool alpha_to_coverage, bool alpha_to_one = false, bool sample_shading = false)
	{
		SET_STATIC_STATE(alpha_to_coverage);
		SET_STATIC_STATE(alpha_to_one);
		SET_STATIC_STATE(sample_shading);
	}

	inline void set_front_face(VkFrontFace front_face)
	{
		SET_STATIC_STATE(front_face);
	}

	inline void set_cull_mode(VkCullModeFlags cull_mode)
	{
		SET_STATIC_STATE(cull_mode);
	}

	inline void set_blend_constants(const float blend_constants[4])
	{
		SET_POTENTIALLY_STATIC_STATE(blend_constants[0]);
		SET_POTENTIALLY_STATIC_STATE(blend_constants[1]);
		SET_POTENTIALLY_STATIC_STATE(blend_constants[2]);
		SET_POTENTIALLY_STATIC_STATE(blend_constants[3]);
	}

#define SET_DYNAMIC_STATE(state, flags)   \
	do                                    \
	{                                     \
		if (dynamic_state.state != state) \
		{                                 \
			dynamic_state.state = state;  \
			set_dirty(flags);             \
		}                                 \
	} while (0)

	inline void set_depth_bias(float depth_bias_constant, float depth_bias_slope)
	{
		SET_DYNAMIC_STATE(depth_bias_constant, COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT);
		SET_DYNAMIC_STATE(depth_bias_slope, COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT);
	}

	inline void set_stencil_front_reference(uint8_t front_compare_mask, uint8_t front_write_mask,
	                                        uint8_t front_reference)
	{
		SET_DYNAMIC_STATE(front_compare_mask, COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT);
		SET_DYNAMIC_STATE(front_write_mask, COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT);
		SET_DYNAMIC_STATE(front_reference, COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT);
	}

	inline void set_stencil_back_reference(uint8_t back_compare_mask, uint8_t back_write_mask, uint8_t back_reference)
	{
		SET_DYNAMIC_STATE(back_compare_mask, COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT);
		SET_DYNAMIC_STATE(back_write_mask, COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT);
		SET_DYNAMIC_STATE(back_reference, COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT);
	}

	inline void set_stencil_reference(uint8_t compare_mask, uint8_t write_mask, uint8_t reference)
	{
		set_stencil_front_reference(compare_mask, write_mask, reference);
		set_stencil_back_reference(compare_mask, write_mask, reference);
	}

private:
	Device *device;
	VkCommandBuffer cmd;
	VkPipelineCache cache;

	const Framebuffer *framebuffer = nullptr;
	const RenderPass *render_pass = nullptr;
	RenderPassInfo render_pass_info;

	struct AttribState
	{
		uint32_t binding;
		VkFormat format;
		VkDeviceSize offset;
	};
	AttribState attribs[VULKAN_NUM_VERTEX_ATTRIBS] = {};

	VkBuffer vbo_buffers[VULKAN_NUM_VERTEX_BUFFERS] = {};
	VkDeviceSize vbo_offsets[VULKAN_NUM_VERTEX_BUFFERS] = {};
	VkDeviceSize vbo_strides[VULKAN_NUM_VERTEX_BUFFERS] = {};
	VkVertexInputRate vbo_input_rates[VULKAN_NUM_VERTEX_BUFFERS] = {};

	struct Binding
	{
		union {
			VkDescriptorBufferInfo buffer;
			VkDescriptorImageInfo image;
			VkBufferView buffer_view;
		};
	};
	Binding bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};
	uint64_t cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};
	uint64_t secondary_cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};
	uint8_t push_constant_data[VULKAN_PUSH_CONSTANT_SIZE] = {};

	struct IndexState
	{
		VkBuffer buffer;
		VkDeviceSize offset;
		VkIndexType index_type;
	};
	IndexState index = {};

	VkPipeline current_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout current_pipeline_layout = VK_NULL_HANDLE;
	PipelineLayout *current_layout = nullptr;
	Program *current_program = nullptr;

	VkViewport viewport = {};
	VkRect2D scissor = {};

	CommandBufferDirtyFlags dirty = ~0u;
	uint32_t dirty_sets = 0;
	uint32_t dirty_vbos = 0;
	uint32_t active_vbos = 0;
	bool uses_swapchain = false;
	bool is_compute = true;

	void set_dirty(CommandBufferDirtyFlags flags)
	{
		dirty |= flags;
	}

	CommandBufferDirtyFlags get_and_clear(CommandBufferDirtyFlags flags)
	{
		auto mask = dirty & flags;
		dirty &= ~flags;
		return mask;
	}

#define COMPARE_OP_BITS 3
#define STENCIL_OP_BITS 3
#define BLEND_FACTOR_BITS 5
#define BLEND_OP_BITS 3
#define CULL_MODE_BITS 2
#define FRONT_FACE_BITS 1
	union PipelineState {
		struct
		{
			// Depth state.
			unsigned depth_write : 1;
			unsigned depth_test : 1;
			unsigned blend_enable : 1;

			unsigned cull_mode : CULL_MODE_BITS;
			unsigned front_face : FRONT_FACE_BITS;
			unsigned depth_bias_enable : 1;

			unsigned depth_compare : COMPARE_OP_BITS;

			unsigned stencil_test : 1;
			unsigned stencil_front_fail : STENCIL_OP_BITS;
			unsigned stencil_front_pass : STENCIL_OP_BITS;
			unsigned stencil_front_depth_fail : STENCIL_OP_BITS;
			unsigned stencil_front_compare_op : COMPARE_OP_BITS;
			unsigned stencil_back_fail : STENCIL_OP_BITS;
			unsigned stencil_back_pass : STENCIL_OP_BITS;
			unsigned stencil_back_depth_fail : STENCIL_OP_BITS;
			unsigned stencil_back_compare_op : COMPARE_OP_BITS;

			unsigned alpha_to_coverage : 1;
			unsigned alpha_to_one : 1;
			unsigned sample_shading : 1;

			unsigned src_color_blend : BLEND_FACTOR_BITS;
			unsigned dst_color_blend : BLEND_FACTOR_BITS;
			unsigned color_blend_op : BLEND_OP_BITS;
			unsigned src_alpha_blend : BLEND_FACTOR_BITS;
			unsigned dst_alpha_blend : BLEND_FACTOR_BITS;
			unsigned alpha_blend_op : BLEND_OP_BITS;
			unsigned primitive_restart : 1;
			unsigned topology : 4;

			uint32_t write_mask;
		} state;
		uint32_t words[4];
	} static_state = {};

	struct PotentialState
	{
		float blend_constants[4];
	} potential_static_state = {};
	static_assert(sizeof(static_state.words) >= sizeof(static_state.state),
	              "Hashable pipeline state is not large enough!");

	struct DynamicState
	{
		float depth_bias_constant = 0.0f;
		float depth_bias_slope = 0.0f;
		uint8_t front_compare_mask = 0;
		uint8_t front_write_mask = 0;
		uint8_t front_reference = 0;
		uint8_t back_compare_mask = 0;
		uint8_t back_write_mask = 0;
		uint8_t back_reference = 0;
		bool depth_bias_enable = false;
		bool stencil_enable = false;
	} dynamic_state;

	void flush_render_state();
	VkPipeline build_graphics_pipeline(Hash hash);
	void flush_graphics_pipeline();
	void flush_descriptor_sets();
	void begin_graphics();
	void flush_descriptor_set(uint32_t set);
	void begin_compute();
	void begin_context();

	void flush_compute_state();
};

using CommandBufferHandle = IntrusivePtr<CommandBuffer>;
}
