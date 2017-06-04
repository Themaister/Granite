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

vec3 Frustum::get_coord(float dx, float dy, float dz) const
{
	vec4 clip = vec4(2.0f * dx - 1.0f, 2.0f * dy - 1.0f, dz, 1.0f);
	clip = inv_view_projection * clip;
	return clip.xyz() / clip.w;
}

void Frustum::build_planes(const mat4 &inv_view_projection)
{
	this->inv_view_projection = inv_view_projection;
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

	vec3 TLN = project(inv_view_projection * tln);
	vec3 BLN = project(inv_view_projection * bln);
	vec3 BLF = project(inv_view_projection * blf);
	vec3 TRN = project(inv_view_projection * trn);
	vec3 TRF = project(inv_view_projection * trf);
	vec3 BRN = project(inv_view_projection * brn);
	vec3 BRF = project(inv_view_projection * brf);

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