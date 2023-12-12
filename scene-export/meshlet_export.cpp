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

#include "meshlet_export.hpp"
#include "meshoptimizer.h"
#include "enum_cast.hpp"
#include "math.hpp"
#include "filesystem.hpp"
#include "meshlet.hpp"

namespace Granite
{
namespace Meshlet
{
using namespace Vulkan::Meshlet;

struct Metadata : Header
{
	Bound bound;
	Stream u32_streams[MaxU32Streams];
};

struct CombinedMesh
{
	uint32_t stream_count;
	MeshStyle mesh_style;

	std::vector<Metadata> meshlets;
};

struct Encoded
{
	std::vector<uint32_t> payload;
	CombinedMesh mesh;
};

struct Meshlet
{
	uint32_t offset;
	uint32_t count;
};

struct PrimitiveAnalysisResult
{
	uint32_t num_primitives;
	uint32_t num_vertices;
};

static i16vec4 encode_vec3_to_snorm_exp(vec3 v)
{
	vec3 vabs = abs(v);
	float max_scale = max(max(vabs.x, vabs.y), vabs.z);
	int max_scale_log2 = int(muglm::floor(log2(max_scale)));
	int scale_log2 = 14 - max_scale_log2;

	// Maximum component should have range of [1, 2) since we use floor of log2, so scale with 2^14 instead of 15.
	v.x = ldexpf(v.x, scale_log2);
	v.y = ldexpf(v.y, scale_log2);
	v.z = ldexpf(v.z, scale_log2);
	v = clamp(round(v), vec3(-0x8000), vec3(0x7fff));

	return i16vec4(i16vec3(v), int16_t(-scale_log2));
}

static i16vec3 encode_vec2_to_snorm_exp(vec2 v)
{
	vec2 vabs = abs(v);
	float max_scale = max(vabs.x, vabs.y);
	int max_scale_log2 = int(muglm::floor(log2(max_scale)));
	int scale_log2 = 14 - max_scale_log2;

	// UVs are unorm scaled, don't need more accuracy than this.
	// If all UVs are in range of [0, 1] space, we should get a constant exponent which aids compression.
	scale_log2 = min(scale_log2, 15);

	// Maximum component should have range of [1, 2) since we use floor of log2, so scale with 2^14 instead of 15.
	v.x = ldexpf(v.x, scale_log2);
	v.y = ldexpf(v.y, scale_log2);
	v = clamp(round(v), vec2(-0x8000), vec2(0x7fff));

	return i16vec3(i16vec2(v), int16_t(-scale_log2));
}

static std::vector<i16vec4> mesh_extract_position_snorm_exp(const SceneFormats::Mesh &mesh)
{
	std::vector<i16vec4> encoded_positions;
	std::vector<vec3> positions;

	size_t num_positions = mesh.positions.size() / mesh.position_stride;
	positions.resize(num_positions);
	auto &layout = mesh.attribute_layout[Util::ecast(MeshAttribute::Position)];
	auto fmt = layout.format;

	if (fmt == VK_FORMAT_R32G32B32A32_SFLOAT || fmt == VK_FORMAT_R32G32B32_SFLOAT)
	{
		for (size_t i = 0; i < num_positions; i++)
			memcpy(positions[i].data, mesh.positions.data() + i * mesh.position_stride + layout.offset, sizeof(float) * 3);
	}
	else if (fmt == VK_FORMAT_UNDEFINED)
		return {};
	else
	{
		LOGE("Unexpected format %u.\n", fmt);
		return {};
	}

	encoded_positions.reserve(positions.size());
	for (auto &pos : positions)
		encoded_positions.push_back(encode_vec3_to_snorm_exp(pos));

	return encoded_positions;
}

static std::vector<i8vec4> mesh_extract_normal_tangent_oct8(const SceneFormats::Mesh &mesh, MeshAttribute attr)
{
	std::vector<i8vec4> encoded_attributes;
	std::vector<vec4> normals;

	auto &layout = mesh.attribute_layout[Util::ecast(attr)];
	auto fmt = layout.format;

	size_t num_attrs = mesh.attributes.size() / mesh.attribute_stride;
	normals.resize(num_attrs);

	if (fmt == VK_FORMAT_R32G32B32_SFLOAT)
	{
		for (size_t i = 0; i < num_attrs; i++)
		{
			memcpy(normals[i].data,
			       mesh.attributes.data() + i * mesh.attribute_stride + layout.offset,
			       sizeof(float) * 3);
			normals[i].w = 0.0f;
		}
	}
	else if (fmt == VK_FORMAT_R32G32B32A32_SFLOAT)
	{
		for (size_t i = 0; i < num_attrs; i++)
		{
			memcpy(normals[i].data,
			       mesh.attributes.data() + i * mesh.attribute_stride + layout.offset,
			       sizeof(float) * 4);
		}
	}
	else if (fmt == VK_FORMAT_UNDEFINED)
		return {};
	else
	{
		LOGE("Unexpected format %u.\n", fmt);
		return {};
	}

	encoded_attributes.resize(normals.size());
	meshopt_encodeFilterOct(encoded_attributes.data(), encoded_attributes.size(),
	                        sizeof(i8vec4), 8, normals[0].data);
	for (auto &n : encoded_attributes)
		n.w = n.w <= 0 ? -1 : 0;

	return encoded_attributes;
}

static i16vec4 encode_uv_to_snorm_scale(vec2 uv)
{
	// UVs tend to be in [0, 1] range. Readjust to use more of the available range.
	uv = 2.0f * uv - 1.0f;
	return i16vec4(encode_vec2_to_snorm_exp(uv), 0);
}

static std::vector<i16vec4> mesh_extract_uv_snorm_scale(const SceneFormats::Mesh &mesh)
{
	std::vector<i16vec4> encoded_uvs;
	std::vector<vec2> uvs;

	size_t num_uvs = mesh.attributes.size() / mesh.attribute_stride;
	uvs.resize(num_uvs);
	auto &layout = mesh.attribute_layout[Util::ecast(MeshAttribute::UV)];
	auto fmt = layout.format;

	if (fmt == VK_FORMAT_R32G32_SFLOAT)
	{
		for (size_t i = 0; i < num_uvs; i++)
			memcpy(uvs[i].data, mesh.attributes.data() + i * mesh.attribute_stride + layout.offset, sizeof(float) * 2);
	}
	else if (fmt == VK_FORMAT_R16G16_UNORM)
	{
		for (size_t i = 0; i < num_uvs; i++)
		{
			u16vec2 u16;
			memcpy(u16.data, mesh.attributes.data() + i * mesh.attribute_stride + layout.offset, sizeof(uint16_t) * 2);
			uvs[i] = vec2(u16) * float(1.0f / 0xffff);
		}
	}
	else if (fmt == VK_FORMAT_UNDEFINED)
		return {};
	else
	{
		LOGE("Unexpected format %u.\n", fmt);
		return {};
	}

	encoded_uvs.reserve(uvs.size());
	for (auto &uv : uvs)
		encoded_uvs.push_back(encode_uv_to_snorm_scale(uv));

	return encoded_uvs;
}

static vec3 decode_snorm_exp(i16vec4 p)
{
	vec3 result;
	result.x = ldexpf(float(p.x), p.w);
	result.y = ldexpf(float(p.y), p.w);
	result.z = ldexpf(float(p.z), p.w);
	return result;
}

static PrimitiveAnalysisResult analyze_primitive_count(std::unordered_map <uint32_t, uint32_t> &vertex_remap,
                                                       const uint32_t *index_buffer, uint32_t max_num_primitives)
{
	PrimitiveAnalysisResult result = {};
	uint32_t vertex_count = 0;

	// We can reference a maximum of 256 vertices.
	vertex_remap.clear();

	for (uint32_t i = 0; i < max_num_primitives; i++)
	{
		uint32_t index0 = index_buffer[3 * i + 0];
		uint32_t index1 = index_buffer[3 * i + 1];
		uint32_t index2 = index_buffer[3 * i + 2];

		vertex_count = uint32_t(vertex_remap.size());

		vertex_remap.insert({index0, uint32_t(vertex_remap.size())});
		vertex_remap.insert({index1, uint32_t(vertex_remap.size())});
		vertex_remap.insert({index2, uint32_t(vertex_remap.size())});

		// If this primitive causes us to go out of bounds, reset.
		if (vertex_remap.size() > MaxVertices)
		{
			max_num_primitives = i;
			break;
		}

		vertex_count = uint32_t(vertex_remap.size());
	}

	result.num_primitives = max_num_primitives;
	result.num_vertices = vertex_count;
	return result;
}

// Analyze bits required to encode a signed delta.
static uvec4 compute_required_bits_unsigned(u8vec4 delta)
{
	uvec4 result;
	for (unsigned i = 0; i < 4; i++)
	{
		uint32_t v = delta[i];
		result[i] = v == 0 ? 0 : (32 - leading_zeroes(v));
	}
	return result;
}

static uvec4 compute_required_bits_signed(u8vec4 delta)
{
	uvec4 result;
	for (unsigned i = 0; i < 4; i++)
	{
		uint32_t v = delta[i];

		if (v == 0)
		{
			result[i] = 0;
		} else
		{
			if (v >= 0x80u)
				v ^= 0xffu;
			result[i] = v == 0 ? 1 : (33 - leading_zeroes(v));
		}
	}
	return result;
}

static uint32_t extract_bit_plane(const uint8_t *bytes, unsigned bit_index)
{
	uint32_t u32 = 0;
	for (unsigned i = 0; i < 32; i++)
		u32 |= ((bytes[4 * i] >> bit_index) & 1u) << i;
	return u32;
}

static void find_linear_predictor(uint16_t *predictor,
                                  const u8vec4 (&stream_buffer)[MaxElements],
                                  unsigned num_elements)
{
	// Sign-extend since the deltas are considered to be signed ints.
	ivec4 unrolled_data[MaxElements];
	for (unsigned i = 0; i < num_elements; i++)
		unrolled_data[i] = ivec4(i8vec4(stream_buffer[i]));

	// Simple linear regression.
	// Pilfered from: https://www.codesansar.com/numerical-methods/linear-regression-method-using-c-programming.htm
	ivec4 x{0}, x2{0}, y{0}, xy{0};
	for (unsigned i = 0; i < num_elements; i++)
	{
		x += int(i);
		x2 += int(i * i);
		y += unrolled_data[i];
		xy += int(i) * unrolled_data[i];
	}

	int n = int(num_elements);
	ivec4 b_denom = (n * x2 - x * x);
	b_denom = select(b_denom, ivec4(1), equal(ivec4(0), b_denom));

	// Encode in u8.8 fixed point.
	ivec4 b = (ivec4(256) * (n * xy - x * y)) / b_denom;
	ivec4 a = ((ivec4(256) * y - b * x)) / n;

	for (unsigned i = 0; i < 4; i++)
		predictor[i] = uint16_t(a[i]);
	for (unsigned i = 0; i < 4; i++)
		predictor[4 + i] = uint16_t(b[i]);
}

static size_t encode_stream(std::vector <uint32_t> &out_payload_buffer,
                            Stream &stream, u8vec4 (&stream_buffer)[MaxElements],
                            unsigned num_elements)
{
	stream.offset_from_base_u32 = uint32_t(out_payload_buffer.size());

	// Delta-encode
	u8vec4 current_value;
	if (num_elements > 1)
		current_value = u8vec4(2) * stream_buffer[0] - stream_buffer[1];
	else
		current_value = stream_buffer[0];
	u8vec4 bias_value = current_value;

	for (unsigned i = 0; i < num_elements; i++)
	{
		u8vec4 next_value = stream_buffer[i];
		stream_buffer[i] = next_value - current_value;
		current_value = next_value;
	}

	// Find optimal linear predictor.
	find_linear_predictor(stream.predictor, stream_buffer, num_elements);

	// u8.8 fixed point.
	auto base_predictor = u16vec4(stream.predictor[0], stream.predictor[1], stream.predictor[2], stream.predictor[3]);
	auto linear_predictor = u16vec4(stream.predictor[4], stream.predictor[5], stream.predictor[6], stream.predictor[7]);

	for (unsigned i = 0; i < num_elements; i++)
	{
		// Only predict in-bounds elements, since we want all out of bounds elements to be encoded to 0 delta
		// without having them affect the predictor.
		stream_buffer[i] -= u8vec4((base_predictor + linear_predictor * uint16_t(i)) >> uint16_t(8));
	}

	for (unsigned i = num_elements; i < MaxElements; i++)
		stream_buffer[i] = u8vec4(0);

	// Try to adjust the range such that it can fit in fewer bits.
	// We can use the constant term in the linear predictor to nudge values in place.
	i8vec4 lo(127);
	i8vec4 hi(-128);

	for (unsigned i = 0; i < num_elements; i++)
	{
		lo = min(lo, i8vec4(stream_buffer[i]));
		hi = max(hi, i8vec4(stream_buffer[i]));
	}

	uvec4 full_bits = compute_required_bits_unsigned(u8vec4(hi - lo));
	u8vec4 target_lo_value = u8vec4(-((uvec4(1) << full_bits) >> 1u));
	u8vec4 bias = target_lo_value - u8vec4(lo);

	for (unsigned i = 0; i < num_elements; i++)
		stream_buffer[i] += bias;

	for (unsigned i = 0; i < 4; i++)
		stream.predictor[i] -= uint16_t(bias[i]) << 8;

	// Based on the linear predictor, it's possible that the encoded value in stream_buffer[0] becomes non-zero again.
	// This is undesirable, since we can use the initial value to force a delta of 0 here, saving precious bits.
	bias_value += stream_buffer[0];
	stream_buffer[0] = u8vec4(0);

	// Simple linear predictor, base equal elements[0], gradient = 0.
	stream.predictor[8] = uint16_t((bias_value.y << 8) | bias_value.x);
	stream.predictor[9] = uint16_t((bias_value.w << 8) | bias_value.z);

	// Encode 32 elements at once.
	for (unsigned chunk_index = 0; chunk_index < MaxElements / 32; chunk_index++)
	{
		uvec4 required_bits = {};
		for (unsigned i = 0; i < 32; i++)
			required_bits = max(required_bits, compute_required_bits_signed(stream_buffer[chunk_index * 32 + i]));

		// Encode bit counts.
		stream.bitplane_meta[chunk_index] = uint16_t((required_bits.x << 0) | (required_bits.y << 4) |
		                                             (required_bits.z << 8) | (required_bits.w << 12));

		for (unsigned i = 0; i < required_bits.x; i++)
			out_payload_buffer.push_back(extract_bit_plane(&stream_buffer[chunk_index * 32][0], i));
		for (unsigned i = 0; i < required_bits.y; i++)
			out_payload_buffer.push_back(extract_bit_plane(&stream_buffer[chunk_index * 32][1], i));
		for (unsigned i = 0; i < required_bits.z; i++)
			out_payload_buffer.push_back(extract_bit_plane(&stream_buffer[chunk_index * 32][2], i));
		for (unsigned i = 0; i < required_bits.w; i++)
			out_payload_buffer.push_back(extract_bit_plane(&stream_buffer[chunk_index * 32][3], i));
	}

	return out_payload_buffer.size() - stream.offset_from_base_u32;
}

static void encode_mesh(Encoded &encoded,
                        const Meshlet *meshlets, size_t num_meshlets,
                        const uint32_t *index_buffer, uint32_t primitive_count,
                        const uint32_t *attributes,
                        unsigned num_u32_streams)
{
	encoded = {};
	auto &mesh = encoded.mesh;
	mesh.stream_count = num_u32_streams + 1;
	mesh.meshlets.reserve(num_meshlets);
	uint32_t base_vertex_offset = 0;

	std::unordered_map <uint32_t, uint32_t> vbo_remap;
	uint32_t primitive_index = 0;
	size_t words_per_stream[MaxU32Streams] = {};

	for (uint32_t meshlet_index = 0; meshlet_index < num_meshlets; meshlet_index++)
	{
		uint32_t primitives_to_process = min(primitive_count - primitive_index, meshlets[meshlet_index].count);
		assert(primitives_to_process);
		assert(primitive_count > primitive_index);

		primitive_index = meshlets[meshlet_index].offset;

		auto analysis_result = analyze_primitive_count(
				vbo_remap, index_buffer + 3 * primitive_index,
				primitives_to_process);

		assert(analysis_result.num_primitives);
		assert(analysis_result.num_vertices);

		primitives_to_process = analysis_result.num_primitives;

		Metadata meshlet = {};
		u8vec4 stream_buffer[MaxElements];

		meshlet.base_vertex_offset = base_vertex_offset;
		meshlet.num_primitives_minus_1 = analysis_result.num_primitives - 1;
		meshlet.num_attributes_minus_1 = analysis_result.num_vertices - 1;
		meshlet.reserved = 0;

		// Encode index buffer.
		for (uint32_t i = 0; i < analysis_result.num_primitives; i++)
		{
			uint8_t i0 = vbo_remap[index_buffer[3 * (primitive_index + i) + 0]];
			uint8_t i1 = vbo_remap[index_buffer[3 * (primitive_index + i) + 1]];
			uint8_t i2 = vbo_remap[index_buffer[3 * (primitive_index + i) + 2]];
			stream_buffer[i] = u8vec4(i0, i1, i2, 0);
		}

		words_per_stream[0] +=
				encode_stream(encoded.payload, meshlet.u32_streams[0], stream_buffer, analysis_result.num_primitives);

		// Handle spill region just in case.
		uint64_t vbo_remapping[MaxVertices + 3];
		unsigned vbo_index = 0;
		for (auto &v : vbo_remap)
		{
			assert(vbo_index < MaxVertices + 3);
			vbo_remapping[vbo_index++] = (uint64_t(v.second) << 32) | v.first;
		}
		std::sort(vbo_remapping, vbo_remapping + vbo_index);

		for (uint32_t stream_index = 0; stream_index < num_u32_streams; stream_index++)
		{
			for (uint32_t i = 0; i < analysis_result.num_vertices; i++)
			{
				auto vertex_index = uint32_t(vbo_remapping[i]);
				uint32_t payload = attributes[stream_index + num_u32_streams * vertex_index];
				memcpy(stream_buffer[i].data, &payload, sizeof(payload));
			}

			words_per_stream[stream_index + 1] +=
					encode_stream(encoded.payload, meshlet.u32_streams[stream_index + 1], stream_buffer,
					              analysis_result.num_vertices);
		}

		mesh.meshlets.push_back(meshlet);
		base_vertex_offset += analysis_result.num_vertices;
		primitive_index += primitives_to_process;
	}

	for (unsigned i = 0; i < MaxU32Streams; i++)
		if (words_per_stream[i])
			LOGI("Stream[%u] = %zu bytes.\n", i, words_per_stream[i] * sizeof(uint32_t));
}

static bool export_encoded_mesh(const std::string &path, const Encoded &encoded)
{
	size_t required_size = 0;

	FormatHeader header = {};

	header.style = encoded.mesh.mesh_style;
	header.u32_stream_count = encoded.mesh.stream_count;
	header.meshlet_count = uint32_t(encoded.mesh.meshlets.size());
	header.payload_size_words = uint32_t(encoded.payload.size());

	required_size += sizeof(magic);
	required_size += sizeof(FormatHeader);

	// Per-meshlet metadata.
	required_size += encoded.mesh.meshlets.size() * sizeof(Header);

	// Bounds.
	required_size += encoded.mesh.meshlets.size() * sizeof(Bound);

	// Stream metadata.
	required_size += encoded.mesh.stream_count * encoded.mesh.meshlets.size() * sizeof(Stream);

	// Payload.
	// Need a padding word to speed up decoder.
	required_size += (encoded.payload.size() + 1) * sizeof(uint32_t);

	auto file = GRANITE_FILESYSTEM()->open(path, FileMode::WriteOnly);
	if (!file)
		return false;

	auto mapping = file->map_write(required_size);
	if (!mapping)
		return false;

	auto *ptr = mapping->mutable_data<unsigned char>();

	memcpy(ptr, magic, sizeof(magic));
	ptr += sizeof(magic);
	memcpy(ptr, &header, sizeof(header));
	ptr += sizeof(header);

	for (uint32_t i = 0; i < header.meshlet_count; i++)
	{
		auto &gpu = static_cast<const Header &>(encoded.mesh.meshlets[i]);
		memcpy(ptr, &gpu, sizeof(gpu));
		ptr += sizeof(gpu);
	}

	for (uint32_t i = 0; i < header.meshlet_count; i++)
	{
		auto &bound = encoded.mesh.meshlets[i].bound;
		memcpy(ptr, &bound, sizeof(bound));
		ptr += sizeof(bound);
	}

	for (uint32_t i = 0; i < header.meshlet_count; i++)
	{
		for (uint32_t j = 0; j < header.u32_stream_count; j++)
		{
			memcpy(ptr, &encoded.mesh.meshlets[i].u32_streams[j], sizeof(Stream));
			ptr += sizeof(Stream);
		}
	}

	memcpy(ptr, encoded.payload.data(), encoded.payload.size() * sizeof(uint32_t));
	ptr += encoded.payload.size() * sizeof(uint32_t);
	memset(ptr, 0, sizeof(uint32_t));
	return true;
}

bool export_mesh_to_meshlet(const std::string &path, SceneFormats::Mesh mesh, MeshStyle style)
{
	mesh_deduplicate_vertices(mesh);
	if (!mesh_optimize_index_buffer(mesh, {}))
		return false;

	std::vector<i16vec4> positions, uv;
	std::vector<i8vec4> normals, tangent;

	unsigned num_u32_streams = 0;

	switch (style)
	{
	case MeshStyle::Skinned:
		LOGE("Unimplemented.\n");
		return false;
	case MeshStyle::Textured:
		uv = mesh_extract_uv_snorm_scale(mesh);
		num_u32_streams += 4;
		if (uv.empty())
		{
			LOGE("No UVs.\n");
			return false;
		}
		normals = mesh_extract_normal_tangent_oct8(mesh, MeshAttribute::Normal);
		tangent = mesh_extract_normal_tangent_oct8(mesh, MeshAttribute::Tangent);
		if (normals.empty() || tangent.empty())
		{
			LOGE("No tangent or normal.\n");
			return false;
		}
		// Fallthrough
	case MeshStyle::Wireframe:
		positions = mesh_extract_position_snorm_exp(mesh);
		if (positions.empty())
		{
			LOGE("No positions.\n");
			return false;
		}
		num_u32_streams += 2;
		break;

	default:
		LOGE("Unknown mesh style.\n");
		return false;
	}

	std::vector<uint32_t> attributes(num_u32_streams * positions.size());
	uint32_t *ptr = attributes.data();
	for (size_t i = 0, n = positions.size(); i < n; i++)
	{
		memcpy(ptr, positions[i].data, sizeof(positions.front()));
		ptr += sizeof(positions.front()) / sizeof(uint32_t);

		if (!normals.empty())
		{
			memcpy(ptr, normals[i].data, sizeof(normals.front()));
			ptr += sizeof(normals.front()) / sizeof(uint32_t);
		}

		if (!tangent.empty())
		{
			memcpy(ptr, tangent[i].data, sizeof(tangent.front()));
			ptr += sizeof(tangent.front()) / sizeof(uint32_t);
		}

		if (!uv.empty())
		{
			memcpy(ptr, uv[i].data, sizeof(uv.front()));
			ptr += sizeof(uv.front()) / sizeof(uint32_t);
		}
	}

	// Use quantized position to guide the clustering.
	std::vector<vec3> position_buffer;
	position_buffer.reserve(positions.size());
	for (auto &p : positions)
		position_buffer.push_back(decode_snorm_exp(p));

	// Special meshoptimizer limit.
	constexpr unsigned max_vertices = 255;
	constexpr unsigned max_primitives = 256;
	size_t num_meshlets = meshopt_buildMeshletsBound(mesh.count, max_vertices, max_primitives);

	std::vector<unsigned> out_vertex_redirection_buffer(num_meshlets * max_vertices);
	std::vector<unsigned char> local_index_buffer(num_meshlets * max_primitives * 3);
	std::vector<meshopt_Meshlet> meshlets(num_meshlets);

	num_meshlets = meshopt_buildMeshlets(meshlets.data(),
	                                     out_vertex_redirection_buffer.data(), local_index_buffer.data(),
	                                     reinterpret_cast<const uint32_t *>(mesh.indices.data()), mesh.count,
	                                     position_buffer[0].data, positions.size(), sizeof(vec3),
	                                     max_vertices, max_primitives, 0.5f);

	meshlets.resize(num_meshlets);

	std::vector<Meshlet> out_meshlets;
	std::vector<uvec3> out_index_buffer;

	out_meshlets.reserve(num_meshlets);
	for (auto &meshlet : meshlets)
	{
		Meshlet m = {};
		m.offset = uint32_t(out_index_buffer.size());
		m.count = meshlet.triangle_count;
		out_meshlets.push_back(m);

		auto *local_indices = local_index_buffer.data() + meshlet.triangle_offset;
		for (unsigned i = 0; i < meshlet.triangle_count; i++)
		{
			out_index_buffer.emplace_back(
					out_vertex_redirection_buffer[local_indices[3 * i + 0] + meshlet.vertex_offset],
					out_vertex_redirection_buffer[local_indices[3 * i + 1] + meshlet.vertex_offset],
					out_vertex_redirection_buffer[local_indices[3 * i + 2] + meshlet.vertex_offset]);
		}
	}

	std::vector<meshopt_Bounds> bounds;
	bounds.clear();
	bounds.reserve(num_meshlets);
	for (auto &meshlet : out_meshlets)
	{
		auto bound = meshopt_computeClusterBounds(
				out_index_buffer[meshlet.offset].data, meshlet.count * 3,
				position_buffer[0].data, positions.size(), sizeof(vec3));
		bounds.push_back(bound);
	}

	Encoded encoded;
	encode_mesh(encoded, out_meshlets.data(), out_meshlets.size(),
	            out_index_buffer[0].data, out_index_buffer.size(),
	            attributes.data(), num_u32_streams);
	encoded.mesh.mesh_style = style;

	assert(bounds.size() == encoded.mesh.meshlets.size());
	const auto *pbounds = bounds.data();
	for (auto &meshlet : encoded.mesh.meshlets)
	{
		memcpy(meshlet.bound.center, pbounds->center, sizeof(float) * 3);
		meshlet.bound.radius = pbounds->radius;
		memcpy(meshlet.bound.cone_axis_cutoff, pbounds->cone_axis, sizeof(pbounds->cone_axis));
		meshlet.bound.cone_axis_cutoff[3] = pbounds->cone_cutoff;
		pbounds++;
	}

	LOGI("Exported meshlet:\n");
	LOGI("  %zu meshlets\n", encoded.mesh.meshlets.size());
	LOGI("  %zu payload bytes\n", encoded.payload.size() * sizeof(uint32_t));
	LOGI("  %u total indices\n", mesh.count);
	LOGI("  %zu total attributes\n", mesh.positions.size() / mesh.position_stride);

	size_t uncompressed_bytes = mesh.indices.size();
	uncompressed_bytes += mesh.positions.size();
	if (style != MeshStyle::Wireframe)
		uncompressed_bytes += mesh.attributes.size();

	LOGI("  %zu uncompressed bytes\n\n\n", uncompressed_bytes);

	return export_encoded_mesh(path, encoded);
}
}
}
