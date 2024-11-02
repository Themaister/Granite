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

#include "meshlet.hpp"
#include "command_buffer.hpp"
#include "buffer.hpp"
#include "device.hpp"
#include "filesystem.hpp"

namespace Vulkan
{
namespace Meshlet
{
MeshView create_mesh_view(const Granite::FileMapping &mapping)
{
	MeshView view = {};

	if (mapping.get_size() < sizeof(magic) + sizeof(FormatHeader))
	{
		LOGE("MESHLET2 file too small.\n");
		return view;
	}

	auto *ptr = mapping.data<unsigned char>();
	auto *end_ptr = ptr + mapping.get_size();

	if (memcmp(ptr, magic, sizeof(magic)) != 0)
	{
		LOGE("Invalid MESHLET2 magic.\n");
		return {};
	}

	ptr += sizeof(magic);

	view.format_header = reinterpret_cast<const FormatHeader *>(ptr);
	ptr += sizeof(*view.format_header);

	if (end_ptr - ptr < ptrdiff_t(view.format_header->meshlet_count * sizeof(Bound)))
		return {};
	view.bounds = reinterpret_cast<const Bound *>(ptr);
	ptr += view.format_header->meshlet_count * sizeof(Bound);

	size_t num_bounds_256 = (view.format_header->meshlet_count + ChunkFactor - 1) / ChunkFactor;

	if (end_ptr - ptr < ptrdiff_t(num_bounds_256 * sizeof(Bound)))
		return {};
	view.bounds_256 = reinterpret_cast<const Bound *>(ptr);
	ptr += num_bounds_256 * sizeof(Bound);

	view.num_bounds = view.format_header->meshlet_count;
	view.num_bounds_256 = num_bounds_256;

	if (end_ptr - ptr < ptrdiff_t(view.format_header->meshlet_count * view.format_header->stream_count * sizeof(Stream)))
		return {};
	view.streams = reinterpret_cast<const Stream *>(ptr);
	ptr += view.format_header->meshlet_count * view.format_header->stream_count * sizeof(Stream);

	if (!view.format_header->payload_size_words)
		return {};

	if (end_ptr - ptr < ptrdiff_t(view.format_header->payload_size_words * sizeof(PayloadWord)))
		return {};
	view.payload = reinterpret_cast<const PayloadWord *>(ptr);

	for (uint32_t i = 0, n = view.format_header->meshlet_count; i < n; i++)
	{
		auto counts = view.streams[i * view.format_header->stream_count].u.counts;
		view.total_primitives += counts.prim_count;
		view.total_vertices += counts.vert_count;
	}

	return view;
}

static void upload_indirect_buffer(CommandBuffer &cmd, const Vulkan::Buffer &indirect_buffer, uint32_t alloc_offset,
                                   const MeshView &view, RuntimeStyle runtime_style)
{
	size_t total_padded_meshlets = view.num_bounds_256 * Meshlet::ChunkFactor;
	size_t total_meshlets = view.format_header->meshlet_count;

	if (runtime_style == RuntimeStyle::Meshlet)
	{
		constexpr size_t Stride = sizeof(Meshlet::RuntimeHeaderDecoded) * Meshlet::ChunkFactor;
		auto *indirect = static_cast<Meshlet::RuntimeHeaderDecoded *>(
				cmd.update_buffer(indirect_buffer, alloc_offset * Stride,
				                   view.num_bounds_256 * Stride));

		uint32_t vert_offset = 0;
		uint32_t prim_offset = 0;

		for (uint32_t i = 0; i < total_meshlets; i++)
		{
			auto &counts = view.streams[i * view.format_header->stream_count].u.counts;
			uint32_t prim_count = counts.prim_count;
			uint32_t vert_count = counts.vert_count;

			indirect[i].primitive_offset = prim_offset;
			indirect[i].vertex_offset = vert_offset;
			indirect[i].primitive_count = prim_count;
			indirect[i].vertex_count = vert_count;

			prim_offset += prim_count;
			vert_offset += vert_count;
		}

		memset(indirect + total_meshlets, 0,
		       (total_padded_meshlets - total_meshlets) * sizeof(Meshlet::RuntimeHeaderDecoded));
	}
	else
	{
		constexpr size_t Stride = sizeof(VkDrawIndexedIndirectCommand);
		auto *indirect = static_cast<VkDrawIndexedIndirectCommand *>(
				cmd.update_buffer(indirect_buffer, alloc_offset * Stride,
				                  view.num_bounds_256 * Stride));

		uint32_t vert_offset = 0;
		uint32_t prim_offset = 0;

		for (uint32_t i = 0; i < view.num_bounds_256; i++)
		{
			uint32_t chunks = std::min<uint32_t>(total_meshlets - i * Meshlet::ChunkFactor, Meshlet::ChunkFactor);

			VkDrawIndexedIndirectCommand draw = {};
			draw.firstIndex = 3 * prim_offset;
			draw.vertexOffset = int32_t(vert_offset);
			draw.instanceCount = 1;

			for (uint32_t chunk = 0; chunk < chunks; chunk++)
			{
				auto &counts = view.streams[(i * Meshlet::ChunkFactor + chunk) *
				                            view.format_header->stream_count].u.counts;
				draw.indexCount += counts.prim_count;
				vert_offset += counts.vert_count;
				prim_offset += counts.prim_count;
			}

			draw.indexCount *= 3;
			indirect[i] = draw;
		}
	}
}

bool decode_mesh(CommandBuffer &cmd, const DecodeInfo &info, const MeshView &view)
{
	if (!cmd.get_device().supports_subgroup_size_log2(true, 5, 7))
	{
		LOGE("Device does not support subgroup paths.\n");
		return false;
	}

	if (!info.streams[0])
	{
		LOGE("Decode stream 0 must be set.\n");
		return false;
	}

	if (!info.ibo)
	{
		LOGE("Output IBO must be set.\n");
		return false;
	}

	BufferCreateInfo buf_info = {};
	buf_info.domain = BufferDomain::LinkedDeviceHost;
	buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	buf_info.size = view.format_header->meshlet_count * view.format_header->stream_count * sizeof(*view.streams);
	auto meshlet_stream_buffer = cmd.get_device().create_buffer(buf_info, view.streams);

	bool meshlet_runtime = info.runtime_style == RuntimeStyle::Meshlet;
	cmd.set_program("builtin://shaders/decode/meshlet_decode.comp");

	cmd.enable_subgroup_size_control(true);
	if (cmd.get_device().supports_subgroup_size_log2(true, 5, 5))
		cmd.set_subgroup_size_log2(true, 5, 5);
	else
		cmd.set_subgroup_size_log2(true, 5, 7);

	cmd.set_storage_buffer(0, 0, *meshlet_stream_buffer);
	cmd.set_storage_buffer(0, 1, *info.payload);
	cmd.set_storage_buffer(0, 2, *info.ibo);

	cmd.set_specialization_constant_mask(0xf);
	cmd.set_specialization_constant(0, view.format_header->stream_count);
	cmd.set_specialization_constant(1, (info.flags & DECODE_MODE_UNROLLED_MESH) != 0);
	cmd.set_specialization_constant(2, uint32_t(info.target_style));
	cmd.set_specialization_constant(3, uint32_t(meshlet_runtime));

	for (unsigned i = 0; i < 3; i++)
		cmd.set_storage_buffer(0, 3 + i, info.streams[i] ? *info.streams[i] : *info.streams[0]);

	struct Offsets
	{
		uint32_t primitive_output_offset;
		uint32_t vertex_output_offset;
		uint32_t index_offset;
	};

	std::vector<Offsets> decode_offsets;
	Offsets offsets = {};

	decode_offsets.reserve(view.format_header->meshlet_count);
	for (uint32_t i = 0; i < view.format_header->meshlet_count; i++)
	{
		if (info.runtime_style == RuntimeStyle::MDI && (info.flags & DECODE_MODE_UNROLLED_MESH) == 0)
		{
			uint32_t mdi_start_index = i & ~(Meshlet::ChunkFactor - 1);
			if (mdi_start_index == i)
				offsets.index_offset = 0;
		}

		decode_offsets.push_back(offsets);

		auto &counts = view.streams[i * view.format_header->stream_count].u.counts;
		offsets.primitive_output_offset += counts.prim_count;
		offsets.vertex_output_offset += counts.vert_count;

		if (!meshlet_runtime)
			offsets.index_offset += counts.vert_count;
	}

	buf_info.domain = BufferDomain::LinkedDeviceHost;
	buf_info.size = decode_offsets.size() * sizeof(decode_offsets.front());
	auto output_offsets_buffer = cmd.get_device().create_buffer(buf_info, decode_offsets.data());

	cmd.set_storage_buffer(0, 6, *output_offsets_buffer);

	uint32_t wg_x = (view.format_header->meshlet_count + 7) / 8;

	struct Push
	{
		uint32_t primitive_offset;
		uint32_t vertex_offset;
		uint32_t meshlet_count;
		uint32_t wg_offset;
	} push = {};

	push.primitive_offset = info.push.primitive_offset;
	push.vertex_offset = info.push.vertex_offset;
	push.meshlet_count = view.format_header->meshlet_count;
	push.wg_offset = 0;

	const uint32_t max_wgx = cmd.get_device().get_gpu_properties().limits.maxComputeWorkGroupCount[0];
	for (uint32_t i = 0; i < wg_x; i += max_wgx)
	{
		uint32_t to_dispatch = std::min<uint32_t>(wg_x - i, max_wgx);
		push.wg_offset = i;
		cmd.push_constants(&push, 0, sizeof(push));
		cmd.dispatch(to_dispatch, 1, 1);
	}

	cmd.set_specialization_constant_mask(0);
	cmd.enable_subgroup_size_control(false);

	if (info.indirect)
		upload_indirect_buffer(cmd, *info.indirect, info.indirect_offset, view, info.runtime_style);

	return true;
}
}
}
