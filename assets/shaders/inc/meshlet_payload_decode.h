#ifndef MESHLET_PAYLOAD_DECODE_H_
#define MESHLET_PAYLOAD_DECODE_H_

#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_KHR_shader_subgroup_shuffle : require
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_subgroup_extended_types_int8 : require

#include "meshlet_payload_constants.h"

#ifndef MESHLET_PAYLOAD_NUM_U32_STREAMS
#error "Must define MESHLET_PAYLOAD_NUM_U32_STREAMS before including meshlet_payload_decode.h"
#endif

#ifndef MESHLET_PAYLOAD_LARGE_WORKGROUP
#error "Must define MESHLET_PAYLOAD_LARGE_WORKGROUP"
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

#if MESHLET_PAYLOAD_LARGE_WORKGROUP
shared u8vec4 shared_chunk_bit_counts[MESHLET_PAYLOAD_NUM_U32_STREAMS][MESHLET_PAYLOAD_NUM_CHUNKS];
shared uint shared_chunk_offset[MESHLET_PAYLOAD_NUM_U32_STREAMS][MESHLET_PAYLOAD_NUM_CHUNKS];
shared uvec2 chunk_values0[MESHLET_PAYLOAD_NUM_CHUNKS];
shared uvec2 chunk_values1[MESHLET_PAYLOAD_NUM_CHUNKS];
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

void meshlet_compute_stream_offsets(uint meshlet_index, uint stream_index,
                                    out uint out_stream_chunk_offset, out u8vec4 out_bit_counts)
{
	if (gl_SubgroupInvocationID < MESHLET_PAYLOAD_NUM_CHUNKS)
	{
		uint bitplane_value = uint(meshlet_streams.data[stream_index + MESHLET_PAYLOAD_NUM_U32_STREAMS * meshlet_index].bitplane_meta[gl_SubgroupInvocationID]);
		u16vec4 bit_counts = (u16vec4(bitplane_value) >> u16vec4(0, 4, 8, 12)) & 0xfus;
		u16vec2 bit_counts2 = bit_counts.xy + bit_counts.zw;
		uint total_bits = bit_counts2.x + bit_counts2.y;
		uint offset = meshlet_streams.data[stream_index + MESHLET_PAYLOAD_NUM_U32_STREAMS * meshlet_index].offset_from_base;
		out_stream_chunk_offset = subgroupExclusiveAdd(total_bits) + offset;
		out_bit_counts = u8vec4(bit_counts);
	}
}

void meshlet_init_workgroup(uint meshlet_index)
{
#if MESHLET_PAYLOAD_LARGE_WORKGROUP

	for (uint stream_index = gl_SubgroupID; stream_index < MESHLET_PAYLOAD_NUM_U32_STREAMS; stream_index += gl_NumSubgroups)
	{
		if (gl_SubgroupInvocationID < MESHLET_PAYLOAD_NUM_CHUNKS)
		{
			// Start by decoding the offset for bitplanes for all u32 streams.
			meshlet_compute_stream_offsets(meshlet_index, stream_index,
										   shared_chunk_offset[stream_index][gl_SubgroupInvocationID],
										   shared_chunk_bit_counts[stream_index][gl_SubgroupInvocationID]);
		}
	}

	barrier();
#endif
}

uint meshlet_get_linear_index()
{
#if MESHLET_PAYLOAD_LARGE_WORKGROUP
	// Rely on SubgroupInvocationID == LocalInvocationID.x here.
	return gl_WorkGroupSize.x * gl_LocalInvocationID.y + gl_SubgroupInvocationID;
#else
	return gl_SubgroupInvocationID;
#endif
}

// Overlap load with consumption.
// Helps RDNA2 quite a lot here!
#define MESHLET_FETCH_BITPLANES(decoded_value, counts, payload_value, offset) \
	for (int i = 0; i < counts; i++) \
	{ \
		decoded_value |= bitfieldExtract(payload_value, int(gl_SubgroupInvocationID), 1) << i; \
		payload_value = payload.data[++offset]; \
	} \
	decoded_value = bitfieldExtract(int(decoded_value), 0, counts)

// Add some specialized variants.

#define MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index, iter) \
	u16vec4 predictor_a##iter = meshlet_streams.data[unrolled_stream_index].predictor_a; \
	u16vec4 predictor_b##iter = meshlet_streams.data[unrolled_stream_index].predictor_b; \
	u8vec4 initial_value_##iter = meshlet_streams.data[unrolled_stream_index].initial_value; \
	uvec2 initial_value##iter = pack_u16vec4_to_uvec2(u16vec4(initial_value_##iter))

#if MESHLET_PAYLOAD_LARGE_WORKGROUP
#define MESHLET_PAYLOAD_DECL_CHUNK_OFFSETS(stream_index, chunk_id, iter) \
	uint bitplane_offsets##iter = shared_chunk_offset[stream_index][chunk_id]; \
	ivec4 bit_counts##iter = ivec4(shared_chunk_bit_counts[stream_index][chunk_id])
#else
#define MESHLET_PAYLOAD_DECL_CHUNK_OFFSETS(stream_index, chunk_id, iter) \
	uint bitplane_offsets##iter = subgroupShuffle(shared_chunk_offset##iter, chunk_id); \
	ivec4 bit_counts##iter = ivec4(subgroupShuffle(shared_chunk_bit_counts##iter, chunk_id))
#endif

#define MESHLET_PAYLOAD_PROCESS_CHUNK(stream_index, chunk_id, iter) \
	uvec4 decoded##iter = ivec4(0); \
	MESHLET_PAYLOAD_DECL_CHUNK_OFFSETS(stream_index, chunk_id, iter); \
	uint value##iter = payload.data[bitplane_offsets##iter]; \
	MESHLET_FETCH_BITPLANES(decoded##iter.x, bit_counts##iter.x, value##iter, bitplane_offsets##iter); \
	MESHLET_FETCH_BITPLANES(decoded##iter.y, bit_counts##iter.y, value##iter, bitplane_offsets##iter); \
	MESHLET_FETCH_BITPLANES(decoded##iter.z, bit_counts##iter.z, value##iter, bitplane_offsets##iter); \
	MESHLET_FETCH_BITPLANES(decoded##iter.w, bit_counts##iter.w, value##iter, bitplane_offsets##iter); \
	uvec2 packed_decoded##iter = pack_u16vec4_to_uvec2(u16vec4(decoded##iter)) & 0xff00ffu; \
	if (linear_index == 0) \
		packed_decoded##iter += initial_value##iter; \
	packed_decoded##iter += pack_u16vec4_to_uvec2((predictor_a##iter + predictor_b##iter * uint16_t(linear_index)) >> 8us); \
	packed_decoded##iter = subgroupInclusiveAdd(packed_decoded##iter)

#if MESHLET_PAYLOAD_LARGE_WORKGROUP
uint meshlet_decode_stream_32_wg256(uint meshlet_index, uint stream_index)
{
	uint unrolled_stream_index = MESHLET_PAYLOAD_NUM_U32_STREAMS * meshlet_index + stream_index;
	uint linear_index = meshlet_get_linear_index();
	uint chunk_id = gl_LocalInvocationID.y;

	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index, 0);
	MESHLET_PAYLOAD_PROCESS_CHUNK(stream_index, chunk_id, 0);

	barrier(); // Resolve WAR hazard from last iteration.
	if (gl_SubgroupInvocationID == MESHLET_PAYLOAD_MAX_ELEMENTS / MESHLET_PAYLOAD_NUM_CHUNKS - 1)
		chunk_values0[chunk_id] = packed_decoded0 & 0xff00ffu;
	barrier();
	if (gl_SubgroupID == 0u && gl_SubgroupInvocationID < gl_WorkGroupSize.y)
		chunk_values0[gl_SubgroupInvocationID] = subgroupInclusiveAdd(chunk_values0[gl_SubgroupInvocationID]);
	barrier();
	if (chunk_id != 0)
		packed_decoded0 += chunk_values0[chunk_id - 1];

	return repack_uint(packed_decoded0);
}

uvec2 meshlet_decode_stream_64_wg256(uint meshlet_index, uint stream_index)
{
	// Dual-pump the computation. VGPR use is quite low either way, so this is fine.
	uint unrolled_stream_index = MESHLET_PAYLOAD_NUM_U32_STREAMS * meshlet_index + stream_index;
	uint linear_index = meshlet_get_linear_index();
	uint chunk_id = gl_LocalInvocationID.y;

	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index, 0);
	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index + 1, 1);
	MESHLET_PAYLOAD_PROCESS_CHUNK(stream_index, chunk_id, 0);
	MESHLET_PAYLOAD_PROCESS_CHUNK(stream_index + 1, chunk_id, 1);

	barrier(); // Resolve WAR hazard from last iteration.
	if (gl_SubgroupInvocationID == gl_SubgroupSize - 1)
	{
		chunk_values0[chunk_id] = packed_decoded0 & 0xff00ffu;
		chunk_values1[chunk_id] = packed_decoded1 & 0xff00ffu;
	}
	barrier();
	if (gl_SubgroupID == 0u && gl_SubgroupInvocationID < gl_WorkGroupSize.y)
		chunk_values0[gl_SubgroupInvocationID] = subgroupInclusiveAdd(chunk_values0[gl_SubgroupInvocationID]);
	else if (gl_SubgroupID == 1u && gl_SubgroupInvocationID < gl_WorkGroupSize.y)
		chunk_values1[gl_SubgroupInvocationID] = subgroupInclusiveAdd(chunk_values1[gl_SubgroupInvocationID]);
	barrier();
	if (chunk_id != 0)
	{
		packed_decoded0 += chunk_values0[chunk_id - 1];
		packed_decoded1 += chunk_values1[chunk_id - 1];
	}

	return uvec2(repack_uint(packed_decoded0), repack_uint(packed_decoded1));
}

// For large workgroups, we imply AMD, where LocalInvocationIndex indexing is preferred.
// We assume that SubgroupInvocationID == LocalInvocationID.x here since it's the only reasonable it would work.
#define MESHLET_DECODE_STREAM_32(meshlet_index, stream_index, report_cb) { \
	uint value = meshlet_decode_stream_32_wg256(meshlet_index, stream_index); \
	report_cb(gl_LocalInvocationIndex, value); }

#define MESHLET_DECODE_STREAM_64(meshlet_index, stream_index, report_cb) { \
	uvec2 value = meshlet_decode_stream_64_wg256(meshlet_index, stream_index); \
	report_cb(gl_LocalInvocationIndex, value); }

#else

// Have to iterate and report once per chunk. Avoids having to spend a lot of LDS memory.
#define MESHLET_DECODE_STREAM_32(meshlet_index, stream_index, report_cb) { \
	uint unrolled_stream_index = MESHLET_PAYLOAD_NUM_U32_STREAMS * meshlet_index + stream_index; \
	uint linear_index = meshlet_get_linear_index(); \
	uvec2 prev_value0 = uvec2(0); \
	uint shared_chunk_offset0; \
	u8vec4 shared_chunk_bit_counts0; \
	meshlet_compute_stream_offsets(meshlet_index, stream_index, shared_chunk_offset0, shared_chunk_bit_counts0); \
	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index, 0); \
	for (uint chunk_id = 0; chunk_id < MESHLET_PAYLOAD_NUM_CHUNKS; chunk_id++) \
	{ \
		MESHLET_PAYLOAD_PROCESS_CHUNK(stream_index, chunk_id, 0); \
		packed_decoded0 += prev_value0; \
		prev_value0 = subgroupBroadcast(packed_decoded0, 31) & 0xff00ffu; \
		report_cb(linear_index, repack_uint(packed_decoded0)); \
		linear_index += gl_SubgroupSize; \
	} \
}

// Have to iterate and report once per chunk. Avoids having to spend a lot of LDS memory.
#define MESHLET_DECODE_STREAM_64(meshlet_index, stream_index, report_cb) { \
	uint unrolled_stream_index = MESHLET_PAYLOAD_NUM_U32_STREAMS * meshlet_index + stream_index; \
	uint linear_index = meshlet_get_linear_index(); \
	uvec2 prev_value0 = uvec2(0); \
	uvec2 prev_value1 = uvec2(0); \
	uint shared_chunk_offset0; \
	u8vec4 shared_chunk_bit_counts0; \
	meshlet_compute_stream_offsets(meshlet_index, stream_index, shared_chunk_offset0, shared_chunk_bit_counts0); \
	uint shared_chunk_offset1; \
	u8vec4 shared_chunk_bit_counts1; \
	meshlet_compute_stream_offsets(meshlet_index, stream_index + 1, shared_chunk_offset1, shared_chunk_bit_counts1); \
	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index, 0); \
	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index + 1, 1); \
	for (uint chunk_id = 0; chunk_id < MESHLET_PAYLOAD_NUM_CHUNKS; chunk_id++) \
	{ \
		MESHLET_PAYLOAD_PROCESS_CHUNK(stream_index, chunk_id, 0); \
		MESHLET_PAYLOAD_PROCESS_CHUNK(stream_index + 1, chunk_id, 1); \
		packed_decoded0 += prev_value0; \
		packed_decoded1 += prev_value1; \
		prev_value0 = subgroupBroadcast(packed_decoded0, 31) & 0xff00ffu; \
		prev_value1 = subgroupBroadcast(packed_decoded1, 31) & 0xff00ffu; \
		report_cb(linear_index, uvec2(repack_uint(packed_decoded0), repack_uint(packed_decoded1))); \
		linear_index += gl_SubgroupSize; \
	} \
}

#endif

#endif