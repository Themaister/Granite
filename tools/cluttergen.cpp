#include "util.hpp"
#include "gli/load.hpp"
#include "gli/texture2d.hpp"
#include "math.hpp"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <random>

using namespace std;
using namespace Util;
using namespace Granite;
using namespace rapidjson;

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

static void add_geometry(vector<vec3> &objects, mt19937 &rnd, const gli::texture &heightmap, float *clutter, int width, int height,
                         int damage_radius, float damage_weight,
                         unsigned count)
{
	uniform_real_distribution<float> dist_w(0.5f, width - 0.5f);
	uniform_real_distribution<float> dist_h(0.5f, height - 0.5f);
	uniform_real_distribution<float> dist_weight(0.25f, 0.75f);
	uniform_real_distribution<float> dist_angle(0.0f, 2.0f * pi<float>());

	for (unsigned i = 0; i < count; i++)
	{
		float x = dist_w(rnd);
		float y = dist_h(rnd);

		float &current = clutter[int(glm::max(y - 0.5f, 0.0f)) * width + int(glm::max(x - 0.5f, 0.0f))];

		// We can place something here!
		if (current > dist_weight(rnd))
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
					clutter[damage_y * width + damage_x] -= exp2(-damage_weight * dist_sqr);
				}
			}
		}
	}
}

static void add_objects(Value &nodes, const vector<vec3> &objects, const char *mesh, MemoryPoolAllocator<> &allocator)
{
	for (auto &object : objects)
	{
		Value t(kObjectType);
		t.AddMember("scene", StringRef(mesh), allocator);

		Value translation(kArrayType);
		translation.PushBack(object.x * 128.0f - 64.0f, allocator);
		translation.PushBack(object.y - 2.0f, allocator);
		translation.PushBack(object.z * 128.0f - 64.0f, allocator);
		t.AddMember("translation", translation, allocator);

#if 0
		Value rotation(kArrayType);
		float angle = dist_angle(rnd);
		quat q = angleAxis(angle, vec3(0.0f, 1.0f, 0.0f));
		rotation.PushBack(q.x, allocator);
		rotation.PushBack(q.y, allocator);
		rotation.PushBack(q.z, allocator);
		rotation.PushBack(q.w, allocator);
		t.AddMember("rotation", rotation, allocator);
#endif

		nodes.PushBack(t, allocator);
	}
}

int main(int argc, char *argv[])
{
	if (argc != 5)
	{
		LOGE("Usage: %s heightmap normalmap splatmap scene\n", argv[0]);
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

	gli::texture2d clutter_mask(gli::FORMAT_R32_SFLOAT_PACK32, gli::extent2d(splatmap.extent(0).x, splatmap.extent(0).y), 1);
	int width = splatmap.extent(0).x;
	int height = splatmap.extent(0).y;

	auto *src = static_cast<const uint32_t *>(splatmap.data());
	auto *clutter = static_cast<float *>(clutter_mask.data());

	const auto convert_normal_y = [](uint32_t v) -> float {
		return float((v >> 20) & 0x3ff) * (1.0f / 1023.0f);
	};

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			float normal_y = glm::max(convert_normal_y(normals.load<uint32_t>(gli::texture::extent_type(x, y, 0), 0, 0, 0)) * 2.0f - 1.0f, 0.0f);
			clutter[y * width + x] = (src[y * width + x] & 0x00ffffffu) ? 0.0f : pow(normal_y, 20.0f);
		}
	}

	Value nodes(kArrayType);

	mt19937 rnd;
	vector<vec3> trees;
	vector<vec3> grass;

	add_geometry(trees, rnd, heightmap, clutter, width, height, 9, 0.01f, 2000);
	add_geometry(grass, rnd, heightmap, clutter, width, height, 3, 0.1f, 10000);

	Document doc;
	doc.SetObject();
	auto &allocator = doc.GetAllocator();

	add_objects(nodes, trees, "pine", allocator);
	add_objects(nodes, grass, "grass", allocator);

	Value scene_list(kObjectType);
	scene_list.AddMember("pine", "Pine.gltf", allocator);
	scene_list.AddMember("grass", "Grass.gltf", allocator);
	doc.AddMember("nodes", nodes, allocator);

	Value t(kArrayType);
	Value s(kArrayType);

	t.PushBack(-64.0f, allocator);
	t.PushBack(-2.0f, allocator);
	t.PushBack(-64.0f, allocator);
	s.PushBack(128.0f, allocator);
	s.PushBack(1.0f, allocator);
	s.PushBack(128.0f, allocator);

	Value terrain(kObjectType);
	terrain.AddMember("heightmap", "../textures/heightmap.ktx", allocator);
	terrain.AddMember("normalmap", "../textures/normalmap.ktx", allocator);
	terrain.AddMember("translation", t, allocator);
	terrain.AddMember("scale", s, allocator);
	terrain.AddMember("lodBias", 0.0f, allocator);
	terrain.AddMember("tilingFactor", 128.0f, allocator);
	terrain.AddMember("size", 2048, allocator);
	terrain.AddMember("baseColorTexture", "../textures/Grass_BaseColor_Array.ktx", allocator);
    terrain.AddMember("normalTexture", "../textures/finenormal.png", allocator);
	terrain.AddMember("splatmapTexture", "../textures/splatmap.ktx", allocator);
	terrain.AddMember("patchData", "bias.json", allocator);

	doc.AddMember("scenes", scene_list, allocator);
	doc.AddMember("terrain", terrain, allocator);

	StringBuffer buffer;
	Writer<StringBuffer> writer(buffer);
	doc.Accept(writer);

	FILE *file = fopen(argv[4], "w");
	if (!file)
	{
		LOGE("Failed to open JSON file for writing: %s\n", argv[4]);
		return 1;
	}

	fputs(buffer.GetString(), file);
	fclose(file);
}