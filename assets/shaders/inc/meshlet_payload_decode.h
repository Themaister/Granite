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

#ifndef MESHLET_PAYLOAD_STREAM_BINDING
#error "Must define MESHLET_PAYLOAD_STREAM_BINDING"
#endif

#ifndef MESHLET_PAYLOAD_PAYLOAD_BINDING
#error "Must define MESHLET_PAYLOAD_PAYLOAD_BINDING"
#endif

struct MeshletStream
{
	uint base_value_or_offsets[2];
	uint bits;
	uint offset_in_words;
};

struct MeshletMetaRuntime
{
	uint stream_offset;
};

struct MeshletInfo
{
	uint primitive_count;
	uint vertex_count;
};

#ifdef MESHLET_PAYLOAD_META_BINDING
layout(set = MESHLET_PAYLOAD_DESCRIPTOR_SET, binding = MESHLET_PAYLOAD_META_BINDING, std430) readonly buffer MeshletMetasRuntime
{
	MeshletMetaRuntime data[];
} meshlet_metas_runtime;
#endif

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
	uint prim_count = meshlet_streams.data[stream_index].base_value_or_offsets[0];
	uint vert_count = meshlet_streams.data[stream_index].base_value_or_offsets[1];
	info.primitive_count = prim_count;
	info.vertex_count = vert_count;
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

uvec3 meshlet_decode_index_buffer(uint stream_index, uint lane_index)
{
	uint offset_in_words = meshlet_streams.data[stream_index].offset_in_words;
	return meshlet_decode3(offset_in_words, lane_index, 5);
}

i16vec3 meshlet_decode_snorm_scaled_i16x3(uint stream_index, uint lane_index, out int exponent)
{
	uint offset_in_words = meshlet_streams.data[stream_index].offset_in_words;
	uint bits = meshlet_streams.data[stream_index].bits;
	exponent = int(bits) >> 16;
	bits &= 0xff;

	uvec3 base_value;
	uint base_value0 = meshlet_streams.data[stream_index].base_value_or_offsets[0];
	uint base_value1 = meshlet_streams.data[stream_index].base_value_or_offsets[1];
	base_value = uvec3(bitfieldExtract(base_value0, 0, 16),
	                   bitfieldExtract(base_value0, 16, 16),
	                   bitfieldExtract(base_value1, 0, 16));

	uvec3 value = meshlet_decode3(offset_in_words, lane_index, bits);
	value += base_value;
	return i16vec3(value);
}

i16vec2 meshlet_decode_snorm_scaled_i16x2(uint stream_index, uint lane_index, out int exponent)
{
	uint offset_in_words = meshlet_streams.data[stream_index].offset_in_words;
	uint bits = meshlet_streams.data[stream_index].bits;
	exponent = int(bits) >> 16;
	bits &= 0xff;

	// Scalar math.
	uint base_value_xy = meshlet_streams.data[stream_index].base_value_or_offsets[0];
	uint base_value_x = bitfieldExtract(base_value_xy, 0, 16);
	uint base_value_y = bitfieldExtract(base_value_xy, 16, 16);
	uvec2 base_value = uvec2(base_value_x, base_value_y);

	uvec2 value = meshlet_decode2(offset_in_words, lane_index, bits);
	value += base_value;
	return i16vec2(value);
}

u8vec4 meshlet_decode_normal_tangent_oct8(uint stream_index, uint lane_index, out bool t_sign)
{
	uint offset_in_words = meshlet_streams.data[stream_index].offset_in_words;
	uint bits = meshlet_streams.data[stream_index].bits;

	// Scalar math.
	uvec4 base_value = uvec4(unpack8(meshlet_streams.data[stream_index].base_value_or_offsets[0]));
	uvec4 value = meshlet_decode4(offset_in_words, lane_index, bits & 0xff);

	value += base_value;

	uint aux = bits >> 16;
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
