#ifndef MESHLET_PAYLOAD_DECODE_H_
#define MESHLET_PAYLOAD_DECODE_H_

#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_control_flow_attributes : require

#include "meshlet_payload_constants.h"

#ifndef MESHLET_PAYLOAD_NUM_U32_STREAMS
#error "Must define MESHLET_PAYLOAD_NUM_U32_STREAMS before including meshlet_payload_decode.h"
#endif

#ifndef MESHLET_PAYLOAD_LARGE_WORKGROUP
#error "Must define MESHLET_PAYLOAD_LARGE_WORKGROUP"
#endif

#ifndef MESHLET_PAYLOAD_SUBGROUP
#error "Must define MESHLET_PAYLOAD_SUBGROUP"
#endif

#if MESHLET_PAYLOAD_SUBGROUP
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_KHR_shader_subgroup_shuffle : require
#extension GL_KHR_shader_subgroup_basic : require
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

struct MeshletMetaRaw
{
	uint base_vertex_offset;
	uint8_t num_primitives_minus_1;
	uint8_t num_attributes_minus_1;
	uint16_t reserved;
};

struct MeshletMetaRuntime
{
	uint stream_offset;
	uint16_t num_primitives;
	uint16_t num_attributes;
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
	uint data[];
} payload;

#if MESHLET_PAYLOAD_LARGE_WORKGROUP
shared uint shared_chunk_offset[MESHLET_PAYLOAD_NUM_U32_STREAMS][MESHLET_PAYLOAD_NUM_CHUNKS];
shared uvec2 chunk_values0[MESHLET_PAYLOAD_NUM_CHUNKS];
shared uvec2 chunk_values1[MESHLET_PAYLOAD_NUM_CHUNKS];
#endif

#if !MESHLET_PAYLOAD_SUBGROUP
shared uint wave_buffer_x[gl_WorkGroupSize.y][gl_WorkGroupSize.x];
shared uint wave_buffer_y[gl_WorkGroupSize.y][gl_WorkGroupSize.x];
shared uint wave_buffer_z[gl_WorkGroupSize.y][gl_WorkGroupSize.x];
shared uint wave_buffer_w[gl_WorkGroupSize.y][gl_WorkGroupSize.x];
shared uvec4 wave_broadcast_value[gl_WorkGroupSize.y];

uint wgx_shuffle(uint v, uint lane)
{
	// WAR hazard.
	barrier();
	if (gl_LocalInvocationID.x == lane)
		wave_broadcast_value[gl_LocalInvocationID.y].x = v;
	barrier();
	return wave_broadcast_value[gl_LocalInvocationID.y].x;
}

uvec2 wgx_shuffle(uvec2 v, uint lane)
{
	// WAR hazard.
	barrier();
	if (gl_LocalInvocationID.x == lane)
		wave_broadcast_value[gl_LocalInvocationID.y].xy = v;
	barrier();
	return wave_broadcast_value[gl_LocalInvocationID.y].xy;
}

uvec4 wgx_shuffle(uvec4 v, uint lane)
{
	// WAR hazard.
	barrier();
	if (gl_LocalInvocationID.x == lane)
		wave_broadcast_value[gl_LocalInvocationID.y] = v;
	barrier();
	return wave_broadcast_value[gl_LocalInvocationID.y];
}

#define wgx_broadcast_last(v) wgx_shuffle(v, gl_WorkGroupSize.x - 1)

uint wgx_exclusive_add8(uint v)
{
	// WAR hazard.
	barrier();
	wave_buffer_x[gl_LocalInvocationID.y][gl_LocalInvocationID.x] = v;
	barrier();

	uint idx = gl_LocalInvocationID.x;

	[[unroll]]
	for (int chunk_size = 2; chunk_size <= 8; chunk_size *= 2)
	{
		int upper_mask = chunk_size >> 1;
		int lower_mask = upper_mask - 1;
		int chunk_mask = ~(chunk_size - 1);
		if ((idx & upper_mask) != 0)
		{
			v += wave_buffer_x[gl_LocalInvocationID.y][(idx & chunk_mask) + lower_mask];
			wave_buffer_x[gl_LocalInvocationID.y][idx] = v;
		}
		barrier();
	}

	if (idx > 0)
		v = wave_buffer_x[gl_LocalInvocationID.y][idx - 1];
	else
		v = 0;

	return v;
}

uvec2 wgx_inclusive_add(uvec2 v)
{
	barrier();
	wave_buffer_x[gl_LocalInvocationID.y][gl_LocalInvocationID.x] = v.x;
	wave_buffer_y[gl_LocalInvocationID.y][gl_LocalInvocationID.x] = v.y;

	uint idx = gl_LocalInvocationID.x;

	[[unroll]]
	for (int chunk_size = 2; chunk_size <= 32; chunk_size *= 2)
	{
		int upper_mask = chunk_size >> 1;
		int lower_mask = upper_mask - 1;
		int chunk_mask = ~(chunk_size - 1);
		barrier();
		if ((idx & upper_mask) != 0)
		{
			v.x += wave_buffer_x[gl_LocalInvocationID.y][(idx & chunk_mask) | lower_mask];
			v.y += wave_buffer_y[gl_LocalInvocationID.y][(idx & chunk_mask) | lower_mask];

			if (chunk_size != 32)
			{
				wave_buffer_x[gl_LocalInvocationID.y][idx] = v.x;
				wave_buffer_y[gl_LocalInvocationID.y][idx] = v.y;
			}
		}
	}

	return v;
}

uvec4 wgx_inclusive_add(uvec4 v)
{
	barrier();
	wave_buffer_x[gl_LocalInvocationID.y][gl_LocalInvocationID.x] = v.x;
	wave_buffer_y[gl_LocalInvocationID.y][gl_LocalInvocationID.x] = v.y;
	wave_buffer_z[gl_LocalInvocationID.y][gl_LocalInvocationID.x] = v.z;
	wave_buffer_w[gl_LocalInvocationID.y][gl_LocalInvocationID.x] = v.w;

	uint idx = gl_LocalInvocationID.x;

	[[unroll]]
	for (int chunk_size = 2; chunk_size <= 32; chunk_size *= 2)
	{
		int upper_mask = chunk_size >> 1;
		int lower_mask = upper_mask - 1;
		int chunk_mask = ~(chunk_size - 1);
		barrier();
		if ((idx & upper_mask) != 0)
		{
			v.x += wave_buffer_x[gl_LocalInvocationID.y][(idx & chunk_mask) | lower_mask];
			v.y += wave_buffer_y[gl_LocalInvocationID.y][(idx & chunk_mask) | lower_mask];
			v.z += wave_buffer_z[gl_LocalInvocationID.y][(idx & chunk_mask) | lower_mask];
			v.w += wave_buffer_w[gl_LocalInvocationID.y][(idx & chunk_mask) | lower_mask];

			if (chunk_size != 32)
			{
				wave_buffer_x[gl_LocalInvocationID.y][idx] = v.x;
				wave_buffer_y[gl_LocalInvocationID.y][idx] = v.y;
				wave_buffer_z[gl_LocalInvocationID.y][idx] = v.z;
				wave_buffer_w[gl_LocalInvocationID.y][idx] = v.w;
			}
		}
	}

	return v;
}


#define wgx_subgroup_invocation_id (gl_LocalInvocationID.x)
#define wgx_subgroup_size (gl_WorkGroupSize.x)
#define wgx_subgroup_id (gl_LocalInvocationID.y)
#define wgx_num_subgroups (gl_WorkGroupSize.y)
#define wgx_mark_uniform(v) (v)
#else
#define wgx_exclusive_add8(v) subgroupExclusiveAdd(v)
#define wgx_inclusive_add(v) subgroupInclusiveAdd(v)
#define wgx_subgroup_invocation_id gl_SubgroupInvocationID
#define wgx_subgroup_size gl_SubgroupSize
#define wgx_subgroup_id gl_SubgroupID
#define wgx_num_subgroups gl_NumSubgroups
#define wgx_broadcast_last(v) subgroupBroadcast(v, 31)
#define wgx_mark_uniform(v) subgroupBroadcastFirst(v)
#define wgx_shuffle(v, lane) subgroupShuffle(v, lane)
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

uvec4 meshlet_decode_bit_counts(uint bitplane_value)
{
	uvec4 out_bit_counts;
	out_bit_counts.x = bitfieldExtract(bitplane_value, 0, 4);
	out_bit_counts.y = bitfieldExtract(bitplane_value, 4, 4);
	out_bit_counts.z = bitfieldExtract(bitplane_value, 8, 4);
	out_bit_counts.w = bitfieldExtract(bitplane_value, 12, 4);
	return out_bit_counts;
}

void meshlet_compute_stream_counts(uint bitplane_value, out uint out_total_bits, out uvec4 out_bit_counts)
{
	out_bit_counts = meshlet_decode_bit_counts(bitplane_value);
	uvec2 bit_counts2 = out_bit_counts.xy + out_bit_counts.zw;
	out_total_bits = bit_counts2.x + bit_counts2.y;
}

void meshlet_init_workgroup(uint base_stream_index)
{
#if MESHLET_PAYLOAD_LARGE_WORKGROUP
#if MESHLET_PAYLOAD_SUBGROUP
	for (uint stream_index = wgx_subgroup_id; stream_index < MESHLET_PAYLOAD_NUM_U32_STREAMS; stream_index += wgx_num_subgroups)
	{
		if (wgx_subgroup_invocation_id < MESHLET_PAYLOAD_NUM_CHUNKS)
		{
			uvec4 bit_counts;
			uint total_bits;

			uint unrolled_stream_index = base_stream_index + stream_index;
			uint bitplane_value = uint(meshlet_streams.data[unrolled_stream_index].bitplane_meta[wgx_subgroup_invocation_id]);
			meshlet_compute_stream_counts(bitplane_value, total_bits, bit_counts);

			uint chunk_offset = meshlet_streams.data[unrolled_stream_index].offset_from_base + wgx_exclusive_add8(total_bits);
			// Start by decoding the offset for bitplanes for all u32 streams.
			shared_chunk_offset[stream_index][wgx_subgroup_invocation_id] = chunk_offset;
		}
	}
#else
	for (uint uniform_stream_index = 0; uniform_stream_index < MESHLET_PAYLOAD_NUM_U32_STREAMS; uniform_stream_index += wgx_num_subgroups)
	{
		uint stream_index = uniform_stream_index + wgx_subgroup_id;
		uint bitplane_value;
		uvec4 bit_counts;
		uint total_bits;

		uint unrolled_stream_index = base_stream_index + stream_index;
		bool active_lane = stream_index < MESHLET_PAYLOAD_NUM_U32_STREAMS && wgx_subgroup_invocation_id < MESHLET_PAYLOAD_NUM_CHUNKS;

		if (active_lane)
		{
			bitplane_value = uint(meshlet_streams.data[unrolled_stream_index].bitplane_meta[wgx_subgroup_invocation_id]);
			meshlet_compute_stream_counts(bitplane_value, total_bits, bit_counts);
		}

		// This needs to happen in dynamically uniform control flow.
		uint chunk_offset = wgx_exclusive_add8(total_bits);

		if (active_lane)
		{
			chunk_offset += meshlet_streams.data[unrolled_stream_index].offset_from_base;
			// Start by decoding the offset for bitplanes for all u32 streams.
			shared_chunk_offset[stream_index][wgx_subgroup_invocation_id] = chunk_offset;
		}
	}
#endif
	barrier();
#endif
}

uint meshlet_get_linear_index()
{
#if !MESHLET_PAYLOAD_SUBGROUP
	return gl_LocalInvocationIndex;
#elif MESHLET_PAYLOAD_LARGE_WORKGROUP
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
		decoded_value |= bitfieldExtract(payload_value, int(wgx_subgroup_invocation_id), 1) << i; \
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
#define MESHLET_PAYLOAD_DECL_CHUNK_OFFSETS(unrolled_stream_index, stream_index, chunk_id, iter) \
	uint bitplane_offsets##iter = shared_chunk_offset[stream_index][chunk_id]; \
	uint bitplane_value##iter = uint(meshlet_streams.data[unrolled_stream_index].bitplane_meta[chunk_id]); \
	ivec4 bit_counts##iter = ivec4(meshlet_decode_bit_counts(bitplane_value##iter))
#else
#define MESHLET_PAYLOAD_DECL_CHUNK_OFFSETS(unrolled_stream_index, stream_index, chunk_id, iter) \
	uint bitplane_offsets##iter = wgx_shuffle(shared_chunk_offset##iter, chunk_id); \
	ivec4 bit_counts##iter = ivec4(wgx_shuffle(shared_chunk_bit_counts##iter, chunk_id))
#endif

#define MESHLET_PAYLOAD_PROCESS_CHUNK(unrolled_stream_index, stream_index, chunk_id, iter) \
	uvec4 decoded##iter = ivec4(0); \
	MESHLET_PAYLOAD_DECL_CHUNK_OFFSETS(unrolled_stream_index, stream_index, chunk_id, iter); \
	uint value##iter = payload.data[bitplane_offsets##iter]; \
	MESHLET_FETCH_BITPLANES(decoded##iter.x, bit_counts##iter.x, value##iter, bitplane_offsets##iter); \
	MESHLET_FETCH_BITPLANES(decoded##iter.y, bit_counts##iter.y, value##iter, bitplane_offsets##iter); \
	MESHLET_FETCH_BITPLANES(decoded##iter.z, bit_counts##iter.z, value##iter, bitplane_offsets##iter); \
	MESHLET_FETCH_BITPLANES(decoded##iter.w, bit_counts##iter.w, value##iter, bitplane_offsets##iter); \
	uvec2 packed_decoded##iter = pack_u16vec4_to_uvec2(u16vec4(decoded##iter)) & 0xff00ffu; \
	if (linear_index == 0) \
		packed_decoded##iter += initial_value##iter; \
	packed_decoded##iter += pack_u16vec4_to_uvec2((predictor_a##iter + predictor_b##iter * uint16_t(linear_index)) >> 8us)

#if MESHLET_PAYLOAD_LARGE_WORKGROUP
uint meshlet_decode_stream_32_wg256(uint base_stream_index, uint stream_index)
{
	uint unrolled_stream_index = base_stream_index + stream_index;
	uint linear_index = meshlet_get_linear_index();

	// Some compilers don't understand this is implicitly scalar.
	uint chunk_id = wgx_mark_uniform(gl_LocalInvocationID.y);

	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index, 0);
	MESHLET_PAYLOAD_PROCESS_CHUNK(unrolled_stream_index, stream_index, chunk_id, 0);
	packed_decoded0 = wgx_inclusive_add(packed_decoded0);

	barrier(); // Resolve WAR hazard from last iteration.
	if (wgx_subgroup_invocation_id == wgx_subgroup_size - 1)
		chunk_values0[chunk_id] = packed_decoded0 & 0xff00ffu;
	barrier();

	for (uint i = 0; i < chunk_id; i++)
		packed_decoded0 += chunk_values0[i];

	return repack_uint(packed_decoded0);
}

uvec2 meshlet_decode_stream_64_wg256(uint base_stream_index, uint stream_index)
{
	// Dual-pump the computation. VGPR use is quite low either way, so this is fine.
	uint unrolled_stream_index = base_stream_index + stream_index;
	uint linear_index = meshlet_get_linear_index();

	// Some compilers don't understand this is implicitly scalar.
	uint chunk_id = wgx_mark_uniform(gl_LocalInvocationID.y);

	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index, 0);
	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index + 1, 1);
	MESHLET_PAYLOAD_PROCESS_CHUNK(unrolled_stream_index, stream_index, chunk_id, 0);
	MESHLET_PAYLOAD_PROCESS_CHUNK(unrolled_stream_index + 1, stream_index + 1, chunk_id, 1);
	uvec4 packed_decoded = wgx_inclusive_add(uvec4(packed_decoded0, packed_decoded1));

	barrier(); // Resolve WAR hazard from last iteration.
	if (wgx_subgroup_invocation_id == wgx_subgroup_size - 1)
	{
		chunk_values0[chunk_id] = packed_decoded.xy & 0xff00ffu;
		chunk_values1[chunk_id] = packed_decoded.zw & 0xff00ffu;
	}
	barrier();

	for (uint i = 0; i < chunk_id; i++)
	{
		packed_decoded.xy += chunk_values0[i];
		packed_decoded.zw += chunk_values1[i];
	}

	return uvec2(repack_uint(packed_decoded.xy), repack_uint(packed_decoded.zw));
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
#define MESHLET_DECODE_STREAM_32(base_stream_index, stream_index, report_cb) { \
	uint unrolled_stream_index = base_stream_index + stream_index; \
	uint linear_index = meshlet_get_linear_index(); \
	uvec2 prev_value = uvec2(0); \
	uint shared_chunk_offset0; \
	uvec4 shared_chunk_bit_counts0; \
	uint total_bits0; \
	uint bitplane_value0; \
	if (wgx_subgroup_invocation_id < MESHLET_PAYLOAD_NUM_CHUNKS) \
		bitplane_value0 = uint(meshlet_streams.data[unrolled_stream_index].bitplane_meta[wgx_subgroup_invocation_id]); \
	meshlet_compute_stream_counts(bitplane_value0, total_bits0, shared_chunk_bit_counts0); \
	shared_chunk_offset0 = wgx_exclusive_add8(total_bits0) + meshlet_streams.data[unrolled_stream_index].offset_from_base; \
	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index, 0); \
	[[loop]] \
	for (uint chunk_id = 0; chunk_id < MESHLET_PAYLOAD_NUM_CHUNKS; chunk_id++) \
	{ \
		MESHLET_PAYLOAD_PROCESS_CHUNK(unrolled_stream_index, stream_index, chunk_id, 0); \
		packed_decoded0 = wgx_inclusive_add(packed_decoded0); \
		packed_decoded0 += prev_value; \
		prev_value = wgx_broadcast_last(packed_decoded0) & 0xff00ffu; \
		report_cb(linear_index, repack_uint(packed_decoded0)); \
		linear_index += wgx_subgroup_size; \
	} \
}

// Have to iterate and report once per chunk. Avoids having to spend a lot of LDS memory.
#define MESHLET_DECODE_STREAM_64(base_stream_index, stream_index, report_cb) { \
	uint unrolled_stream_index = base_stream_index + stream_index; \
	uint linear_index = meshlet_get_linear_index(); \
	uvec4 prev_value = uvec4(0); \
	uint shared_chunk_offset0; \
	uvec4 shared_chunk_bit_counts0; \
	uint shared_chunk_offset1; \
	uvec4 shared_chunk_bit_counts1; \
	uint total_bits0; \
	uint total_bits1; \
	uint bitplane_value0; \
	uint bitplane_value1; \
	if (wgx_subgroup_invocation_id < MESHLET_PAYLOAD_NUM_CHUNKS) \
	{ \
		bitplane_value0 = uint(meshlet_streams.data[unrolled_stream_index].bitplane_meta[wgx_subgroup_invocation_id]); \
		bitplane_value1 = uint(meshlet_streams.data[unrolled_stream_index + 1].bitplane_meta[wgx_subgroup_invocation_id]); \
	} \
	meshlet_compute_stream_counts(bitplane_value0, total_bits0, shared_chunk_bit_counts0); \
	meshlet_compute_stream_counts(bitplane_value1, total_bits1, shared_chunk_bit_counts1); \
	shared_chunk_offset0 = wgx_exclusive_add8(total_bits0) + meshlet_streams.data[unrolled_stream_index].offset_from_base; \
	shared_chunk_offset1 = wgx_exclusive_add8(total_bits1) + meshlet_streams.data[unrolled_stream_index + 1].offset_from_base; \
	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index, 0); \
	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index + 1, 1); \
	[[loop]] \
	for (uint chunk_id = 0; chunk_id < MESHLET_PAYLOAD_NUM_CHUNKS; chunk_id++) \
	{ \
		MESHLET_PAYLOAD_PROCESS_CHUNK(unrolled_stream_index, stream_index, chunk_id, 0); \
		MESHLET_PAYLOAD_PROCESS_CHUNK(unrolled_stream_index + 1, stream_index + 1, chunk_id, 1); \
		uvec4 packed_decoded = wgx_inclusive_add(uvec4(packed_decoded0, packed_decoded1)); \
		packed_decoded += prev_value; \
		prev_value = wgx_broadcast_last(packed_decoded) & 0xff00ffu; \
		report_cb(linear_index, uvec2(repack_uint(packed_decoded.xy), repack_uint(packed_decoded.zw))); \
		linear_index += wgx_subgroup_size; \
	} \
}

#endif

#endif
