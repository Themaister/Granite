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
const int primary_steps = 32;
const int light_steps = 8;

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

float trace_to_outer_atmosphere(vec3 pos, vec3 dir, float outer_radius)
{
	float a = dot(dir, dir);
	float b = 2.0 * dot(pos, dir);
	float c = dot(pos, pos) - outer_radius * outer_radius;
	float quadratic = b * b - 4.0 * a * c;

	float result;
	if (quadratic < 0.0)
		result = 0.0;
	else
		result = (-b + sqrt(quadratic)) / (2.0 * a);

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

vec3 accumulate_optical_depth(vec3 pos, vec3 dir, float t)
{
	vec3 accumulated_optical_depth = vec3(0.0);
	float step_length = t / float(light_steps);
	for (int i = 0; i < light_steps; i++)
	{
		float t_dir = (float(i) + 0.5) * step_length;
		vec3 sample_pos = pos + t_dir * dir;
		float h = length(sample_pos) - E_radius;
		accumulated_optical_depth += sample_optical_depth(h, step_length);
	}
	return accumulated_optical_depth;
}

vec3 rayleigh_mie_scatter(vec3 V, float camera_height)
{
	// Accumulate in-scatter in this direction.
	float t = trace_to_outer_atmosphere(vec3(0.0, E_radius + camera_height, 0.0), V, E_radius + H_atmosphere);

	vec3 accumulated_optical_depth = vec3(0.0);
	vec3 inscatter_rayleigh = vec3(0.0);
	vec3 inscatter_mie = vec3(0.0);
	float step_length = t / float(primary_steps);
	for (int i = 0; i < primary_steps; i++)
	{
		float t_view = (float(i) + 0.5) * step_length;
		vec3 sample_pos = vec3(0.0, E_radius + camera_height, 0.0) + t_view * V;
		float h = length(sample_pos) - E_radius;

		float depth_R, depth_M;
		vec3 optical_depth_sample = sample_optical_depth(h, depth_R, depth_M, step_length);

		float t_sun = trace_to_outer_atmosphere(sample_pos, registers.sun_direction, E_radius + H_atmosphere);
		vec3 optical_depth_total = accumulated_optical_depth + 0.5 * optical_depth_sample +
				accumulate_optical_depth(sample_pos, registers.sun_direction, t_sun);
		vec3 T = transmittance(optical_depth_total);

		accumulated_optical_depth += optical_depth_sample;
		inscatter_rayleigh += depth_R * T;
		inscatter_mie += depth_M * T;
	}

	float cos_theta = dot(V, registers.sun_direction);
	inscatter_rayleigh *= phase_rayleigh(cos_theta) * B_rayleigh;
	inscatter_mie *= phase_mie(cos_theta) * B_mie;
	vec3 inscatter = inscatter_rayleigh + inscatter_mie;
	return inscatter;
}

#endif