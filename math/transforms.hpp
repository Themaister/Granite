#pragma once

#include "math.hpp"

namespace Granite
{
void compute_model_transform(mat4 &world, mat4 &normal, vec3 scale, quat rotation, vec3 translation);
}