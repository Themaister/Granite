#ifndef DEBUG_CHANNEL_H_
#define DEBUG_CHANNEL_H_

layout(set = 7, binding = 15, std430) buffer DebugChannelSSBO
{
	uint counter;
	writeonly uint words[];
} debug_channel;

uint allocate_debug_message(uint argument_words, uint code, uvec3 coord)
{
	argument_words += 5u;
	uint offset = atomicAdd(debug_channel.counter, argument_words);
	if (offset + argument_words > debug_channel.words.length())
		offset = ~0u;
	else
	{
		debug_channel.words[offset] = argument_words;
		debug_channel.words[offset + 1u] = code;
		debug_channel.words[offset + 2u] = coord.x;
		debug_channel.words[offset + 3u] = coord.y;
		debug_channel.words[offset + 4u] = coord.z;
		offset += 5u;
	}
	return offset;
}

void add_debug_message(uint code, uvec3 coord)
{
	allocate_debug_message(0, code, coord);
}

void add_debug_message(uint code, uvec3 coord, uint v)
{
	uint offset = allocate_debug_message(1, code, coord);
	if (offset != ~0u)
	{
		debug_channel.words[offset] = v;
	}
}

void add_debug_message(uint code, uvec3 coord, uvec2 v)
{
	uint offset = allocate_debug_message(2, code, coord);
	if (offset != ~0u)
	{
		debug_channel.words[offset] = v.x;
		debug_channel.words[offset + 1u] = v.y;
	}
}

void add_debug_message(uint code, uvec3 coord, uvec3 v)
{
	uint offset = allocate_debug_message(3, code, coord);
	if (offset != ~0u)
	{
		debug_channel.words[offset] = v.x;
		debug_channel.words[offset + 1u] = v.y;
		debug_channel.words[offset + 2u] = v.z;
	}
}

void add_debug_message(uint code, uvec3 coord, uvec4 v)
{
	uint offset = allocate_debug_message(4, code, coord);
	if (offset != ~0u)
	{
		debug_channel.words[offset] = v.x;
		debug_channel.words[offset + 1u] = v.y;
		debug_channel.words[offset + 2u] = v.z;
		debug_channel.words[offset + 3u] = v.w;
	}
}

void add_debug_message(uint code, uvec3 coord, int v)
{
	add_debug_message(code, coord, uint(v));
}

void add_debug_message(uint code, uvec3 coord, ivec2 v)
{
	add_debug_message(code, coord, uvec2(v));
}

void add_debug_message(uint code, uvec3 coord, ivec3 v)
{
	add_debug_message(code, coord, uvec3(v));
}

void add_debug_message(uint code, uvec3 coord, ivec4 v)
{
	add_debug_message(code, coord, uvec4(v));
}

void add_debug_message(uint code, uvec3 coord, float v)
{
	add_debug_message(code, coord, floatBitsToUint(v));
}

void add_debug_message(uint code, uvec3 coord, vec2 v)
{
	add_debug_message(code, coord, floatBitsToUint(v));
}

void add_debug_message(uint code, uvec3 coord, vec3 v)
{
	add_debug_message(code, coord, floatBitsToUint(v));
}

void add_debug_message(uint code, uvec3 coord, vec4 v)
{
	add_debug_message(code, coord, floatBitsToUint(v));
}

#endif