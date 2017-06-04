#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_SWIZZLE
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/mat4x4.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/transform.hpp"

namespace Granite
{
using namespace glm;

inline void quantize_color(uint8_t *v, const vec4 &color)
{
	for (unsigned i = 0; i < 4; i++)
		v[i] = uint8_t(round(clamp(color[i] * 255.0f, 0.0f, 255.0f)));
}
}