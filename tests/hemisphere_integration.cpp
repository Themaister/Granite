#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include "logging.hpp"
#include "bitops.hpp"

using namespace Granite;

struct VolumeCube
{
	vec3 directions[6];
};

static vec3 sample_light(const VolumeCube &cube, vec3 n)
{
#if 0
	vec3 n2 = n * n;
	ivec3 index_offset = ivec3(lessThan(n, vec3(0.0f)));

	vec3 result = cube.directions[index_offset.x + 0] * n2.x +
	              cube.directions[index_offset.y + 2] * n2.y +
	              cube.directions[index_offset.z + 4] * n2.z;

	result *= 1.0f / pi<float>();
	return result;
#else
	(void)cube;
	const vec3 dir = normalize(vec3(0.0f, 1.0f, 1.0f));
	return vec3(100.0f, 50.0f, 25.0f) * pow(clamp(dot(n, dir), 0.0f, 1.0f), 100.0f);
#endif
}

mat3 integrate_patch(const VolumeCube &cube, vec3 pos_begin, vec3 pos_dx, vec3 pos_dy)
{
	constexpr unsigned Res = 64;
	mat3 contribution_per_major_axis = mat3(0.0f);

	for (unsigned y = 0; y < Res; y++)
	{
		for (unsigned x = 0; x < Res; x++)
		{
			vec2 uv = vec2(x + 0.5f, y + 0.5f) / vec2(Res);
			vec3 n = pos_begin + uv.x * pos_dx + uv.y * pos_dy;
			float l2 = dot(n, n);
			float inv_l = inversesqrt(l2);
			float area = (1.0f / float(Res * Res)) * inv_l * inv_l * inv_l;

			n *= inv_l;
			vec3 col = sample_light(cube, n);

			vec3 hemisphere_area = abs(n) * area;
			contribution_per_major_axis[0] += col * hemisphere_area.x;
			contribution_per_major_axis[1] += col * hemisphere_area.y;
			contribution_per_major_axis[2] += col * hemisphere_area.z;
		}
	}
	return contribution_per_major_axis;
}

static VolumeCube resample_cube(const VolumeCube &cube)
{
	static const vec3 base_dirs[6] = {
		vec3(1.0f, 0.0f, 0.0f),
		vec3(-1.0f, 0.0f, 0.0f),
		vec3(0.0f, 1.0f, 0.0f),
		vec3(0.0f, -1.0f, 0.0f),
		vec3(0.0f, 0.0f, 1.0f),
		vec3(0.0f, 0.0f, -1.0f),
	};

	static const vec3 right[6] = {
		vec3(0.0f, 0.0f, -1.0f),
		vec3(0.0f, 0.0f, +1.0f),
		vec3(1.0f, 0.0f, 0.0f),
		vec3(1.0f, 0.0f, 0.0f),
		vec3(1.0f, 0.0f, 0.0f),
		vec3(-1.0f, 0.0f, 0.0f),
	};

	static const vec3 downs[6] = {
		vec3(0.0f, -1.0f, 0.0f),
		vec3(0.0f, -1.0f, 0.0f),
		vec3(0.0f, 0.0f, +1.0f),
		vec3(0.0f, 0.0f, -1.0f),
		vec3(0.0f, -1.0f, 0.0f),
		vec3(0.0f, -1.0f, 0.0f),
	};

	mat3 contributions[6 * 2 * 2];

	const auto M = [](unsigned p) { return 1u << p; };

	const uint32_t patch_mask_per_face[6] = {
		(0xf << 0) | M(9) | M(11) | M(13) | M(15) | M(17) | M(19) | M(20) | M(22),
		(0xf << 4) | M(8) | M(10) | M(12) | M(14) | M(16) | M(18) | M(21) | M(23),
		(0xf << 8) | M(0) | M(1) | M(4) | M(5) | M(20) | M(21) | M(16) | M(17),
		(0xf << 12) | M(2) | M(3) | M(6) | M(7) | M(18) | M(19) | M(22) | M(23),
		(0xf << 16) | M(0) | M(2) | M(5) | M(7) | M(10) | M(11) | M(12) | M(13),
		(0xf << 20) | M(1) | M(3) | M(4) | M(6) | M(8) | M(9) | M(14) | M(15),
	};

	VolumeCube result = {};

	for (unsigned face = 0; face < 6; face++)
	{
		for (int patch_y = 0; patch_y < 2; patch_y++)
		{
			for (int patch_x = 0; patch_x < 2; patch_x++)
			{
				vec3 pos = base_dirs[face] +
				           float(patch_x - 1) * right[face] +
				           float(patch_y - 1) * downs[face];

				contributions[face * 4 + patch_y * 2 + patch_x] = integrate_patch(cube, pos, right[face], downs[face]);
			}
		}
	}

	for (unsigned face = 0; face < 6; face++)
	{
		Util::for_each_bit(patch_mask_per_face[face], [&](unsigned bit) {
			result.directions[face] += contributions[bit][face >> 1u];
		});
		result.directions[face] *= 1.0f / pi<float>();
	}

	return result;
}

int main()
{

	VolumeCube cube = {};
	cube.directions[0] = vec3(1.0f, 0.75f, 0.75f);
	cube.directions[1] = vec3(0.5f, 0.75f, 0.75f);
	cube.directions[2] = vec3(0.75f, 1.0f, 0.75f);
	cube.directions[3] = vec3(0.75f, 0.5f, 0.75f);
	cube.directions[4] = vec3(0.75f, 0.75f, 1.0f);
	cube.directions[5] = vec3(0.75f, 0.75f, 0.5f);

	auto resampled_cube = resample_cube(cube);

	const auto log_cube = [&]() {
		printf("=====\n");
		for (unsigned face = 0; face < 6; face++)
		{
			printf("Face %u: (%.3f, %.3f, %.3f).\n", face,
			       resampled_cube.directions[face].x, resampled_cube.directions[face].y,
			       resampled_cube.directions[face].z);
		}
		printf("=====\n");
	};

	log_cube();
	resampled_cube = resample_cube(resampled_cube);
	log_cube();
	resampled_cube = resample_cube(resampled_cube);
	log_cube();
}