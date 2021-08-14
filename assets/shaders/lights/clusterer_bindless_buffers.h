#ifndef CLUSTERER_BINDLESS_BUFFERS_H_
#define CLUSTERER_BINDLESS_BUFFERS_H_

#include "../inc/global_bindings.h"
#include "clusterer_data.h"

layout(std140, set = 0, binding = BINDING_GLOBAL_CLUSTERER_PARAMETERS) uniform ClusterParameters
{
	ClustererParametersBindless cluster;
};

layout(std430, set = 0, binding = BINDING_GLOBAL_CLUSTER_TRANSFORM) readonly buffer ClustererData
{
	ClustererBindlessTransforms cluster_transforms;
};

uint cluster_mask_range(uint mask, uvec2 range, uint start_index)
{
	range.x = clamp(range.x, start_index, start_index + 32u);
	range.y = clamp(range.y + 1u, range.x, start_index + 32u);

	uint num_bits = range.y - range.x;
	uint range_mask = num_bits == 32 ?
		0xffffffffu :
		((1u << num_bits) - 1u) << (range.x - start_index);
	return mask & uint(range_mask);
}

#endif