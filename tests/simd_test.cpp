#include "simd.hpp"
#include "muglm/muglm_impl.hpp"
#include "util.hpp"
#include "transforms.hpp"
#include <assert.h>
#include <string.h>

using namespace Granite;

int main()
{
	mat4 a(vec4(1, 2, 3, 4), vec4(5, 6, 7, 8), vec4(9, 10, 11, 12), vec4(13, 14, 15, 16));
	mat4 b(vec4(1, 2, 3, 4), vec4(-5, 6, 7, -8), vec4(9, 10, 11, 12), vec4(13, -14, 15, 16));
	mat4 ref = a * b;
	mat4 c;
	SIMD::mul(c, a, b);

	if (memcmp(&ref, &c, sizeof(mat4)) != 0)
	{
		LOGE("Error!\n");
		return 1;
	}

	mat4 test_transform;
	compute_model_transform(test_transform, vec3(8.0f, 6.0f, -3.0f), angleAxis(0.8f, vec3(0.1f, 0.2f, 0.3f)), vec3(8.0f, 1.0f, -0.5f), mat4(1.0f));

	AABB aabb(vec3(-10.0f, 4.0f, 2.0f), vec3(5.0f, 6.0f, 7.0f));
	AABB ref_aabb = aabb.transform(test_transform);
	AABB optim_aabb;
	SIMD::transform_aabb(optim_aabb, aabb, test_transform);
	if (distance(ref_aabb.get_minimum4(), optim_aabb.get_minimum4()) > 0.00001f)
	{
		LOGE("Error!\n");
		return 1;
	}

	if (distance(ref_aabb.get_maximum4(), optim_aabb.get_maximum4()) > 0.00001f)
	{
		LOGE("Error!\n");
		return 1;
	}
}