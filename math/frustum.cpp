#include "frustum.hpp"

namespace Granite
{

bool Frustum::intersects(const AABB &aabb) const
{
	for (auto &plane : planes)
	{
		bool intersects_plane = false;
		for (unsigned i = 0; i < 8; i++)
		{
			if (dot(vec4(aabb.get_corner(i), 1.0f), plane) >= 0.0f)
			{
				intersects_plane = true;
				break;
			}
		}

		if (!intersects_plane)
			return false;
	}

	return true;
}

void Frustum::build_planes(const mat4 &inv_projection)
{
	static const vec4 tln(-1.0f, -1.0f, 0.0f, 1.0f);
	static const vec4 tlf(-1.0f, -1.0f, 1.0f, 1.0f);
	static const vec4 bln(-1.0f, +1.0f, 0.0f, 1.0f);
	static const vec4 blf(-1.0f, +1.0f, 1.0f, 1.0f);
	static const vec4 trn(+1.0f, -1.0f, 0.0f, 1.0f);
	static const vec4 trf(+1.0f, -1.0f, 1.0f, 1.0f);
	static const vec4 brn(+1.0f, +1.0f, 0.0f, 1.0f);
	static const vec4 brf(+1.0f, +1.0f, 1.0f, 1.0f);

	const auto project = [](const vec4 &v) {
		return v.xyz() / vec3(v.w);
	};

	vec3 TLN = project(inv_projection * tln);
	vec3 BLN = project(inv_projection * bln);
	vec3 BLF = project(inv_projection * blf);
	vec3 TRN = project(inv_projection * trn);
	vec3 TRF = project(inv_projection * trf);
	vec3 BRN = project(inv_projection * brn);
	vec3 BRF = project(inv_projection * brf);

	vec3 l = normalize(cross(BLF - BLN, TLN - BLN));
	vec3 r = normalize(cross(TRF - TRN, BRN - TRN));
	vec3 n = normalize(cross(BLN - BRN, TRN - BRN));
	vec3 f = normalize(cross(TRF - BRF, BLF - BRF));
	vec3 t = normalize(cross(TLN - TRN, TRF - TRN));
	vec3 b = normalize(cross(BRF - BRN, BLN - BRN));

	planes[0] = vec4(l, -dot(l, BLN));
	planes[1] = vec4(r, -dot(r, TRN));
	planes[2] = vec4(n, -dot(n, BRN));
	planes[3] = vec4(f, -dot(f, BRF));
	planes[4] = vec4(t, -dot(t, TRN));
	planes[5] = vec4(b, -dot(b, BRN));
}
}