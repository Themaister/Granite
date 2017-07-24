/* Copyright (c) 2017 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "math.hpp"
#include "util.hpp"
#include "gli/load.hpp"
#include "gli/texture2d.hpp"
#include "gli/generate_mipmaps.hpp"
#include "gli/save.hpp"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/istreamwrapper.h"
#include <random>
#include <fstream>
#include <gli/core/filter.hpp>
#include "FastNoise.h"
#include "path.hpp"
#include "tool_util.hpp"

using namespace std;
using namespace Util;
using namespace Granite;
using namespace rapidjson;

static const float height_offset = -64.0f;
static const float height_offset_y = -2.0f;
static const float height_scale = 128.0f;
static const float height_scale_y = 3.0f;

static float sample_heightmap(const gli::texture &tex, float x, float y)
{
	x = clamp(x, 0.0f, float(tex.extent(0).x - 2));
	y = clamp(y, 0.0f, float(tex.extent(0).y - 2));

	float ix = floor(x);
	float iy = floor(y);
	float fx = x - ix;
	float fy = y - iy;
	int sx = int(ix);
	int sy = int(iy);

	float h00 = unpackHalf1x16(tex.load<uint16_t>(gli::texture::extent_type(sx, sy, 0), 0, 0, 0));
	float h10 = unpackHalf1x16(tex.load<uint16_t>(gli::texture::extent_type(sx + 1, sy, 0), 0, 0, 0));
	float h01 = unpackHalf1x16(tex.load<uint16_t>(gli::texture::extent_type(sx, sy + 1, 0), 0, 0, 0));
	float h11 = unpackHalf1x16(tex.load<uint16_t>(gli::texture::extent_type(sx + 1, sy + 1, 0), 0, 0, 0));

	float x0 = mix(h00, h10, fx);
	float x1 = mix(h01, h11, fx);
	return mix(x0, x1, fy);
}

static void add_geometry(vector<vec3> &objects, mt19937 &rnd, const gli::texture &heightmap, const gli::texture &splatmap,
                         float *clutter, int width, int height,
                         int damage_radius,
                         float min_weight, float max_weight,
                         unsigned count,
                         const vec4 &splat_weights)
{
	uniform_real_distribution<float> dist_w(0.5f, width - 0.5f);
	uniform_real_distribution<float> dist_h(0.5f, height - 0.5f);
	uniform_real_distribution<float> dist_clutter(0.0f, 1.0f);
	uniform_real_distribution<float> dist_angle(0.0f, 2.0f * pi<float>());

	FastNoise noise;
	noise.SetFrequency(0.004f);

	float damage_weight = 3.0f / float(damage_radius * damage_radius);

	const auto clustering_sample = [&noise](float x, float y) -> float {
		auto value = noise.GetSimplex(x, y);
		return value;
	};

	for (unsigned i = 0; i < count; i++)
	{
		float x = dist_w(rnd);
		float y = dist_h(rnd);

		float &current = clutter[int(glm::max(y - 0.5f, 0.0f)) * width + int(glm::max(x - 0.5f, 0.0f))];

		using Pixel = glm::tvec4<uint8_t>;
		auto splat_pixel = splatmap.load<Pixel>(gli::extent3d(int(glm::max(x - 0.5f, 0.0f)), int(glm::max(y - 0.5f, 0.0f)), 0), 0, 0, 0);
		vec4 splat_normalized = vec4(splat_pixel.x, splat_pixel.y, splat_pixel.z, splat_pixel.w) * (1.0f / 255.0f);
		splat_normalized.w = glm::max(1.0f - splat_normalized.x - splat_normalized.y - splat_normalized.z, 0.0f);
		float weighted_current = current * dot(splat_normalized, splat_weights);

		float random_clutter = dist_clutter(rnd);
		float random_range = clustering_sample(x, y);

		// We can place something here!
		if (weighted_current > random_clutter && random_range > min_weight && random_range < max_weight)
		{
			float u = x / width;
			float v = y / height;
			objects.push_back(vec3(u, sample_heightmap(heightmap, x, y), v));

			x -= 0.5f;
			y -= 0.5f;
			int ix = int(x);
			int iy = int(y);

			// Damage a radius around the tree to discourage more clutter.
			int start_x = glm::max(ix - damage_radius + 1, 0);
			int end_x = glm::min(ix + damage_radius, width - 1);
			int start_y = glm::max(iy - damage_radius + 1, 0);
			int end_y = glm::min(iy + damage_radius, height - 1);

			for (int damage_y = start_y; damage_y <= end_y; damage_y++)
			{
				for (int damage_x = start_x; damage_x <= end_x; damage_x++)
				{
					float dist_x = damage_x - x;
					float dist_y = damage_y - y;
					float dist_sqr = dist_x * dist_x + dist_y * dist_y;
					clutter[damage_y * width + damage_x] -= 1.5f * exp2(-damage_weight * dist_sqr);
				}
			}
		}
	}
}

static void add_objects(Value &nodes, mt19937 &rnd, const vector<vec3> &objects, const char *mesh, float y_offset, MemoryPoolAllocator<> &allocator)
{
	uniform_real_distribution<float> dist_angle(0.0f, 2.0f * pi<float>());

	for (auto &object : objects)
	{
		Value t(kObjectType);
		t.AddMember("scene", StringRef(mesh), allocator);

		Value translation(kArrayType);
		translation.PushBack(object.x * height_scale + height_offset, allocator);
		translation.PushBack((object.y + y_offset) * height_scale_y + height_offset_y, allocator);
		translation.PushBack(object.z * height_scale + height_offset, allocator);
		t.AddMember("translation", translation, allocator);

		Value rotation(kArrayType);
		float angle = dist_angle(rnd);
		quat q = angleAxis(angle, vec3(0.0f, 1.0f, 0.0f));
		rotation.PushBack(q.x, allocator);
		rotation.PushBack(q.y, allocator);
		rotation.PushBack(q.z, allocator);
		rotation.PushBack(q.w, allocator);
		t.AddMember("rotation", rotation, allocator);

		nodes.PushBack(t, allocator);
	}
}

static float get_neighbor_normal_y(const gli::texture &normals, int x, int y, int width, int height)
{
	const auto convert_normal = [](uint32_t v) -> vec3 {
		return vec3((uvec3(v) >> uvec3(0, 10, 20)) & uvec3(0x3ff)) * (1.0f / 1023.0f) * 2.0f - 1.0f;
	};

	float normal_y = 1.0f;
	int sx = glm::max(x - 1, 0);
	int ex = glm::min(x + 1, width - 1);
	int sy = glm::max(y - 1, 0);
	int ey = glm::min(y + 1, height - 1);

	for (int j = sy; j <= ey; j++)
	{
		for (int i = sx; i <= ex; i++)
		{
			vec3 n = convert_normal(normals.load<uint32_t>(gli::texture::extent_type(i, j, 0), 0, 0, 0));
			n = normalize(n);
			normal_y = glm::min(n.z, normal_y);
		}
	}

	normal_y = glm::max(normal_y, 0.0f);
	return normal_y;
}

int main(int argc, char *argv[])
{
	if (argc != 7)
	{
		LOGE("Usage: %s heightmap normalmap splatmap scene-desc scene-output occlusionmap\n", argv[0]);
		return 1;
	}

	auto heightmap = gli::load(argv[1]);
	auto normals = gli::load(argv[2]);
	auto splatmap = gli::load(argv[3]);

	if (heightmap.empty())
	{
		LOGE("Failed to load heightmap: %s\n", argv[1]);
		return 1;
	}

	if (normals.empty())
	{
		LOGE("Failed to load normalmap: %s\n", argv[2]);
		return 1;
	}

	if (normals.format() != gli::FORMAT_RGB10A2_UNORM_PACK32)
	{
		LOGE("Unexpected format on normalmap: %s\n", argv[2]);
		return 1;
	}

	if (splatmap.empty())
	{
		LOGE("Failed to load splatmap: %s\n", argv[3]);
		return 1;
	}

	if (heightmap.extent(0).x != normals.extent(0).x || heightmap.extent(0).y != normals.extent(0).y)
	{
		LOGE("Heightmap size != normalmap size\n");
		return 1;
	}

	if (heightmap.extent(0).x != splatmap.extent(0).x || heightmap.extent(0).y != splatmap.extent(0).y)
	{
		LOGE("Heightmap size != splatmap size\n");
		return 1;
	}

	Document desc;
	ifstream ifs(argv[4]);
	IStreamWrapper wrapper(ifs);
	desc.ParseStream(wrapper);

	gli::texture2d clutter_mask(gli::FORMAT_R32_SFLOAT_PACK32, gli::extent2d(splatmap.extent(0).x, splatmap.extent(0).y), 1);
	int width = splatmap.extent(0).x;
	int height = splatmap.extent(0).y;

	auto *clutter = static_cast<float *>(clutter_mask.data());

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			float normal_y = get_neighbor_normal_y(normals, x, y, width, height);
			clutter[y * width + x] = pow(normal_y, 10.0f);
		}
	}

	Value nodes(kArrayType);

	mt19937 rnd(0);

	Document doc;
	doc.SetObject();
	auto &allocator = doc.GetAllocator();
	Value scene_list(kObjectType);

	for (auto itr = desc["types"].MemberBegin(); itr != desc["types"].MemberEnd(); ++itr)
	{
		auto &type = itr->value;
		vector<vec3> objects;
		add_geometry(objects, rnd, heightmap, splatmap,
		             clutter, width, height,
		             type["damageRadius"].GetInt(),
		             type["minWeight"].GetFloat(), type["maxWeight"].GetFloat(),
		             type["count"].GetUint(),
		             vec4(type["splatTypes"][0].GetFloat(),
		                  type["splatTypes"][1].GetFloat(),
		                  type["splatTypes"][2].GetFloat(),
		                  type["splatTypes"][3].GetFloat()));

		add_objects(nodes, rnd, objects, itr->name.GetString(), type["yOffset"].GetFloat(), allocator);
		scene_list.AddMember(itr->name, type["mesh"], allocator);
	}

	doc.AddMember("nodes", nodes, allocator);

	Value t(kArrayType);
	Value s(kArrayType);

	t.PushBack(height_offset, allocator);
	t.PushBack(height_offset_y, allocator);
	t.PushBack(height_offset, allocator);
	s.PushBack(height_scale, allocator);
	s.PushBack(height_scale_y, allocator);
	s.PushBack(height_scale, allocator);

	Value terrain(kObjectType);
	terrain.AddMember("heightmap", "../textures/heightmap.ktx", allocator);
	terrain.AddMember("normalmap", "../textures/normalmap.ktx", allocator);
	terrain.AddMember("occlusionmap", "../textures/occlusionmap.ktx", allocator);
	terrain.AddMember("translation", t, allocator);
	terrain.AddMember("scale", s, allocator);
	terrain.AddMember("lodBias", 0.0f, allocator);
	terrain.AddMember("tilingFactor", 64.0f, allocator);
	terrain.AddMember("normalSize", 128, allocator);
	terrain.AddMember("size", width, allocator);
	terrain.AddMember("baseColorTexture", "../textures/Grass_BaseColor_Array.ktx", allocator);
	terrain.AddMember("normalTexture", "../textures/Grass_NormalMap.ktx", allocator);
	terrain.AddMember("splatmapTexture", "../textures/splatmap.ktx", allocator);
	terrain.AddMember("patchData", "bias.json", allocator);

	doc.AddMember("scenes", scene_list, allocator);
	doc.AddMember("terrain", terrain, allocator);
	if (desc.HasMember("background"))
	{
		doc.AddMember("background", desc["background"], allocator);

		auto &bg = doc["background"];
		if (bg.HasMember("fog"))
		{
			auto &fog = bg["fog"];
			if (!fog.HasMember("color") && bg.HasMember("skybox"))
			{
				auto skydome_path = Path::relpath(argv[5], bg["skybox"].GetString());
				auto skydome = gli::load(skydome_path);
				if (skydome.empty())
				{
					LOGE("Failed to load skydome: %s\n", skydome_path.c_str());
					return 1;
				}
				vec4 color;
				skybox_to_fog_color(color, skydome);
				Value v(kArrayType);
				v.PushBack(color.r, allocator);
				v.PushBack(color.g, allocator);
				v.PushBack(color.b, allocator);
				fog.AddMember("color", v, allocator);
			}
		}
	}

	StringBuffer buffer;
	Writer<StringBuffer> writer(buffer);
	doc.Accept(writer);

	FILE *file = fopen(argv[5], "w");
	if (!file)
	{
		LOGE("Failed to open JSON file for writing: %s\n", argv[4]);
		return 1;
	}

	fputs(buffer.GetString(), file);
	fclose(file);

	gli::texture2d clutter_mask_unorm(gli::FORMAT_R8_UNORM_PACK8, gli::extent2d(width, height), num_miplevels(width, height));
	uint8_t *data = static_cast<uint8_t *>(clutter_mask_unorm.data());
	for (int i = 0; i < width * height; i++)
		data[i] = uint8_t(clamp(round((clutter[i] * 0.75f + 0.25f) * 255.0f), 32.0f, 255.0f));
	clutter_mask_unorm = gli::generate_mipmaps(clutter_mask_unorm, gli::FILTER_LINEAR);

	if (!gli::save(clutter_mask_unorm, argv[6]))
	{
		LOGE("Failed to save clutter mask texture.\n");
		return 1;
	}
}
