/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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

#include "gltf.hpp"
#include "gltf_export.hpp"
#include "logging.hpp"
#include "cli_parser.hpp"
#include "rapidjson_wrapper.hpp"

using namespace Granite;
using namespace Util;
using namespace std;

static SceneFormats::TextureCompressionFamily string_to_compression(const string &fmt)
{
	if (fmt == "bc")
		return SceneFormats::TextureCompressionFamily::BC;
	else if (fmt == "astc")
		return SceneFormats::TextureCompressionFamily::ASTC;
	else if (fmt == "png")
		return SceneFormats::TextureCompressionFamily::PNG;
	else
	{
		LOGE("Unrecognized format, using uncompressed.\n");
		return SceneFormats::TextureCompressionFamily::Uncompressed;
	}
}

static void print_help()
{
	LOGI("Usage: [--output <out.glb>] [--texcomp <type>]\n");
	LOGI("[--environment-reflection <path>] [--environment-cube <path>]\n");
	LOGI("[--environment-irradiance <path>] [--environment-texcomp <type>]\n");
	LOGI("[--environment-texcomp-quality <1 (fast) - 5 (slow)>]\n");
	LOGI("[--environment-intensity <intensity>]\n");
	LOGI("[--threads <num threads>]\n");
	LOGI("[--fog-color R G B] [--fog-falloff falloff]\n");
	LOGI("[--extra-lights lights.json]\n");
	LOGI("[--texcomp-quality <1 (fast) - 5 (slow)>] input.gltf\n");
	LOGI("[--animate-cameras]\n");
	LOGI("[--optimize-meshes]\n");
	LOGI("[--stripify-meshes]\n");
	LOGI("[--quantize-attributes]\n");
	LOGI("[--flip-tangent-w]\n");
	LOGI("[--renormalize-normals]\n");
	LOGI("[--gltf]\n");
}

int main(int argc, char *argv[])
{
	Global::init(Global::MANAGER_FEATURE_THREAD_GROUP_BIT |
	             Global::MANAGER_FEATURE_FILESYSTEM_BIT |
	             Global::MANAGER_FEATURE_EVENT_BIT);

	struct Arguments
	{
		string input;
		string output;
	} args;

	SceneFormats::ExportOptions options;
	float scale = 1.0f;
	string extra_lights;
	string extra_cameras;
	bool animate_cameras = false;
	bool flip_tangent_w = false;
	bool renormalize_normals = false;

	CLICallbacks cbs;
	cbs.add("--output", [&](CLIParser &parser) { args.output = parser.next_string(); });
	cbs.add("--texcomp", [&](CLIParser &parser) { options.compression = string_to_compression(parser.next_string()); });
	cbs.add("--texcomp-quality", [&](CLIParser &parser) { options.texcomp_quality = parser.next_uint(); });
	cbs.add("--environment-cube", [&](CLIParser &parser) { options.environment.cube = parser.next_string(); });
	cbs.add("--environment-reflection", [&](CLIParser &parser) { options.environment.reflection = parser.next_string(); });
	cbs.add("--environment-irradiance", [&](CLIParser &parser) { options.environment.irradiance = parser.next_string(); });
	cbs.add("--environment-texcomp", [&](CLIParser &parser) { options.environment.compression = string_to_compression(parser.next_string()); });
	cbs.add("--environment-texcomp-quality", [&](CLIParser &parser) { options.environment.texcomp_quality = parser.next_uint(); });
	cbs.add("--environment-intensity", [&](CLIParser &parser) { options.environment.intensity = parser.next_double(); });
	cbs.add("--extra-lights", [&](CLIParser &parser) { extra_lights = parser.next_string(); });
	cbs.add("--extra-cameras", [&](CLIParser &parser) { extra_cameras = parser.next_string(); });
	cbs.add("--scale", [&](CLIParser &parser) { scale = parser.next_double(); });
	cbs.add("--animate-cameras", [&](CLIParser &) { animate_cameras = true; });
	cbs.add("--flip-tangent-w", [&](CLIParser &) { flip_tangent_w = true; });
	cbs.add("--renormalize-normals", [&](CLIParser &) { renormalize_normals = true; });
	cbs.add("--gltf", [&](CLIParser &) { options.gltf = true; });

	cbs.add("--fog-color", [&](CLIParser &parser) {
		for (unsigned i = 0; i < 3; i++)
			options.environment.fog_color[i] = parser.next_double();
	});

	cbs.add("--fog-falloff", [&](CLIParser &parser) {
		options.environment.fog_falloff = parser.next_double();
	});

	cbs.add("--quantize-attributes", [&](CLIParser &) {
		options.quantize_attributes = true;
	});

	cbs.add("--optimize-meshes", [&](CLIParser &) {
		options.optimize_meshes = true;
	});

	cbs.add("--stripify-meshes", [&](CLIParser &) {
		options.optimize_meshes = true;
		options.stripify_meshes = true;
	});

	cbs.add("--threads", [&](CLIParser &parser) { options.threads = parser.next_uint(); });
	cbs.add("--help", [](CLIParser &parser) { print_help(); parser.end(); });
	cbs.default_handler = [&](const char *arg) { args.input = arg; };
	CLIParser cli_parser(move(cbs), argc - 1, argv + 1);
	if (!cli_parser.parse())
		return 1;
	else if (cli_parser.is_ended_state())
		return 0;

	if (args.input.empty() || args.output.empty())
	{
		print_help();
		return 1;
	}

	GLTF::Parser parser(args.input);
	vector<SceneFormats::Node> nodes;

	SceneFormats::SceneInformation info;
	info.animations = parser.get_animations();
	info.cameras = parser.get_cameras();
	info.lights = parser.get_lights();
	info.materials = parser.get_materials();
	info.meshes = parser.get_meshes();
	info.nodes = parser.get_nodes();
	info.skins = parser.get_skins();
	nodes = parser.get_nodes();

	SceneFormats::SceneNodes custom_nodes;
	auto &scene = parser.get_scenes()[parser.get_default_scene()];

	custom_nodes.name = scene.name;
	custom_nodes.node_indices = scene.node_indices;
	info.scene_nodes = &custom_nodes;

	if (scale != 1.0f)
	{
		SceneFormats::Node root;
		root.children.reserve(nodes.size());
		for (unsigned i = 0; i < nodes.size(); i++)
			root.children.push_back(i);
		root.transform.scale = vec3(scale);

		custom_nodes.node_indices.clear();
		custom_nodes.node_indices.push_back(nodes.size());
		info.scene_nodes = &custom_nodes;
		nodes.push_back(root);
	}

	vector<SceneFormats::Mesh> meshes;
	if (renormalize_normals || flip_tangent_w)
	{
		meshes = parser.get_meshes();
		for (auto &mesh : meshes)
		{
			if (renormalize_normals)
			{
				mesh_renormalize_normals(mesh);
				mesh_renormalize_tangents(mesh);
			}

			if (flip_tangent_w)
				mesh_flip_tangents_w(mesh);
		}
		info.meshes = meshes;
	}

	vector<SceneFormats::CameraInfo> cameras;
	vector<SceneFormats::Animation> animations;
	if (!extra_cameras.empty())
	{
		cameras = parser.get_cameras();

		string json;
		if (!Global::filesystem()->read_file_to_string(extra_cameras, json))
		{
			LOGE("Failed to read config file for lights.\n");
			return 1;
		}

		rapidjson::Document doc;
		doc.Parse(json);

		SceneFormats::Animation animation;

		if (animate_cameras)
		{
			animations = parser.get_animations();

			// Add one camera, which will be animated by a single animating node transform.
			SceneFormats::CameraInfo camera;
			auto &first_camera = doc["cameras"][0];

			camera.type = SceneFormats::CameraInfo::Type::Perspective;
			camera.znear = first_camera["znear"].GetFloat();
			camera.zfar = first_camera["zfar"].GetFloat();
			camera.yfov = first_camera["fovy"].GetFloat();
			camera.aspect_ratio = first_camera["aspect"].GetFloat();
			camera.attached_to_node = true;
			camera.node_index = nodes.size();
			cameras.push_back(camera);

			animation.channels.resize(2);
			animation.channels[0].type = SceneFormats::AnimationChannel::Type::Translation;
			animation.channels[1].type = SceneFormats::AnimationChannel::Type::Rotation;
			animation.channels[0].node_index = nodes.size();
			animation.channels[1].node_index = nodes.size();

			float timestamp = 0.0f;
			for (auto itr = doc["cameras"].Begin(); itr != doc["cameras"].End(); ++itr)
			{
				auto &c = *itr;

				auto &t = c["position"];
				auto &d = c["direction"];
				auto &u = c["up"];
				animation.channels[0].linear.values.emplace_back(t[0].GetFloat(), t[1].GetFloat(), t[2].GetFloat());
				quat rot = conjugate(look_at(vec3(d[0].GetFloat(), d[1].GetFloat(), d[2].GetFloat()),
				                             vec3(u[0].GetFloat(), u[1].GetFloat(), u[2].GetFloat())));
				animation.channels[1].spherical.values.push_back(rot);

				for (auto &chan : animation.channels)
					chan.timestamps.push_back(timestamp);
				timestamp += 1.0f;
			}

			animation.name = "Camera";
			animations.push_back(move(animation));
			custom_nodes.node_indices.push_back(camera.node_index);
			nodes.push_back({});
			info.animations = animations;
		}
		else
		{
			for (auto itr = doc["cameras"].Begin(); itr != doc["cameras"].End(); ++itr)
			{
				auto &c = *itr;

				SceneFormats::CameraInfo camera;
				camera.type = SceneFormats::CameraInfo::Type::Perspective;
				camera.znear = c["znear"].GetFloat();
				camera.zfar = c["zfar"].GetFloat();
				camera.yfov = c["fovy"].GetFloat();
				camera.aspect_ratio = c["aspect"].GetFloat();
				camera.attached_to_node = true;
				camera.node_index = nodes.size();

				cameras.push_back(camera);

				SceneFormats::Node camera_node;
				auto &t = c["position"];
				auto &d = c["direction"];
				auto &u = c["up"];
				camera_node.transform.translation = vec3(t[0].GetFloat(), t[1].GetFloat(), t[2].GetFloat());
				camera_node.transform.rotation =
						conjugate(look_at(vec3(d[0].GetFloat(), d[1].GetFloat(), d[2].GetFloat()),
						                  vec3(u[0].GetFloat(), u[1].GetFloat(), u[2].GetFloat())));

				custom_nodes.node_indices.push_back(camera.node_index);
				nodes.push_back(camera_node);
			}
		}

		info.cameras = cameras;
	}

	vector<SceneFormats::LightInfo> lights;
	if (!extra_lights.empty())
	{
		lights = parser.get_lights();

		string json;
		if (!Global::filesystem()->read_file_to_string(extra_lights, json))
		{
			LOGE("Failed to read config file for lights.\n");
			return 1;
		}

		rapidjson::Document doc;
		doc.Parse(json);

		// Directional
		{
			SceneFormats::Node light_node;
			SceneFormats::LightInfo light;

			light.attached_to_node = true;
			light.node_index = nodes.size();
			light.type = SceneFormats::LightInfo::Type::Directional;

			auto &color = doc["directional"]["color"];
			auto &dir = doc["directional"]["direction"];
			light.color = vec3(color[0].GetFloat(), color[1].GetFloat(), color[2].GetFloat());
			light_node.transform.rotation = conjugate(look_at_arbitrary_up(vec3(dir[0].GetFloat(), dir[1].GetFloat(), dir[2].GetFloat())));

			lights.push_back(light);
			custom_nodes.node_indices.push_back(light.node_index);
			nodes.push_back(light_node);
		}

		auto &spots = doc["spot"];
		for (auto itr = spots.Begin(); itr != spots.End(); ++itr)
		{
			auto &spot = *itr;

			SceneFormats::Node light_node;
			SceneFormats::LightInfo light;
			light.attached_to_node = true;
			light.node_index = nodes.size();
			light.type = SceneFormats::LightInfo::Type::Spot;

			auto &color = spot["color"];
			auto &dir = spot["direction"];
			auto &pos = spot["position"];

			light.color = vec3(color[0].GetFloat(), color[1].GetFloat(), color[2].GetFloat());
			light_node.transform.rotation = conjugate(look_at_arbitrary_up(vec3(dir[0].GetFloat(), dir[1].GetFloat(), dir[2].GetFloat())));
			light_node.transform.translation = vec3(pos[0].GetFloat(), pos[1].GetFloat(), pos[2].GetFloat());
			if (spot.HasMember("range"))
				light.range = spot["range"].GetFloat();
			light.outer_cone = spot["outerCone"].GetFloat();
			light.inner_cone = spot["innerCone"].GetFloat();

			custom_nodes.node_indices.push_back(light.node_index);
			lights.push_back(light);
			nodes.push_back(light_node);
		}

		auto &points = doc["point"];
		for (auto itr = points.Begin(); itr != points.End(); ++itr)
		{
			auto &point = *itr;

			SceneFormats::Node light_node;
			SceneFormats::LightInfo light;
			light.attached_to_node = true;
			light.node_index = nodes.size();
			light.type = SceneFormats::LightInfo::Type::Point;

			auto &color = point["color"];
			auto &pos = point["position"];

			light_node.transform.translation = vec3(pos[0].GetFloat(), pos[1].GetFloat(), pos[2].GetFloat());
			light.color = vec3(color[0].GetFloat(), color[1].GetFloat(), color[2].GetFloat());
			if (point.HasMember("range"))
				light.range = point["range"].GetFloat();

			custom_nodes.node_indices.push_back(light.node_index);
			lights.push_back(light);
			nodes.push_back(light_node);
		}

		info.lights = lights;
	}

	info.nodes = nodes;

	if (!SceneFormats::export_scene_to_glb(info, args.output, options))
	{
		LOGE("Failed to export scene to GLB.\n");
		return 1;
	}

	return 0;
}
