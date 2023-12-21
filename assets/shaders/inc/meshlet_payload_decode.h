#ifndef MESHLET_PAYLOAD_DECODE_H_
#define MESHLET_PAYLOAD_DECODE_H_

#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_control_flow_attributes : require

#include "meshlet_payload_constants.h"

#ifndef MESHLET_PAYLOAD_DESCRIPTOR_SET
#error "Must define MESHLET_PAYLOAD_DESCRIPTOR_SET"
#endif

#ifndef MESHLET_PAYLOAD_META_BINDING
#error "Must define MESHLET_PAYLOAD_META_BINDING"
#endif

#ifndef MESHLET_PAYLOAD_STREAM_BINDING
#error "Must define MESHLET_PAYLOAD_STREAM_BINDING"
#endif

#ifndef MESHLET_PAYLOAD_PAYLOAD_BINDING
#error "Must define MESHLET_PAYLOAD_PAYLOAD_BINDING"
#endif

struct MeshletStream
{
	uint base_value_or_offsets[12];
	uint bit_plane_config;
	uint reserved;
	int aux;
	uint offset_in_b128;
};

struct MeshletMetaRaw
{
	uint base_vertex_offset;
	uint num_chunks;
};

struct MeshletMetaRuntime
{
	uint stream_offset;
	uint num_chunks;
};

struct MeshletChunkInfo
{
	uint primitive_count;
	uint primitive_offset;
	uint vertex_count;
	uint vertex_offset;
};

struct MeshletInfo
{
	uint primitive_count;
	uint vertex_count;
};

layout(set = MESHLET_PAYLOAD_DESCRIPTOR_SET, binding = MESHLET_PAYLOAD_META_BINDING, std430) readonly buffer MeshletMetasRaw
{
	MeshletMetaRaw data[];
} meshlet_metas_raw;

layout(set = MESHLET_PAYLOAD_DESCRIPTOR_SET, binding = MESHLET_PAYLOAD_META_BINDING, std430) readonly buffer MeshletMetasRuntime
{
	MeshletMetaRuntime data[];
} meshlet_metas_runtime;

layout(set = MESHLET_PAYLOAD_DESCRIPTOR_SET, binding = MESHLET_PAYLOAD_STREAM_BINDING, std430) readonly buffer MeshletStreams
{
	MeshletStream data[];
} meshlet_streams;

layout(set = MESHLET_PAYLOAD_DESCRIPTOR_SET, binding = MESHLET_PAYLOAD_PAYLOAD_BINDING, std430) readonly buffer Payload
{
	uvec4 data[];
} payload;

layout(set = MESHLET_PAYLOAD_DESCRIPTOR_SET, binding = MESHLET_PAYLOAD_PAYLOAD_BINDING, std430) readonly buffer PayloadU32
{
	uint data[];
} payload_u32;

layout(set = MESHLET_PAYLOAD_DESCRIPTOR_SET, binding = MESHLET_PAYLOAD_PAYLOAD_BINDING, std430) readonly buffer PayloadU16
{
	uint16_t data[];
} payload_u16;

MeshletInfo meshlet_get_meshlet_info(uint stream_index)
{
	MeshletInfo info;
	uint v = meshlet_streams.data[stream_index].base_value_or_offsets[MESHLET_PAYLOAD_NUM_CHUNKS];
	uint prim_offset = bitfieldExtract(v, 0, 16);
	uint vert_offset = bitfieldExtract(v, 16, 16);
	info.primitive_count = prim_offset;
	info.vertex_count = vert_offset;
	return info;
}

MeshletChunkInfo meshlet_get_chunk_info(uint stream_index, uint chunk_index)
{
	MeshletChunkInfo info;

	uint v0 = meshlet_streams.data[stream_index].base_value_or_offsets[chunk_index];
	uint v1 = meshlet_streams.data[stream_index].base_value_or_offsets[chunk_index + 1];

	uint prim_offset0 = bitfieldExtract(v0, 0, 16);
	uint vert_offset0 = bitfieldExtract(v0, 16, 16);
	uint prim_offset1 = bitfieldExtract(v1, 0, 16);
	uint vert_offset1 = bitfieldExtract(v1, 16, 16);

	info.primitive_count = prim_offset1 - prim_offset0;
	info.primitive_offset = prim_offset0;
	info.vertex_count = vert_offset1 - vert_offset0;
	info.vertex_offset = vert_offset0;

	return info;
}

uint meshlet_decode_index_buffer(uint stream_index, uint chunk_index, int lane_index)
{
	uint offset_in_b128 = meshlet_streams.data[stream_index].offset_in_b128;

	// Fixed 5-bit encoding.
	offset_in_b128 += 4 * chunk_index;

	// Scalar load. 64 bytes in one go.
	uvec4 p0 = payload.data[offset_in_b128 + 0];
	uvec4 p1 = payload.data[offset_in_b128 + 1];
	uvec4 p2 = payload.data[offset_in_b128 + 2];
	uvec4 p3 = payload.data[offset_in_b128 + 3];

	uint indices = 0;

	indices |= bitfieldExtract(p0.x, lane_index, 1) << 0u;
	indices |= bitfieldExtract(p0.y, lane_index, 1) << 1u;
	indices |= bitfieldExtract(p0.z, lane_index, 1) << 2u;
	indices |= bitfieldExtract(p0.w, lane_index, 1) << 3u;

	indices |= bitfieldExtract(p1.x, lane_index, 1) << 8u;
	indices |= bitfieldExtract(p1.y, lane_index, 1) << 9u;
	indices |= bitfieldExtract(p1.z, lane_index, 1) << 10u;
	indices |= bitfieldExtract(p1.w, lane_index, 1) << 11u;

	indices |= bitfieldExtract(p2.x, lane_index, 1) << 16u;
	indices |= bitfieldExtract(p2.y, lane_index, 1) << 17u;
	indices |= bitfieldExtract(p2.z, lane_index, 1) << 18u;
	indices |= bitfieldExtract(p2.w, lane_index, 1) << 19u;

	indices |= bitfieldExtract(p3.x, lane_index, 1) << 4u;
	indices |= bitfieldExtract(p3.y, lane_index, 1) << 12u;
	indices |= bitfieldExtract(p3.z, lane_index, 1) << 20u;

	return indices;
}

i16vec3 meshlet_decode_snorm_scaled_i16x3(uint stream_index, uint chunk_index, int lane_index, out int exponent)
{
	uint offset_in_b128 = meshlet_streams.data[stream_index].offset_in_b128;
	uint bit_plane_config = meshlet_streams.data[stream_index].bit_plane_config;
	exponent = meshlet_streams.data[stream_index].aux;

	// Scalar math.
	if (chunk_index != 0)
	{
		uint prev_bit_mask = bitfieldExtract(bit_plane_config, 0, int((chunk_index - 1) * 4));
		offset_in_b128 += bitCount(prev_bit_mask & 0x88888888) * 12;
		offset_in_b128 += bitCount(prev_bit_mask & 0x44444444) * 6;
		offset_in_b128 += bitCount(prev_bit_mask & 0x22222222) * 3;
		offset_in_b128 += bitCount(prev_bit_mask & 0x11111111) * 2;
	}

	// Scalar math.
	uint encoded_bits = bitfieldExtract(bit_plane_config, int(chunk_index * 4), 4);
	uint base_value_xy = meshlet_streams.data[stream_index].base_value_or_offsets[chunk_index];
	uint base_value_z = meshlet_streams.data[stream_index].base_value_or_offsets[8 + chunk_index / 2];
	uint base_value_x = bitfieldExtract(base_value_xy, 0, 16);
	uint base_value_y = bitfieldExtract(base_value_xy, 16, 16);
	base_value_z = bitfieldExtract(base_value_z, int(16 * (chunk_index & 1)), 16);
	uvec3 base_value = uvec3(base_value_x, base_value_y, base_value_z);

	uvec3 value = uvec3(0);

	if (encoded_bits == 8)
	{
		// Vector loads.
		uint value_xy = payload_u32.data[offset_in_b128 * 4 + lane_index];
		uint value_z = uint(payload_u16.data[offset_in_b128 * 8 + 64 + lane_index]);

		value.x = bitfieldExtract(value_xy, 0, 16);
		value.y = bitfieldExtract(value_xy, 16, 16);
		value.z = value_z;
	}
	else if (encoded_bits != 0)
	{
		uvec4 p0, p1, p2, p3, p4, p5;

		// Scalar loads, vector math.
		// Preload early. Also helps compiler prove it can use common descriptor (RADV thing).
		p0 = payload.data[offset_in_b128];
		offset_in_b128 += 1;

#define UNROLL_BITS_4(out_value, bit_offset, p) \
	out_value |= bitfieldExtract(p.x, lane_index, 1) << ((bit_offset) + 0); \
	out_value |= bitfieldExtract(p.y, lane_index, 1) << ((bit_offset) + 1); \
	out_value |= bitfieldExtract(p.z, lane_index, 1) << ((bit_offset) + 2); \
	out_value |= bitfieldExtract(p.w, lane_index, 1) << ((bit_offset) + 3)
#define UNROLL_BITS_8(out_value, bit_offset, p0, p1) \
	UNROLL_BITS_4(out_value, bit_offset, p0); \
	UNROLL_BITS_4(out_value, (bit_offset) + 4, p1)

		int bit_offset = 0;
		if ((encoded_bits & 4) != 0)
		{
			p1 = payload.data[offset_in_b128 + 0];
			p2 = payload.data[offset_in_b128 + 1];
			p3 = payload.data[offset_in_b128 + 2];
			p4 = payload.data[offset_in_b128 + 3];
			p5 = payload.data[offset_in_b128 + 4];

			UNROLL_BITS_8(value.x, 0, p0, p1);
			UNROLL_BITS_8(value.y, 0, p2, p3);
			UNROLL_BITS_8(value.z, 0, p4, p5);

			// Preload for next iteration.
			p0 = payload.data[offset_in_b128 + 5];

			offset_in_b128 += 6;
			bit_offset += 8;
		}

		if ((encoded_bits & 2) != 0)
		{
			p1 = payload.data[offset_in_b128 + 0];
			p2 = payload.data[offset_in_b128 + 1];

			UNROLL_BITS_4(value.x, bit_offset, p0);
			UNROLL_BITS_4(value.y, bit_offset, p1);
			UNROLL_BITS_4(value.z, bit_offset, p2);

			// Preload for next iteration.
			p0 = payload.data[offset_in_b128 + 2];
			offset_in_b128 += 3;
			bit_offset += 4;
		}

		if ((encoded_bits & 1) != 0)
		{
			p1 = payload.data[offset_in_b128];
			value.x |= bitfieldExtract(p0.x, lane_index, 1) << (bit_offset + 0);
			value.x |= bitfieldExtract(p0.y, lane_index, 1) << (bit_offset + 1);
			value.y |= bitfieldExtract(p0.z, lane_index, 1) << (bit_offset + 0);
			value.y |= bitfieldExtract(p0.w, lane_index, 1) << (bit_offset + 1);
			value.z |= bitfieldExtract(p1.x, lane_index, 1) << (bit_offset + 0);
			value.z |= bitfieldExtract(p1.y, lane_index, 1) << (bit_offset + 1);
		}
	}

	value += base_value;
	return i16vec3(value);
}

#endif
