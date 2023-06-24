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
	uint16_t predictor[4 * 2 + 2];
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
		}
		else
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

static void encode_stream(std::vector<uint32_t> &out_payload_buffer,
                          MeshletStream &stream, u8vec4 (&stream_buffer)[MaxElements],
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
			uint8_t i0 = vbo_remap[index_buffer[3 * (primitive_index + i) + 0]];
			uint8_t i1 = vbo_remap[index_buffer[3 * (primitive_index + i) + 1]];
			uint8_t i2 = vbo_remap[index_buffer[3 * (primitive_index + i) + 2]];
			stream_buffer[i] = u8vec4(i0, i1, i2, 0);
		}

		encode_stream(out_payload_buffer, meshlet.u32_streams[0], stream_buffer, analysis_result.num_primitives);

		// Handle spill region just in case.
		uint64_t vbo_remapping[MaxVertices + 3];
		unsigned vbo_index = 0;
		for (auto &v : vbo_remap)
		{
			assert(vbo_index < MaxVertices + 3);
			vbo_remapping[vbo_index++] = (uint64_t(v.second) << 32) | v.first;
		}
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

static void decode_mesh(std::vector<uint32_t> &out_index_buffer, std::vector<uint32_t> &out_u32_stream,
                        const std::vector<uint32_t> &payload, const MeshMetadata &mesh)
{
	assert(mesh.stream_count > 1);
	assert(mesh.stream_meta[0].type == StreamType::Primitive);
	assert(mesh.stream_meta[0].stream_index_component == 0);

	const unsigned u32_stride = mesh.stream_count - 1;
	unsigned index_count = 0;
	unsigned attr_count = 0;

	for (auto &meshlet : mesh.meshlets)
	{
		index_count += (meshlet.num_primitives_minus_1 + 1) * 3;
		attr_count += meshlet.num_attributes_minus_1 + 1;
	}

	out_index_buffer.clear();
	out_u32_stream.clear();
	out_index_buffer.reserve(index_count);
	out_u32_stream.resize(attr_count * (mesh.stream_count - 1));

	for (auto &meshlet : mesh.meshlets)
	{
		for (unsigned stream_index = 0; stream_index < mesh.stream_count; stream_index++)
		{
			auto &stream = meshlet.u32_streams[stream_index];
			const uint32_t *pdata = payload.data() + mesh.data_stream_offset_u32 + stream.offset_from_base_u32;

			u8vec4 deltas[MaxElements] = {};
			const u16vec4 base_predictor = u16vec4(
					stream.predictor[0], stream.predictor[1],
					stream.predictor[2], stream.predictor[3]);
			const u16vec4 linear_predictor = u16vec4(
					stream.predictor[4], stream.predictor[5],
					stream.predictor[6], stream.predictor[7]);
			const u8vec4 initial_value =
					u8vec4(u16vec2(stream.predictor[8], stream.predictor[9]).xxyy() >> u16vec4(0, 8, 0, 8));

			for (unsigned chunk = 0; chunk < (MaxElements / 32); chunk++)
			{
				auto bits_per_u8 = (uvec4(stream.bitplane_meta[chunk]) >> uvec4(0, 4, 8, 12)) & 0xfu;
				uvec4 bitplanes[8] = {};

				for (unsigned comp = 0; comp < 4; comp++)
				{
					for (unsigned bit = 0; bit < bits_per_u8[comp]; bit++)
						bitplanes[bit][comp] = *pdata++;
					// Sign-extend.

					unsigned bit_count = bits_per_u8[comp];
					if (bit_count)
						for (unsigned bit = bit_count; bit < 8; bit++)
							bitplanes[bit][comp] = bitplanes[bit_count - 1][comp];
				}

				for (unsigned i = 0; i < 32; i++)
				{
					for (uint32_t bit = 0; bit < 8; bit++)
						deltas[chunk * 32 + i] |= u8vec4(((bitplanes[bit] >> i) & 1u) << bit);
				}
			}

			// Apply predictors.
			deltas[0] += initial_value;
			for (unsigned i = 0; i < MaxElements; i++)
				deltas[i] += u8vec4((base_predictor + linear_predictor * u16vec4(i)) >> u16vec4(8));

			// Resolve deltas.
			for (unsigned i = 1; i < MaxElements; i++)
				deltas[i] += deltas[i - 1];

			if (stream_index == 0)
			{
				// Index decode.
				unsigned num_primitives = meshlet.num_primitives_minus_1 + 1;
				for (unsigned i = 0; i < num_primitives; i++)
					for (unsigned j = 0; j < 3; j++)
						out_index_buffer.push_back(deltas[i][j] + meshlet.base_vertex_offset);
			}
			else
			{
				// Attributes.
				unsigned num_attributes = meshlet.num_attributes_minus_1 + 1;
				auto *out_attr = out_u32_stream.data() + meshlet.base_vertex_offset * u32_stride + (stream_index - 1);
				for (unsigned i = 0; i < num_attributes; i++, out_attr += u32_stride)
					memcpy(out_attr, deltas[i].data, sizeof(*out_attr));
			}
		}
	}
}

int main()
{
	std::vector<uint32_t> out_payload_buffer;

	uint32_t index_buffer[32 * 3];
	uint32_t u32_stream[32 * 3];
	for (unsigned i = 0; i < 32 * 3; i++)
	{
		index_buffer[i] = i;
		u32_stream[i] = 3 * i;
	}

	MeshMetadata mesh;
	encode_mesh(out_payload_buffer, mesh, index_buffer,
	            sizeof(index_buffer) / (3 * sizeof(index_buffer[0])),
	            u32_stream, 1);

	std::vector<uint32_t> out_index_buffer;
	std::vector<uint32_t> out_u32_stream;
	decode_mesh(out_index_buffer, out_u32_stream, out_payload_buffer, mesh);

	return 0;
}