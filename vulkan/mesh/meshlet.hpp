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

#include <stdint.h>

namespace Granite
{
class FileMapping;
}

namespace Vulkan
{
class CommandBuffer;
class Buffer;
}

namespace Vulkan
{
// MESHLET1 format.
namespace Meshlet
{
static constexpr unsigned MaxStreams = 8;
static constexpr unsigned MaxElements = 256;
static constexpr unsigned ElementsPerChunk = 32;
static constexpr unsigned NumChunks = MaxElements / ElementsPerChunk;
static constexpr unsigned MaxPrimitives = MaxElements;
static constexpr unsigned MaxVertices = MaxElements;

struct Stream
{
	uint32_t base_value_or_vertex_offset[12];
	uint32_t bit_plane_config0;
	uint32_t bit_plane_config1;
	uint32_t aux;
	uint32_t offset_in_b128;
};

struct Header
{
	uint32_t base_vertex_offset;
	uint16_t num_primitives;
	uint16_t num_attributes;
};

// For GPU use
struct RuntimeHeader
{
	uint32_t stream_offset;
	uint16_t num_primitives;
	uint16_t num_attributes;
};

struct RuntimeHeaderDecoded
{
	uint32_t primitive_offset;
	uint32_t vertex_offset;
	uint32_t num_primitives;
	uint32_t num_attributes;
};

struct Bound
{
	float center[3];
	float radius;
	float cone_axis_cutoff[4];
};

enum class StreamType
{
	Primitive = 0, // RGB8_UINT (fixed 5-bit encoding, fixed base value of 0)
	Position, // RGB16_SINT * 2^aux
	NormalTangentOct8, // Octahedron encoding in RG8, BA8 for tangent. Following uvec4 encodes 1-bit sign.
	UV, // (0.5 * (R16G16_SINT * 2^aux) + 0.5
	BoneIndices, // RGBA8_UINT
	BoneWeights, // RGBA8_UNORM
};

enum class MeshStyle : uint32_t
{
	Wireframe = 0, // Primitive + Position
	Textured, // Untextured + TangentOct8 + UV
	Skinned // Textured + Bone*
};

struct FormatHeader
{
	MeshStyle style;
	uint32_t stream_count;
	uint32_t meshlet_count;
	uint32_t payload_size_b128;
};

struct PayloadB128
{
	uint32_t words[4];
};

struct MeshView
{
	const FormatHeader *format_header;
	const Header *headers;
	const Bound *bounds;
	const Stream *streams;
	const PayloadB128 *payload;
	uint32_t total_primitives;
	uint32_t total_vertices;
};

static const char magic[8] = { 'M', 'E', 'S', 'H', 'L', 'E', 'T', '2' };

MeshView create_mesh_view(const Granite::FileMapping &mapping);

enum DecodeModeFlagBits : uint32_t
{
	DECODE_MODE_RAW_PAYLOAD = 1 << 0,
};
using DecodeModeFlags = uint32_t;

enum class RuntimeStyle
{
	MDI,
	Meshlet
};

struct DecodeInfo
{
	const Vulkan::Buffer *ibo, *streams[3], *indirect, *payload;
	DecodeModeFlags flags;
	MeshStyle target_style;
	RuntimeStyle runtime_style;

	struct
	{
		uint32_t primitive_offset;
		uint32_t vertex_offset;
		uint32_t meshlet_offset;
	} push;
};

bool decode_mesh(Vulkan::CommandBuffer &cmd, const DecodeInfo &decode_info, const MeshView &view);
}
}
