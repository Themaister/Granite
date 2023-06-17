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

static void find_linear_predictor(uint16_t (&predictor)[8],
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

static void encode_stream(std::vector<uint32_t> &out_payload_buffer,
                          MeshletStream &stream, u8vec4 (&stream_buffer)[MaxElements],
                          unsigned num_elements)
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

	// Find optimal predictor.
	find_linear_predictor(stream.predictor, stream_buffer, num_elements);

	// u8.8 fixed point.
	auto base_predictor = u16vec4(stream.predictor[0], stream.predictor[1], stream.predictor[2], stream.predictor[3]);
	auto linear_predictor = u16vec4(stream.predictor[4], stream.predictor[5], stream.predictor[6], stream.predictor[7]);

	// Delta-encode
	u8vec4 current_value{0};
	for (unsigned i = 0; i < num_elements; i++)
	{
		// Only predict-in bounds elements, since we want all out of bounds elements to be encoded to 0 delta
		// without having them affect the predictor.
		stream_buffer[i] -= u8vec4((base_predictor + linear_predictor * uint16_t(i)) >> uint16_t(8));

		u8vec4 next_value = stream_buffer[i];
		stream_buffer[i] = next_value - current_value;
		current_value = next_value;
	}

	for (unsigned i = num_elements; i < MaxElements; i++)
		stream_buffer[i] = u8vec4(0);

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
	mesh = {};
	mesh.stream_count = num_u32_streams + 1;
	mesh.data_stream_offset_u32 = 0; // Can be adjusted in isolation later to pack multiple payload streams into one buffer.
	mesh.meshlets.reserve((primitive_count + MaxPrimitives - 1) / MaxPrimitives);
	uint32_t base_vertex_offset = 0;

	std::unordered_map<uint32_t, uint32_t> vbo_remap;

	for (uint32_t primitive_index = 0; primitive_index < primitive_count; )
	{
		uint32_t primitives_to_process = min(primitive_count - primitive_index, MaxPrimitives);
		auto analysis_result = analyze_primitive_count(vbo_remap, index_buffer + 3 * primitive_index, primitives_to_process);
		primitives_to_process = analysis_result.num_primitives;

		MeshletMetadata meshlet = {};
		u8vec4 stream_buffer[MaxElements];

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

		encode_stream(out_payload_buffer, meshlet.u32_streams[0], stream_buffer, analysis_result.num_primitives);

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

			encode_stream(out_payload_buffer, meshlet.u32_streams[stream_index + 1], stream_buffer,
			              analysis_result.num_vertices);
		}

		mesh.meshlets.push_back(meshlet);

		primitive_index += primitives_to_process;
		base_vertex_offset += analysis_result.num_vertices;
	}

	mesh.data_stream_size_u32 = uint32_t(out_payload_buffer.size());
}

int main()
{
	std::vector<uint32_t> out_payload_buffer;

	const uint32_t index_buffer[] = {
		0, 2, 4,
		6, 4, 2,
	};

	const uint32_t u32_stream[] = {
		0, 1, 2, 3, 4, 5, 6, 7,
	};

	MeshMetadata mesh;
	encode_mesh(out_payload_buffer, mesh, index_buffer, sizeof(index_buffer) / (3 * sizeof(index_buffer[0])),
				u32_stream, 1);

	return 0;
}