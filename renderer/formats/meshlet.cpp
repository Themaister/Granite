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

#include "meshlet.hpp"
#include "command_buffer.hpp"
#include "buffer.hpp"
#include "device.hpp"

namespace Granite
{
namespace SceneFormats
{
namespace Meshlet
{
MeshView create_mesh_view(const FileMapping &mapping)
{
	MeshView view = {};

	if (mapping.get_size() < sizeof(magic) + sizeof(FormatHeader))
	{
		LOGE("MESHLET1 file too small.\n");
		return view;
	}

	auto *ptr = mapping.data<unsigned char>();
	auto *end_ptr = ptr + mapping.get_size();

	if (memcmp(ptr, magic, sizeof(magic)) != 0)
	{
		LOGE("Invalid MESHLET1 magic.\n");
		return {};
	}

	ptr += sizeof(magic);

	view.format_header = reinterpret_cast<const FormatHeader *>(ptr);
	ptr += sizeof(*view.format_header);

	if (end_ptr - ptr < ptrdiff_t(view.format_header->meshlet_count * sizeof(Header)))
		return {};
	view.headers = reinterpret_cast<const Header *>(ptr);
	ptr += view.format_header->meshlet_count * sizeof(Header);

	if (end_ptr - ptr < ptrdiff_t(view.format_header->meshlet_count * sizeof(Bound)))
		return {};
	view.bounds = reinterpret_cast<const Bound *>(ptr);
	ptr += view.format_header->meshlet_count * sizeof(Bound);

	if (end_ptr - ptr < ptrdiff_t(view.format_header->meshlet_count * view.format_header->u32_stream_count * sizeof(Stream)))
		return {};
	view.streams = reinterpret_cast<const Stream *>(ptr);
	ptr += view.format_header->meshlet_count * view.format_header->u32_stream_count * sizeof(Stream);

	if (!view.format_header->payload_size_words)
		return {};

	if (end_ptr - ptr < ptrdiff_t(view.format_header->payload_size_words * sizeof(uint32_t)))
		return {};
	view.payload = reinterpret_cast<const uint32_t *>(ptr);

	for (uint32_t i = 0, n = view.format_header->meshlet_count; i < n; i++)
	{
		view.total_primitives += view.headers[i].num_primitives_minus_1 + 1;
		view.total_vertices += view.headers[i].num_attributes_minus_1 + 1;
	}

	return view;
}

bool decode_mesh(Vulkan::CommandBuffer &cmd,
                 const Vulkan::Buffer &ibo, uint64_t ibo_offset,
                 const Vulkan::Buffer &vbo, uint64_t vbo_offset,
                 const Vulkan::Buffer &payload, uint64_t payload_offset,
                 const MeshView &view)
{
	// TODO: Implement LDS fallback.
	if (!cmd.get_device().supports_subgroup_size_log2(true, 5, 5))
	{
		LOGE("Device does not support Wave32.\n");
		return false;
	}

	const uint32_t u32_stride = view.format_header->u32_stream_count - 1;

	Vulkan::BufferCreateInfo buf_info = {};
	buf_info.domain = Vulkan::BufferDomain::LinkedDeviceHost;
	buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	buf_info.size = view.format_header->meshlet_count * sizeof(*view.headers);
	auto meshlet_meta_buffer = cmd.get_device().create_buffer(buf_info, view.headers);

	buf_info.size = view.format_header->meshlet_count * view.format_header->u32_stream_count * sizeof(*view.streams);
	auto meshlet_stream_buffer = cmd.get_device().create_buffer(buf_info, view.streams);

	std::vector<uvec2> output_offset_strides;
	output_offset_strides.reserve(view.format_header->meshlet_count * view.format_header->u32_stream_count);

	uint32_t index_count = 0;
	for (uint32_t i = 0; i < view.format_header->meshlet_count; i++)
	{
		output_offset_strides.emplace_back(index_count, 0);
		index_count += view.headers[i].num_primitives_minus_1 + 1;
		for (uint32_t j = 1; j < view.format_header->u32_stream_count; j++)
			output_offset_strides.emplace_back(view.headers[i].base_vertex_offset * u32_stride + (j - 1), u32_stride);
	}

	buf_info.domain = Vulkan::BufferDomain::LinkedDeviceHost;
	buf_info.size = output_offset_strides.size() * sizeof(uvec2);
	auto output_offset_strides_buffer = cmd.get_device().create_buffer(buf_info, output_offset_strides.data());

	cmd.set_program("builtin://shaders/decode/meshlet_decode.comp");
	cmd.enable_subgroup_size_control(true);
	cmd.set_subgroup_size_log2(true, 5, 5);

	cmd.set_storage_buffer(0, 0, *meshlet_meta_buffer);
	cmd.set_storage_buffer(0, 1, *meshlet_stream_buffer);
	cmd.set_storage_buffer(0, 2, vbo, vbo_offset, view.total_vertices * u32_stride * sizeof(uint32_t));
	cmd.set_storage_buffer(0, 3, ibo, ibo_offset, view.total_primitives * 3 * sizeof(uint32_t));
	cmd.set_storage_buffer(0, 4, payload, payload_offset, view.format_header->payload_size_words * sizeof(uint32_t));
	cmd.set_storage_buffer(0, 5, *output_offset_strides_buffer);
	cmd.set_specialization_constant_mask(1);
	cmd.set_specialization_constant(0, view.format_header->u32_stream_count);

	// TODO: Split dispatches for big chungus meshes.
	// (Starts to become a problem around 8-16 million primitives per dispatch).
	if (view.format_header->meshlet_count > cmd.get_device().get_gpu_properties().limits.maxComputeWorkGroupCount[0])
	{
		LOGW("Exceeding workgroup limit (%u > %u).\n", view.format_header->meshlet_count,
		     cmd.get_device().get_gpu_properties().limits.maxComputeWorkGroupCount[0]);
	}

	cmd.dispatch(view.format_header->meshlet_count, 1, 1);
	cmd.set_specialization_constant_mask(0);
	return true;
}
}
}
}
