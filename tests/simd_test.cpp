#include "simd.hpp"
#include "muglm/muglm_impl.hpp"
#include "muglm/matrix_helper.hpp"
#include "logging.hpp"
#include "transforms.hpp"
#include "frustum.hpp"
#include <assert.h>
#include <string.h>

using namespace Granite;

static void test_matrix_multiply()
{
	mat4 a(vec4(1, 2, 3, 4), vec4(5, 6, 7, 8), vec4(9, 10, 11, 12), vec4(13, 14, 15, 16));
	mat4 b(vec4(1, 2, 3, 4), vec4(-5, 6, 7, -8), vec4(9, 10, 11, 12), vec4(13, -14, 15, 16));
	mat4 ref = a * b;
	mat4 c;
	SIMD::mul(c, a, b);

	if (memcmp(&ref, &c, sizeof(mat4)) != 0)
	{
		LOGE("Error in matrix multiply!\n");
		exit(1);
	}
}

static void test_aabb_transform()
{
	mat4 test_transform;
	compute_model_transform(test_transform, vec3(8.0f, 6.0f, -3.0f), angleAxis(0.8f, vec3(0.1f, 0.2f, 0.3f)), vec3(8.0f, 1.0f, -0.5f), mat4(1.0f));

	AABB aabb(vec3(-10.0f, 4.0f, 2.0f), vec3(5.0f, 6.0f, 7.0f));
	AABB ref_aabb = aabb.transform(test_transform);
	AABB optim_aabb;
	SIMD::transform_aabb(optim_aabb, aabb, test_transform);
	if (distance(ref_aabb.get_minimum4(), optim_aabb.get_minimum4()) > 0.00001f)
	{
		LOGE("Error aabb!\n");
		exit(1);
	}

	if (distance(ref_aabb.get_maximum4(), optim_aabb.get_maximum4()) > 0.00001f)
	{
		LOGE("Error aabb!\n");
		exit(1);
	}
}

static void test_frustum_cull()
{
	mat4 m = projection(0.4f, 1.0f, 0.1f, 5.0f);
	Frustum frustum;
	frustum.build_planes(inverse(m));

	for (int z = -10; z <= 10; z++)
	{
		for (int y = -10; y <= 10; y++)
		{
			for (int x = -10; x <= 10; x++)
			{
				AABB aabb(vec3(x, y, z) * 0.25f - 0.1f, vec3(x, y, z) * 0.25f + 0.1f);
				bool slow_test = frustum.intersects_slow(aabb);
				bool fast_test = SIMD::frustum_cull(aabb, frustum.get_planes());
				if (slow_test != fast_test)
				{
					LOGE("Frustum cull mismatch.\n");
					exit(1);
				}
			}
		}
	}
}

static void test_quat()
{
	quat q(-0.91354f, 0.123415f, 0.4325f, -0.8434f);
	mat3 reference = mat3_cast(q);

	vec4 cols[3];
	SIMD::convert_quaternion_with_scale(cols, q, vec3(1.0f));

	for (unsigned col = 0; col < 3; col++)
	{
		for (int comp = 0; comp < 3; comp++)
		{
			if (muglm::abs(cols[col][comp] - reference[col][comp]) > 0.00001f)
			{
				LOGE("Quat mismatch!\n");
				exit(1);
			}
		}

		if (cols[col].w != 0.0f)
		{
			LOGE("Quat mismatch!\n");
			exit(1);
		}
	}
}

int main()
{
	test_matrix_multiply();
	test_frustum_cull();
	test_aabb_transform();
	test_quat();
	LOGI(":D\n");
}