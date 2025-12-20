#ifndef AFFINE_H_
#define AFFINE_H_

struct mat_affine
{
	vec4 rows[3];
};

mat3x4 mat_affine_to_transposed(mat_affine m)
{
	return mat3x4(m.rows[0], m.rows[1], m.rows[2]);
}

mat3 mat_affine_to_transposed3x3(mat_affine m)
{
	return mat3(m.rows[0].xyz, m.rows[1].xyz, m.rows[2].xyz);
}

vec3 mat_affine_get_translation(mat_affine m)
{
	return vec3(m.rows[0].w, m.rows[1].w, m.rows[2].w);
}

vec3 mat_affine_get_forward(mat_affine m)
{
	return vec3(-m.rows[0].z, -m.rows[1].z, -m.rows[2].z);
}

vec3 mat_affine_get_right(mat_affine m)
{
	return vec3(m.rows[0].x, m.rows[1].x, m.rows[2].x);
}

vec3 mat_affine_get_up(mat_affine m)
{
	return vec3(m.rows[0].y, m.rows[1].y, m.rows[2].y);
}

mediump mat3 mat_affine_to_transposed_rotation(mat_affine m)
{
	return mat3(m.rows[0].xyz, m.rows[1].xyz, m.rows[2].xyz);
}

vec3 mul(mat_affine m, vec4 v)
{
	return v * mat_affine_to_transposed(m);
}

vec3 mul(mat_affine m, vec3 v)
{
	return mul(m, vec4(v, 1.0));
}

mediump vec3 mul_normal(mat_affine m, mediump vec3 v)
{
	mediump mat3x4 M = mat3x4(m.rows[0], m.rows[1], m.rows[2]);
	return vec4(v, 0.0) * M;
}

#endif