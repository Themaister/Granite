#pragma once

#include "math.hpp"

namespace Granite
{
void compute_model_transform(mat4 &world, mat4 &normal, vec3 scale, quat rotation, vec3 translation);
quat rotate_vector(vec3 from, vec3 to);
quat look_at(vec3 direction, vec3 up);
quat rotate_vector_axis(vec3 from, vec3 to, vec3 axis);
mat4 projection(float fovy, float aspect, float znear, float zfar);
}