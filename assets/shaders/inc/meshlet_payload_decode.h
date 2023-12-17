#ifndef MESHLET_PAYLOAD_DECODE_H_
#define MESHLET_PAYLOAD_DECODE_H_

#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_control_flow_attributes : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_KHR_shader_subgroup_shuffle : require
#extension GL_KHR_shader_subgroup_basic : require

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
	u16vec4 bitplane_meta;
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

shared uvec4 shared_chunk_offset[MESHLET_PAYLOAD_NUM_U32_STREAMS];
shared uvec2 chunk_values0[MESHLET_PAYLOAD_NUM_CHUNKS];
shared uvec2 chunk_values1[MESHLET_PAYLOAD_NUM_CHUNKS];

shared uint wave_buffer_x[MESHLET_PAYLOAD_NUM_CHUNKS];
shared uint wave_buffer_y[MESHLET_PAYLOAD_NUM_CHUNKS];
shared uint wave_buffer_z[MESHLET_PAYLOAD_NUM_CHUNKS];
shared uint wave_buffer_w[MESHLET_PAYLOAD_NUM_CHUNKS];

uvec2 wgx_inclusive_add(uvec2 v)
{
	v = subgroupInclusiveAdd(v);
	if (gl_SubgroupInvocationID == gl_SubgroupSize - 1)
	{
		wave_buffer_x[gl_SubgroupID] = v.x;
		wave_buffer_y[gl_SubgroupID] = v.y;
	}

	barrier();

	for (uint i = 0; i < gl_SubgroupID; i++)
	{
		v.x += wave_buffer_x[i];
		v.y += wave_buffer_y[i];
	}

	return v;
}

uvec4 wgx_inclusive_add(uvec4 v)
{
	v = subgroupInclusiveAdd(v);
	if (gl_SubgroupInvocationID == gl_SubgroupSize - 1)
	{
		wave_buffer_x[gl_SubgroupID] = v.x;
		wave_buffer_y[gl_SubgroupID] = v.y;
		wave_buffer_z[gl_SubgroupID] = v.z;
		wave_buffer_w[gl_SubgroupID] = v.w;
	}

	barrier();

	for (uint i = 0; i < gl_SubgroupID; i++)
	{
		v.x += wave_buffer_x[i];
		v.y += wave_buffer_y[i];
		v.z += wave_buffer_z[i];
		v.w += wave_buffer_w[i];
	}

	return v;
}

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
	if (gl_LocalInvocationIndex < MESHLET_PAYLOAD_NUM_U32_STREAMS)
	{
		uint unrolled_stream_index = base_stream_index + gl_LocalInvocationIndex;
		uvec4 bitplane_values = uvec4(meshlet_streams.data[unrolled_stream_index].bitplane_meta);

		uvec3 total_bits;
		uvec4 bit_counts;
		meshlet_compute_stream_counts(bitplane_values.x, total_bits.x, bit_counts);
		meshlet_compute_stream_counts(bitplane_values.y, total_bits.y, bit_counts);
		meshlet_compute_stream_counts(bitplane_values.z, total_bits.z, bit_counts);
		total_bits.y += total_bits.x;
		total_bits.z += total_bits.y;
		uint chunk_offset = meshlet_streams.data[unrolled_stream_index].offset_from_base;
		shared_chunk_offset[gl_LocalInvocationIndex] = chunk_offset + uvec4(0, total_bits);
	}
	barrier();
}

uint meshlet_get_linear_index()
{
	return gl_SubgroupSize * gl_SubgroupID + gl_SubgroupInvocationID;
}

// Overlap load with consumption.
// Helps RDNA2 quite a lot here!
#define MESHLET_FETCH_BITPLANES(decoded_value, counts, payload_value, offset) \
	for (int i = 0; i < counts; i++) \
	{ \
		decoded_value |= bitfieldExtract(payload_value, local_chunk_index, 1) << i; \
		payload_value = payload.data[++offset]; \
	} \
	decoded_value = bitfieldExtract(int(decoded_value), 0, counts)

// Add some specialized variants.

#define MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index, iter) \
	u16vec4 predictor_a##iter = meshlet_streams.data[unrolled_stream_index].predictor_a; \
	u16vec4 predictor_b##iter = meshlet_streams.data[unrolled_stream_index].predictor_b; \
	u8vec4 initial_value_##iter = meshlet_streams.data[unrolled_stream_index].initial_value; \
	uvec2 initial_value##iter = pack_u16vec4_to_uvec2(u16vec4(initial_value_##iter))

#define MESHLET_PAYLOAD_DECL_CHUNK_OFFSETS(unrolled_stream_index, stream_index, chunk_id, iter) \
	uint bitplane_offsets##iter = shared_chunk_offset[stream_index][chunk_id]; \
	uint bitplane_value##iter = uint(meshlet_streams.data[unrolled_stream_index].bitplane_meta[chunk_id]); \
	ivec4 bit_counts##iter = ivec4(meshlet_decode_bit_counts(bitplane_value##iter))

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

uint meshlet_decode_stream_32_wg256(uint base_stream_index, uint stream_index)
{
	uint unrolled_stream_index = base_stream_index + stream_index;
	uint linear_index = meshlet_get_linear_index();

#if MESHLET_PAYLOAD_WAVE32
	uint chunk_id = gl_SubgroupID;
	int local_chunk_index = int(gl_SubgroupInvocationID);
#else
	uint chunk_id = linear_index / 32u;
	int local_chunk_index = int(linear_index & 31);
#endif

	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index, 0);
	MESHLET_PAYLOAD_PROCESS_CHUNK(unrolled_stream_index, stream_index, chunk_id, 0);
	packed_decoded0 = wgx_inclusive_add(packed_decoded0);
	return repack_uint(packed_decoded0);
}

uvec2 meshlet_decode_stream_64_wg256(uint base_stream_index, uint stream_index)
{
	// Dual-pump the computation. VGPR use is quite low either way, so this is fine.
	uint unrolled_stream_index = base_stream_index + stream_index;
	uint linear_index = meshlet_get_linear_index();

#if MESHLET_PAYLOAD_WAVE32
	uint chunk_id = gl_SubgroupID;
	int local_chunk_index = int(gl_SubgroupInvocationID);
#else
	uint chunk_id = linear_index / 32u;
	int local_chunk_index = int(linear_index & 31);
#endif

	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index, 0);
	MESHLET_PAYLOAD_DECL_STREAM(unrolled_stream_index + 1, 1);
	MESHLET_PAYLOAD_PROCESS_CHUNK(unrolled_stream_index, stream_index, chunk_id, 0);
	MESHLET_PAYLOAD_PROCESS_CHUNK(unrolled_stream_index + 1, stream_index + 1, chunk_id, 1);
	uvec4 packed_decoded = wgx_inclusive_add(uvec4(packed_decoded0, packed_decoded1));
	return uvec2(repack_uint(packed_decoded.xy), repack_uint(packed_decoded.zw));
}

#define MESHLET_DECODE_STREAM_32(meshlet_index, stream_index, report_cb) { \
	uint value = meshlet_decode_stream_32_wg256(meshlet_index, stream_index); \
	report_cb(meshlet_get_linear_index(), value); }

#define MESHLET_DECODE_STREAM_64(meshlet_index, stream_index, report_cb) { \
	uvec2 value = meshlet_decode_stream_64_wg256(meshlet_index, stream_index); \
	report_cb(meshlet_get_linear_index(), value); }

#endif
