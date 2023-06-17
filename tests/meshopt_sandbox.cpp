#include "logging.hpp"
#include <stdint.h>
#include <vector>
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include <unordered_map>
#include "bitops.hpp"
#include <assert.h>
#include <algorithm>
using namespace Granite;

static constexpr unsigned MaxStreams = 16;
static constexpr unsigned MaxU32Streams = 16;
static constexpr unsigned MaxElements = 256;
static constexpr unsigned MaxPrimitives = MaxElements;
static constexpr unsigned MaxVertices = MaxElements;

struct MeshletStream
{
	uint32_t offset_from_base_u32;
	uint16_t predictor[4 * 2];
	uint16_t bitplane_meta[MaxElements / 32];
};

struct MeshletMetadata
{
	uint32_t base_vertex_offset;
	uint8_t num_primitives_minus_1;
	uint8_t num_attributes_minus_1;
	uint16_t reserved;
	MeshletStream u32_streams[MaxU32Streams];
};

enum class StreamType : uint8_t
{
	Primitive, // R8G8B8X8_UINT
	PositionF16, // R16G16B16X16_FLOAT
};

struct StreamMeta
{
	StreamType type;
	uint8_t stream_index_component;
};

struct MeshMetadata
{
	uint32_t stream_count;
	uint32_t data_stream_offset_u32;
	uint32_t data_stream_size_u32;

	// Stream meta is used to configure the decode shader.
	StreamMeta stream_meta[MaxStreams];

	std::vector<MeshletMetadata> meshlets;
};

struct PrimitiveAnalysisResult
{
	uint32_t num_primitives;
	uint32_t num_vertices;
};

static PrimitiveAnalysisResult analyze_primitive_count(std::unordered_map<uint32_t, uint32_t> &vertex_remap,
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

		vertex_remap.insert({ index0, uint32_t(vertex_remap.size()) });
		vertex_remap.insert({ index1, uint32_t(vertex_remap.size()) });
		vertex_remap.insert({ index2, uint32_t(vertex_remap.size()) });

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
static uvec4 compute_required_bits(u8vec4 delta)
{
	uvec4 result;
	for (unsigned i = 0; i < 4; i++)
	{
		uint32_t v = delta[i];
		if (v >= 0x80u)
			v ^= 0xffu;
		result[i] = v == 0 ? 0 : (32 - leading_zeroes(v));
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

static void encode_stream(std::vector<uint32_t> &out_payload_buffer,
                          MeshletStream &stream, u8vec4 (&stream_buffer)[MaxElements])
{
	stream.offset_from_base_u32 = uint32_t(out_payload_buffer.size());
	// Simple linear predictor, base equal elements[0], gradient = 0.
	stream.predictor[0] = uint16_t(stream_buffer[0].x) << 8;
	stream.predictor[1] = uint16_t(stream_buffer[0].y) << 8;
	stream.predictor[2] = uint16_t(stream_buffer[0].z) << 8;
	stream.predictor[3] = uint16_t(stream_buffer[0].w) << 8;
	stream.predictor[4] = 0;
	stream.predictor[5] = 0;
	stream.predictor[6] = 0;
	stream.predictor[7] = 0;

	// Delta-encode
	u8vec4 current_value = stream_buffer[0];
	for (unsigned i = 0; i < MaxElements; i++)
	{
		u8vec4 next_value = stream_buffer[i];
		stream_buffer[i] = next_value - current_value;
		current_value = next_value;
	}

	// Encode 32 elements at once.
	for (unsigned chunk_index = 0; chunk_index < MaxElements / 32; chunk_index++)
	{
		uvec4 required_bits = {};
		for (unsigned i = 0; i < 32; i++)
			required_bits = max(required_bits, compute_required_bits(stream_buffer[chunk_index * 32 + i]));

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
}

static void encode_mesh(std::vector<uint32_t> &out_payload_buffer, MeshMetadata &mesh,
                        const uint32_t *index_buffer, uint32_t primitive_count,
                        const uint32_t *attributes,
                        unsigned num_u32_streams)
{
	u8vec4 stream_buffer[MaxElements] = {};
	mesh.stream_count = num_u32_streams + 1;
	mesh.data_stream_offset_u32 = 0; // Can be adjusted in isolation later to pack multiple payload streams into one buffer.
	uint32_t base_vertex_offset = 0;

	std::unordered_map<uint32_t, uint32_t> vbo_remap;

	for (uint32_t primitive_index = 0; primitive_index < primitive_count; )
	{
		uint32_t primitives_to_process = min(primitive_count - primitive_index, MaxPrimitives);
		auto analysis_result = analyze_primitive_count(vbo_remap, index_buffer + 3 * primitive_index, primitives_to_process);
		primitives_to_process = analysis_result.num_primitives;

		MeshletMetadata meshlet = {};

		meshlet.base_vertex_offset = base_vertex_offset;
		meshlet.num_primitives_minus_1 = analysis_result.num_primitives - 1;
		meshlet.num_attributes_minus_1 = analysis_result.num_vertices - 1;
		meshlet.reserved = 0;

		// Encode index buffer.
		for (uint32_t i = 0; i < analysis_result.num_primitives; i++)
		{
			uint8_t i0 = vbo_remap[index_buffer[3 * i + 0]];
			uint8_t i1 = vbo_remap[index_buffer[3 * i + 1]];
			uint8_t i2 = vbo_remap[index_buffer[3 * i + 2]];
			stream_buffer[i] = u8vec4(i0, i1, i2, 0);
		}

		for (uint32_t i = analysis_result.num_primitives; i < MaxElements; i++)
			stream_buffer[i] = stream_buffer[analysis_result.num_primitives - 1];

		encode_stream(out_payload_buffer, meshlet.u32_streams[0], stream_buffer);

		uint64_t vbo_remapping[MaxVertices];
		unsigned vbo_index = 0;
		for (auto &v : vbo_remap)
			vbo_remapping[vbo_index++] = (uint64_t(v.second) << 32) | v.first;
		std::sort(vbo_remapping, vbo_remapping + analysis_result.num_vertices);

		for (uint32_t stream_index = 0; stream_index < num_u32_streams; stream_index++)
		{
			for (uint32_t i = 0; i < analysis_result.num_vertices; i++)
			{
				auto vertex_index = uint32_t(vbo_remapping[i]);
				uint32_t payload = attributes[stream_index + num_u32_streams * vertex_index];
				stream_buffer[i] = u8vec4(uint8_t(payload >> 0), uint8_t(payload >> 8),
				                          uint8_t(payload >> 16), uint8_t(payload >> 24));
			}

			for (uint32_t i = analysis_result.num_vertices; i < MaxElements; i++)
				stream_buffer[i] = stream_buffer[analysis_result.num_vertices - 1];

			encode_stream(out_payload_buffer, meshlet.u32_streams[stream_index + 1], stream_buffer);
		}

		mesh.meshlets.push_back(meshlet);

		primitive_index += primitives_to_process;
		base_vertex_offset += analysis_result.num_vertices;
	}

	mesh.data_stream_size_u32 = uint32_t(out_payload_buffer.size());
}

int main()
{
}