#ifndef MESHLET_PAYLOAD_DECODE_H_
#define MESHLET_PAYLOAD_DECODE_H_

#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_scalar_block_layout : require

#include "meshlet_payload_constants.h"

#ifndef MESHLET_PAYLOAD_NUM_U32_STREAMS
#error "Must define MESHLET_PAYLOAD_NUM_U32_STREAMS before including meshlet_payload_decode.h"
#endif

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
	u16vec4 predictor_a;
	u16vec4 predictor_b;
	u8vec4 initial_value;
	uint offset_from_base;
	uint16_t bitplane_meta[MESHLET_PAYLOAD_NUM_CHUNKS];
};

struct MeshletMeta
{
	uint base_vertex_offset;
	uint8_t num_primitives_minus_1;
	uint8_t num_attributes_minus_1;
	uint16_t reserved;
};

layout(set = MESHLET_PAYLOAD_DESCRIPTOR_SET, binding = MESHLET_PAYLOAD_META_BINDING, std430) readonly buffer MeshletMetas
{
	MeshletMeta data[];
} meshlet_metas;

layout(set = MESHLET_PAYLOAD_DESCRIPTOR_SET, binding = MESHLET_PAYLOAD_STREAM_BINDING, std430) readonly buffer MeshletStreams
{
	MeshletStream data[];
} meshlet_streams;

layout(set = MESHLET_PAYLOAD_DESCRIPTOR_SET, binding = MESHLET_PAYLOAD_PAYLOAD_BINDING, std430) readonly buffer Payload
{
	uint data[];
} payload;

shared u8vec4 shared_chunk_bit_counts[MESHLET_PAYLOAD_NUM_U32_STREAMS][MESHLET_PAYLOAD_NUM_CHUNKS];
shared uint shared_chunk_offset[MESHLET_PAYLOAD_NUM_U32_STREAMS][MESHLET_PAYLOAD_NUM_CHUNKS];
#if MESHLET_PAYLOAD_PACKED_WAVEOPS
shared u8vec4 chunk_values[MESHLET_PAYLOAD_NUM_CHUNKS];
#else
shared uvec2 chunk_values[MESHLET_PAYLOAD_NUM_CHUNKS];
#endif

// Hardcodes wave32 atm. Need fallback.

uvec2 pack_u16vec4_to_uvec2(u16vec4 v)
{
	return uvec2(pack32(v.xy), pack32(v.zw));
}

uint repack_uint(uvec2 v)
{
	u16vec4 v16 = u16vec4(unpack16(v.x), unpack16(v.y));
	return pack32(u8vec4(v16));
}

void meshlet_init_workgroup(uint meshlet_index)
{
	int subgroup_lane = int(gl_SubgroupInvocationID);

	for (uint stream_index = gl_SubgroupID; stream_index < MESHLET_PAYLOAD_NUM_U32_STREAMS; stream_index += gl_NumSubgroups)
	{
		// Start by decoding the offset for bitplanes for all u32 streams.
		if (subgroup_lane < int(gl_WorkGroupSize.y))
		{
			uint bitplane_value = uint(meshlet_streams.data[stream_index + MESHLET_PAYLOAD_NUM_U32_STREAMS * meshlet_index].bitplane_meta[subgroup_lane]);
			u16vec4 bit_counts = (u16vec4(bitplane_value) >> u16vec4(0, 4, 8, 12)) & 0xfus;
			u16vec2 bit_counts2 = bit_counts.xy + bit_counts.zw;
			uint total_bits = bit_counts2.x + bit_counts2.y;
			uint offset = meshlet_streams.data[stream_index + NUM_U32_STREAMS * meshlet_index].offset_from_base;
			shared_chunk_offset[stream_index][subgroup_lane] = subgroupExclusiveAdd(total_bits) + offset;
			shared_chunk_bit_counts[stream_index][subgroup_lane] = u8vec4(bit_counts);
		}
	}

	barrier();
}

uint meshlet_get_linear_index()
{
	return gl_SubgroupID * gl_SubgroupSize + gl_SubgroupInvocationID;
}

uint meshlet_decode_stream(uint meshlet_index, uint stream_index)
{
	uint unrolled_stream_index = MESHLET_PAYLOAD_NUM_U32_STREAMS * meshlet_index + stream_index;
	uint offset_from_base = meshlet_streams.data[unrolled_stream_index].offset_from_base;
	u16vec4 predictor_a = meshlet_streams.data[unrolled_stream_index].predictor_a;
	u16vec4 predictor_b = meshlet_streams.data[unrolled_stream_index].predictor_b;
	u8vec4 initial_value_ = meshlet_streams.data[unrolled_stream_index].initial_value;
	uvec2 initial_value = pack_u16vec4_to_uvec2(u16vec4(initial_value_));

	uint chunk_id = gl_SubgroupID;
	int subgroup_lane = int(gl_SubgroupInvocationID);
	uint bitplane_offsets = shared_chunk_offset[stream_index][chunk_id];
	ivec4 bit_counts = ivec4(shared_chunk_bit_counts[stream_index][chunk_id]);

	uvec4 decoded = ivec4(0);

	// Overlap load with consumption.
	// Helps RDNA2 quite a lot here!
	uint value = payload.data[bitplane_offsets];

	for (int i = 0; i < bit_counts.x; i++)
	{
		decoded.x |= bitfieldExtract(value, subgroup_lane, 1) << i;
		value = payload.data[++bitplane_offsets];
	}
	decoded.x = bitfieldExtract(int(decoded.x), 0, bit_counts.x);

	for (int i = 0; i < bit_counts.y; i++)
	{
		decoded.y |= bitfieldExtract(value, subgroup_lane, 1) << i;
		value = payload.data[++bitplane_offsets];
	}
	decoded.y = bitfieldExtract(int(decoded.y), 0, bit_counts.y);

	for (int i = 0; i < bit_counts.z; i++)
	{
		decoded.z |= bitfieldExtract(value, subgroup_lane, 1) << i;
		value = payload.data[++bitplane_offsets];
	}
	decoded.z = bitfieldExtract(int(decoded.z), 0, bit_counts.z);

	for (int i = 0; i < bit_counts.w; i++)
	{
		decoded.w |= bitfieldExtract(value, subgroup_lane, 1) << i;
		value = payload.data[++bitplane_offsets];
	}
	decoded.w = bitfieldExtract(int(decoded.w), 0, bit_counts.w);

	// Resolve deltas in packed 4x8 math.
	uvec2 packed_decoded = pack_u16vec4_to_uvec2(u16vec4(decoded)) & 0xff00ffu;
	uint linear_index = meshlet_get_linear_index();
	if (linear_index == 0)
		packed_decoded += initial_value;
	packed_decoded += pack_u16vec4_to_uvec2((predictor_a + predictor_b * uint16_t(linear_index)) >> 8us);
	packed_decoded = subgroupInclusiveAdd(packed_decoded);

	if (stream_index > 0)
		barrier(); // Resolve WAR hazard from last iteration.
	if (subgroup_lane == int(gl_SubgroupSize) - 1)
		chunk_values[chunk_id] = packed_decoded & 0xff00ffu;
	barrier();
	if (gl_SubgroupID == 0u && subgroup_lane < int(gl_WorkGroupSize.y))
		chunk_values[subgroup_lane] = subgroupInclusiveAdd(chunk_values[subgroup_lane]);
	barrier();
	if (chunk_id != 0)
		packed_decoded += chunk_values[chunk_id - 1];

	return repack_uint(packed_decoded);
}

#endif