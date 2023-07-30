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
#include "filesystem.hpp"
#include "math.hpp"

namespace Granite
{
namespace SceneFormats
{
// MESHLET1 format.
namespace Meshlet
{
static constexpr unsigned MaxU32Streams = 16;
static constexpr unsigned MaxElements = 256;
static constexpr unsigned MaxPrimitives = MaxElements;
static constexpr unsigned MaxVertices = MaxElements;

struct Stream
{
	uint16_t predictor[4 * 2 + 2];
	uint32_t offset_from_base_u32;
	uint16_t bitplane_meta[MaxElements / 32];
};

struct Header
{
	uint32_t base_vertex_offset;
	uint8_t num_primitives_minus_1;
	uint8_t num_attributes_minus_1;
	uint16_t reserved;
};

struct Bound
{
	vec3 center;
	float radius;
	i8vec4 cone_axis_cutoff;
};

enum class StreamType
{
	Primitive = 0, // R8G8B8X8_UINT
	PositionE16, // RGB16_SSCALED * 2^(A16_SINT)
	NormalOct8, // Octahedron encoding in RG8.
	TangentOct8, // Octahedron encoding in RG8, sign bit in B8 (if not zero, +1, otherwise -1).
	UV, // R16G16_SNORM * B16_SSCALED
	BoneIndices, // RGBA8_UINT
	BoneWeights, // RGB8_UNORM (sums to 1, A is implied).
};

enum class MeshStyle : uint32_t
{
	Wireframe = 0, // Primitive + Position
	Untextured, // Wireframe + NormalOct8
	Textured, // Untextured + TangentOct8 + UV
	Skinned // Textured + Bone*
};

struct FormatHeader
{
	MeshStyle style;
	uint32_t u32_stream_count;
	uint32_t meshlet_count;
	uint32_t payload_size_words;
};

struct MeshView
{
	const FormatHeader *format_header;
	const Header *headers;
	const Bound *bounds;
	const Stream *streams;
	const uint32_t *payload;
	uint32_t total_primitives;
	uint32_t total_vertices;
};

static const char magic[8] = { 'M', 'E', 'S', 'H', 'L', 'E', 'T', '1' };

MeshView create_mesh_view(const FileMapping &mapping);
}
}
}
