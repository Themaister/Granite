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

	if (end_ptr - ptr < ptrdiff_t(view.format_header->meshlet_count * sizeof(Header)))
		return {};
	view.headers = reinterpret_cast<const Header *>(ptr);
	ptr += view.format_header->meshlet_count * sizeof(Header);

	if (end_ptr - ptr < ptrdiff_t(view.format_header->meshlet_count * sizeof(Bound)))
		return {};
	view.bounds = reinterpret_cast<const Bound *>(ptr);
	ptr += view.format_header->meshlet_count * sizeof(Bound);

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
		auto offsets = view.streams[i * view.format_header->stream_count].u.offsets[NumChunks];
		view.total_primitives += offsets.prim_offset;
		view.total_vertices += offsets.attr_offset;
	}

	return view;
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

	cmd.push_constants(&info.push, 0, sizeof(info.push));

	BufferCreateInfo buf_info = {};
	buf_info.domain = BufferDomain::LinkedDeviceHost;
	buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	buf_info.size = view.format_header->meshlet_count * sizeof(*view.headers);
	auto meshlet_meta_buffer = cmd.get_device().create_buffer(buf_info, view.headers);

	buf_info.size = view.format_header->meshlet_count * view.format_header->stream_count * sizeof(*view.streams);
	auto meshlet_stream_buffer = cmd.get_device().create_buffer(buf_info, view.streams);

	std::vector<uint32_t> decode_offsets;

	bool meshlet_runtime = info.runtime_style == RuntimeStyle::Meshlet;
	cmd.set_program("builtin://shaders/decode/meshlet_decode.comp");

	cmd.enable_subgroup_size_control(true);
	cmd.set_subgroup_size_log2(true, 5, 7);

	cmd.set_storage_buffer(0, 0, *meshlet_meta_buffer);
	cmd.set_storage_buffer(0, 1, *meshlet_stream_buffer);
	cmd.set_storage_buffer(0, 2, *info.payload);
	cmd.set_storage_buffer(0, 3, *info.ibo);

	cmd.set_specialization_constant_mask(0xf);
	cmd.set_specialization_constant(0, view.format_header->stream_count);
	cmd.set_specialization_constant(1, (info.flags & DECODE_MODE_UNROLLED_MESH) != 0);
	cmd.set_specialization_constant(2, uint32_t(info.target_style));
	cmd.set_specialization_constant(3, uint32_t(meshlet_runtime));

	for (unsigned i = 0; i < 3; i++)
		cmd.set_storage_buffer(0, 4 + i, *info.streams[0]);

	switch (info.target_style)
	{
	case MeshStyle::Skinned:
		cmd.set_storage_buffer(0, 6, *info.streams[2]);
		// Fallthrough
	case MeshStyle::Textured:
		cmd.set_storage_buffer(0, 5, *info.streams[1]);
		// Fallthrough
	case MeshStyle::Wireframe:
		cmd.set_storage_buffer(0, 4, *info.streams[0]);
		break;

	default:
		return false;
	}

	decode_offsets.reserve(view.format_header->meshlet_count);
	uint32_t index_count = 0;
	for (uint32_t i = 0; i < view.format_header->meshlet_count; i++)
	{
		decode_offsets.push_back(index_count);

		// Unroll all elements as-is.
		if (meshlet_runtime)
			index_count += MaxElementsPrim;
		else
			index_count += view.streams[i * view.format_header->stream_count].u.offsets[NumChunks].prim_offset;
	}

	buf_info.domain = BufferDomain::LinkedDeviceHost;
	buf_info.size = decode_offsets.size() * sizeof(uint32_t);
	auto output_offset_strides_buffer = cmd.get_device().create_buffer(buf_info, decode_offsets.data());

	cmd.set_storage_buffer(0, 7, *output_offset_strides_buffer);
	cmd.set_storage_buffer(0, 8, info.indirect ? *info.indirect : *info.streams[0]);

	// TODO: Split dispatches for big chungus meshes.
	// (Starts to become a problem around 8-16 million primitives per dispatch).
	if (view.format_header->meshlet_count > cmd.get_device().get_gpu_properties().limits.maxComputeWorkGroupCount[0])
	{
		LOGW("Exceeding workgroup limit (%u > %u).\n", view.format_header->meshlet_count,
		     cmd.get_device().get_gpu_properties().limits.maxComputeWorkGroupCount[0]);
	}

	cmd.dispatch(view.format_header->meshlet_count, 1, 1);
	cmd.set_specialization_constant_mask(0);
	cmd.enable_subgroup_size_control(false);
	return true;
}
}
}
