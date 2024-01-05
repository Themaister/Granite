#ifndef MESHLET_PAYLOAD_DECODE_H_
#define MESHLET_PAYLOAD_DECODE_H_

#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_control_flow_attributes : require
#extension GL_ARB_gpu_shader_int64 : require

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
	uint offset_in_words;
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
	uint data[];
} payload;

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

uvec2 meshlet_decode2(uint offset_in_words, uint index, uint bit_count)
{
	uint start_bit = index * bit_count * 2;
	uint start_word = offset_in_words + start_bit / 32u;
	start_bit &= 31u;
	uint word0 = payload.data[start_word];
	uint word1 = payload.data[start_word + 1u];
	uvec2 v;

	uint64_t word = packUint2x32(uvec2(word0, word1));
	v.x = uint(word >> start_bit);
	start_bit += bit_count;
	v.y = uint(word >> start_bit);
	return bitfieldExtract(v, 0, int(bit_count));
}

uvec3 meshlet_decode3(uint offset_in_words, uint index, uint bit_count)
{
	uint start_bit = index * bit_count * 3;
	uint start_word = offset_in_words + start_bit / 32u;
	start_bit &= 31u;
	uint word0 = payload.data[start_word];
	uint word1 = payload.data[start_word + 1u];
	uvec3 v;

	uint64_t word = packUint2x32(uvec2(word0, word1));
	v.x = uint(word >> start_bit);
	start_bit += bit_count;
	v.y = uint(word >> start_bit);
	start_bit += bit_count;
	v.z = uint(word >> start_bit);
	return bitfieldExtract(v, 0, int(bit_count));
}

uvec4 meshlet_decode4(uint offset_in_words, uint index, uint bit_count)
{
	uint start_bit = index * bit_count * 4;
	uint start_word = offset_in_words + start_bit / 32u;
	start_bit &= 31u;
	uint word0 = payload.data[start_word];
	uint word1 = payload.data[start_word + 1u];
	uvec4 v;

	uint64_t word = packUint2x32(uvec2(word0, word1));
	v.x = uint(word >> start_bit);
	start_bit += bit_count;
	v.y = uint(word >> start_bit);
	start_bit += bit_count;
	v.z = uint(word >> start_bit);
	start_bit += bit_count;
	v.w = uint(word >> start_bit);
	return bitfieldExtract(v, 0, int(bit_count));
}

uint meshlet_decode_offset(uint bit_plane_config, uint chunk_index, uint components)
{
	// Scalar math.
	uint offset;
	if (chunk_index != 0)
	{
		uint prev_bit_mask = bitfieldExtract(bit_plane_config, 0, int(chunk_index) * 4);
		offset = bitCount(prev_bit_mask & 0x88888888) * 8;
		offset += bitCount(prev_bit_mask & 0x44444444) * 4;
		offset += bitCount(prev_bit_mask & 0x22222222) * 2;
		offset += bitCount(prev_bit_mask & 0x11111111) * 1;
		offset += chunk_index;
		offset *= components;
	}
	else
		offset = 0;

	return offset;
}

uvec3 meshlet_decode_index_buffer(uint stream_index, uint chunk_index, int lane_index)
{
	uint offset_in_words = meshlet_streams.data[stream_index].offset_in_words;
	// Fixed 5-bit encoding.
	offset_in_words += 15 * chunk_index;
	return meshlet_decode3(offset_in_words, lane_index, 5);
}

i16vec3 meshlet_decode_snorm_scaled_i16x3(uint stream_index, uint chunk_index, int lane_index, out int exponent)
{
	uint offset_in_words = meshlet_streams.data[stream_index].offset_in_words;
	uint bit_plane_config = meshlet_streams.data[stream_index].bit_plane_config;
	exponent = meshlet_streams.data[stream_index].aux;

	// Scalar math.
	offset_in_words += meshlet_decode_offset(bit_plane_config, chunk_index, 3);

	uint base_word = chunk_index * 3;
	uint base_word_u32 = base_word / 2;

	uvec3 base_value;
	uint base_value0 = meshlet_streams.data[stream_index].base_value_or_offsets[base_word_u32];
	uint base_value1 = meshlet_streams.data[stream_index].base_value_or_offsets[base_word_u32 + 1];

	if ((chunk_index & 1) != 0)
	{
		base_value = uvec3(bitfieldExtract(base_value0, 16, 16),
		                   bitfieldExtract(base_value1, 0, 16),
		                   bitfieldExtract(base_value1, 16, 16));
	}
	else
	{
		base_value = uvec3(bitfieldExtract(base_value0, 0, 16),
		                   bitfieldExtract(base_value0, 16, 16),
		                   bitfieldExtract(base_value1, 0, 16));
	}

	uint encoded_bits = bitfieldExtract(bit_plane_config, int(chunk_index * 4), 4) + 1;
	uvec3 value = meshlet_decode3(offset_in_words, lane_index, encoded_bits);
	value += base_value;
	return i16vec3(value);
}

i16vec2 meshlet_decode_snorm_scaled_i16x2(uint stream_index, uint chunk_index, int lane_index, out int exponent)
{
	uint offset_in_words = meshlet_streams.data[stream_index].offset_in_words;
	uint bit_plane_config = meshlet_streams.data[stream_index].bit_plane_config;
	exponent = meshlet_streams.data[stream_index].aux;

	offset_in_words += meshlet_decode_offset(bit_plane_config, chunk_index, 2);

	// Scalar math.
	uint base_value_xy = meshlet_streams.data[stream_index].base_value_or_offsets[chunk_index];
	uint base_value_x = bitfieldExtract(base_value_xy, 0, 16);
	uint base_value_y = bitfieldExtract(base_value_xy, 16, 16);
	uvec2 base_value = uvec2(base_value_x, base_value_y);

	uint encoded_bits = bitfieldExtract(bit_plane_config, int(chunk_index * 4), 4) + 1;
	uvec2 value = meshlet_decode2(offset_in_words, lane_index, encoded_bits);
	value += base_value;
	return i16vec2(value);
}

#undef UNROLL_BITS_4
#undef UNROLL_BITS_8

u8vec4 meshlet_decode_normal_tangent_oct8(uint stream_index, uint chunk_index, int lane_index, out bool t_sign)
{
	uint offset_in_words = meshlet_streams.data[stream_index].offset_in_words;
	uint bit_plane_config = meshlet_streams.data[stream_index].bit_plane_config;

	offset_in_words += meshlet_decode_offset(bit_plane_config, chunk_index, 4);

	// Scalar math.
	uvec4 base_value = uvec4(unpack8(meshlet_streams.data[stream_index].base_value_or_offsets[chunk_index]));
	uint encoded_bits = bitfieldExtract(bit_plane_config, int(chunk_index * 4), 4) + 1;
	uvec4 value = meshlet_decode4(offset_in_words, lane_index, encoded_bits);

	value += base_value;

	uint aux = bitfieldExtract(uint(meshlet_streams.data[stream_index].aux), int(chunk_index * 2), 2);
	if (aux == 3)
	{
		t_sign = bool(value.w & 1);
		value.w &= ~1;
	}
	else
		t_sign = aux == 2;

	return u8vec4(value);
}

#endif
