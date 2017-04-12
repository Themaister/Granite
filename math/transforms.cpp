#include "transforms.hpp"

namespace Granite
{
void compute_model_transform(mat4 &world, mat4 &normal, vec3 scale, quat rotation, vec3 translation)
{
	mat4 S = glm::scale(scale);
	mat4 R = mat4_cast(rotation);
	mat4 T = glm::translate(translation);

	mat4 model = R * S;
	normal = transpose(inverse(model));
	world = T * model;
}
}