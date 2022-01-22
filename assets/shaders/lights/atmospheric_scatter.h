#ifndef ATMOSPHERIC_SCATTER_H_
#define ATMOSPHERIC_SCATTER_H_

// Loosely based on https://www.shadertoy.com/view/wlBXWK
/*
 * MIT License
 *
 * Copyright (c) 2019 Dimas "Dimev", "Skythedragon" Leenman
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

const vec3 B_rayleigh = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float B_mie = 21.0e-6;
const vec3 B_absorption = vec3(2.04e-5, 4.97e-5, 1.95e-6);
const float G = 0.7;
const float G2 = G * G;
const float H_rayleigh = 8000.0;
const float H_mie = 1200.0;
const float H_absorption = 30000.0;
const float absorption_falloff = 4000.0;
const float E_radius = 6.371e6;
const float H_atmosphere = 100000.0;

float phase_rayleigh(float cos_theta)
{
	float mu = cos_theta;
	float mumu = mu * mu;
	return 3.0 / (50.2654824574 /* (16 * pi) */) * (1.0 + mumu);
}

float phase_mie(float cos_theta)
{
	float g = G;
	float gg = G2;
	float mu = cos_theta;
	float mumu = mu * mu;
	return 3.0 / (25.1327412287 /* (8 * pi) */) * ((1.0 - gg) * (mumu + 1.0)) / (pow(1.0 + gg - 2.0 * mu * g, 1.5) * (2.0 + gg));
}

float density_rayleigh(float h)
{
	return exp(-h / H_rayleigh);
}

float density_mod_absorption(float h)
{
	float denom = (H_absorption - h) / absorption_falloff;
	return 1.0 / (denom * denom + 1.0);
}

float density_mie(float h)
{
	return exp(-h / H_mie);
}

vec2 trace_to_sphere(vec3 pos, vec3 dir, float radius)
{
	// a term is implicitly 1 since dir is normalized.
	float b = 2.0 * dot(pos, dir);
	float c = dot(pos, pos) - radius * radius;
	float quadratic = b * b - 4.0 * c;

	vec2 result;
	if (quadratic < 0.0)
	{
		result = vec2(0.0);
	}
	else
	{
		float q = sqrt(quadratic);
		result.x = (-b - q) * 0.5;
		result.y = (-b + q) * 0.5;
	}

	return result;
}

vec3 transmittance(vec3 optical_depth)
{
	return exp(-optical_depth);
}

vec3 sample_optical_depth(float h, out float depth_R, out float depth_M, float step_length)
{
	depth_R = density_rayleigh(h) * step_length;
	depth_M = density_mie(h) * step_length;
	float depth_A = density_mod_absorption(h) * depth_R;
	vec3 depth = depth_R * B_rayleigh + depth_M * B_mie + depth_A * B_absorption;
	return depth;
}

vec3 sample_optical_depth(float h, float step_length)
{
	float depth_R, depth_M;
	return sample_optical_depth(h, depth_R, depth_M, step_length);
}

vec3 accumulate_optical_depth(vec3 pos, vec3 dir, float t, int light_steps)
{
	vec3 accumulated_optical_depth = vec3(0.0);
	float step_length = t / float(light_steps);
	for (int i = 0; i < light_steps; i++)
	{
		float t_dir = (float(i) + 0.5) * step_length;
		vec3 sample_pos = pos + t_dir * dir;
		float h = max(length(sample_pos) - E_radius, 0.0);
		accumulated_optical_depth += sample_optical_depth(h, step_length);
	}
	return accumulated_optical_depth;
}

vec3 rayleigh_mie_scatter(vec3 V, vec3 L, float camera_height, int primary_steps, int light_steps)
{
	camera_height = max(camera_height, 0.0);
	vec3 pos = vec3(0.0, E_radius + camera_height, 0.0);

	// Accumulate in-scatter in this direction.
	vec2 t_range_atmos = trace_to_sphere(pos, V, E_radius + H_atmosphere);
	t_range_atmos.x = max(t_range_atmos.x, 0.0);

	// Earth is opaque. Make sure we don't trace through the earth, especially relevant for GI lookups
	// which can sample skydome in lots of weird directions.
	// In case we're inside the earths radius, pretend we're above ground for purposes of making math not explode.
	vec2 t_range_earth = trace_to_sphere(pos, V, 0.98 * E_radius);
	// If we have a positive t intersection, ray will hit ground there.
	bool intersects_earth = any(greaterThan(t_range_earth, vec2(0.0)));
	float t_diff = max(t_range_atmos.y - t_range_atmos.x, 0.0);

	vec3 inscatter;

	if (t_diff > 0.0 && !intersects_earth)
	{
		vec3 accumulated_optical_depth = vec3(0.0);
		vec3 inscatter_rayleigh = vec3(0.0);
		vec3 inscatter_mie = vec3(0.0);
		float step_length = t_diff / float(primary_steps);
		for (int i = 0; i < primary_steps; i++)
		{
			float t_view = (float(i) + 0.5) * step_length + t_range_atmos.x;
			vec3 sample_pos = pos + t_view * V;
			float h = max(length(sample_pos) - E_radius, 0.0);

			float depth_R, depth_M;
			vec3 optical_depth_sample = sample_optical_depth(h, depth_R, depth_M, step_length);

			float t_sun = trace_to_sphere(sample_pos, L, E_radius + H_atmosphere).y;
			vec3 optical_depth_total = accumulated_optical_depth + 0.5 * optical_depth_sample +
					accumulate_optical_depth(sample_pos, L, t_sun, light_steps);
			vec3 T = transmittance(optical_depth_total);

			accumulated_optical_depth += optical_depth_sample;
			inscatter_rayleigh += depth_R * T;
			inscatter_mie += depth_M * T;
		}

		float cos_theta = dot(V, L);
		inscatter_rayleigh *= phase_rayleigh(cos_theta) * B_rayleigh;
		inscatter_mie *= phase_mie(cos_theta) * B_mie;
		inscatter = inscatter_rayleigh + inscatter_mie;
	}
	else
		inscatter = vec3(0.0);

	return inscatter;
}

#endif