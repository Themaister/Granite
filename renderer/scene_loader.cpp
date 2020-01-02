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

#include "scene_loader.hpp"
#include "gltf.hpp"
#include "scene_formats.hpp"
#include "rapidjson_wrapper.hpp"
#include "mesh_util.hpp"
#include "enum_cast.hpp"
#include "ground.hpp"

using namespace std;
using namespace rapidjson;
using namespace Util;

namespace Granite
{

SceneLoader::SceneLoader()
{
	scene = make_unique<Scene>();
	animation_system = make_unique<AnimationSystem>();
}

unique_ptr<AnimationSystem> SceneLoader::consume_animation_system()
{
	return move(animation_system);
}

AnimationSystem &SceneLoader::get_animation_system()
{
	return *animation_system;
}

Scene::NodeHandle SceneLoader::load_scene_to_root_node(const std::string &path)
{
	auto ext = Path::ext(path);
	if (ext == "gltf" || ext == "glb")
	{
		return parse_gltf(path);
	}
	else
	{
		string json;
		if (!Global::filesystem()->read_file_to_string(path, json))
			throw runtime_error("Failed to load GLTF file.");
		return parse_scene_format(path, json);
	}
}

void SceneLoader::load_scene(const std::string &path)
{
	auto node = load_scene_to_root_node(path);
	scene->set_root_node(node);
}

Scene::NodeHandle SceneLoader::build_tree_for_subscene(const SubsceneData &subscene)
{
	auto &parser = *subscene.parser;
	std::vector<Scene::NodeHandle> nodes;
	nodes.reserve(parser.get_nodes().size());

	auto &scene_nodes = parser.get_scenes()[parser.get_default_scene()];
	auto touched = build_used_nodes_in_scene(scene_nodes, parser.get_nodes());

	unsigned node_index = 0;
	for (auto &node : parser.get_nodes())
	{
		if (!node.joint && touched.count(node_index))
		{
			Scene::NodeHandle nodeptr;
			if (node.has_skin)
			{
				nodeptr = scene->create_skinned_node(parser.get_skins()[node.skin]);

#if 1
				auto skin_compat = parser.get_skins()[node.skin].skin_compat;
				for (auto &animation : parser.get_animations())
				{
					if (animation.skin_compat == skin_compat)
					{
						auto animation_id = animation_system->register_animation(animation.name, animation);
						auto state_id = animation_system->start_animation(*nodeptr, animation_id, 0.0);
						animation_system->set_repeating(state_id, true);
					}
				}
#endif
			}
			else
				nodeptr = scene->create_node();

			nodes.push_back(nodeptr);
			nodeptr->transform.translation = node.transform.translation;
			nodeptr->transform.rotation = node.transform.rotation;
			nodeptr->transform.scale = node.transform.scale;
		}
		else
			nodes.push_back({});

		node_index++;
	}

	for (auto &animation : parser.get_animations())
	{
		if (!animation.skinning)
		{
			auto animation_id = animation_system->register_animation(animation.name, animation);
			auto state_id = animation_system->start_animation_multi(nodes.data(), nodes.size(), animation_id, 0.0);
			animation_system->set_repeating(state_id, true);
		}
	}

	unsigned i = 0;
	for (auto &node : parser.get_nodes())
	{
		if (nodes[i])
		{
			for (auto &child : node.children)
				if (nodes[child])
					nodes[i]->add_child(nodes[child]);

			for (auto &mesh : node.meshes)
				scene->create_renderable(subscene.meshes[mesh], nodes[i].get());
		}
		i++;
	}

	for (auto &camera : parser.get_cameras())
	{
		auto cam_entity = this->scene->create_entity();

		Camera cam_params;
		cam_params.set_fovy(camera.yfov);
		cam_params.set_aspect(camera.aspect_ratio);
		cam_params.set_depth_range(camera.znear, camera.zfar);
		cam_entity->allocate_component<CameraComponent>()->camera = cam_params;

		if (camera.attached_to_node && touched.count(camera.node_index))
		{
			auto *t = cam_entity->allocate_component<CachedTransformComponent>();
			t->transform = &nodes[camera.node_index]->cached_transform;
		}
	}

	for (auto &light : parser.get_lights())
	{
		if (light.attached_to_node && touched.count(light.node_index))
			scene->create_light(light, nodes[light.node_index].get());
	}

	auto root = scene->create_node();
#if 0
	for (auto &node : nodes)
		if (node && !node->get_parent())
			root->add_child(node);
#else
	for (auto &scene_node_index : scene_nodes.node_indices)
		root->add_child(nodes[scene_node_index]);
#endif

	return root;
}

void SceneLoader::load_animation(const std::string &path, SceneFormats::Animation &animation)
{
	string str;
	if (!Global::filesystem()->read_file_to_string(path, str))
	{
		LOGE("Failed to load file: %s\n", path.c_str());
		return;
	}

	Document doc;
	doc.Parse(str);
	if (doc.HasParseError())
		throw logic_error("Failed to parse.");

	auto &timestamps = doc["timestamps"];

	vector<float> timestamp_values;
	for (auto itr = timestamps.Begin(); itr != timestamps.End(); ++itr)
		timestamp_values.push_back(itr->GetFloat());

	SceneFormats::AnimationChannel channel;

	if (doc.HasMember("rotation"))
	{
		SlerpSampler slerp;
		auto &rotations = doc["rotation"];
		for (auto itr = rotations.Begin(); itr != rotations.End(); ++itr)
		{
			auto &value = *itr;
			float x = value[0].GetFloat();
			float y = value[1].GetFloat();
			float z = value[2].GetFloat();
			float w = value[3].GetFloat();
			slerp.values.push_back(normalize(quat(w, x, y, z)));
		}

		channel.type = SceneFormats::AnimationChannel::Type::Rotation;
		channel.spherical = move(slerp);
		channel.timestamps = timestamp_values;
		animation.channels.push_back(move(channel));
	}

	if (doc.HasMember("translation"))
	{
		LinearSampler linear;
		auto &rotations = doc["translation"];
		for (auto itr = rotations.Begin(); itr != rotations.End(); ++itr)
		{
			auto &value = *itr;
			float x = value[0].GetFloat();
			float y = value[1].GetFloat();
			float z = value[2].GetFloat();
			linear.values.push_back(vec3(x, y, z));
		}

		channel.type = SceneFormats::AnimationChannel::Type::Translation;
		channel.linear = move(linear);
		channel.timestamps = timestamp_values;
		animation.channels.push_back(move(channel));
	}

	if (doc.HasMember("scale"))
	{
		LinearSampler linear;
		auto &rotations = doc["scale"];
		for (auto itr = rotations.Begin(); itr != rotations.End(); ++itr)
		{
			auto &value = *itr;
			float x = value[0].GetFloat();
			float y = value[1].GetFloat();
			float z = value[2].GetFloat();
			linear.values.push_back(vec3(x, y, z));
		}

		channel.type = SceneFormats::AnimationChannel::Type::Scale;
		channel.linear = move(linear);
		channel.timestamps = timestamp_values;
		animation.channels.push_back(move(channel));
	}

	animation.update_length();
}

Scene::NodeHandle SceneLoader::parse_gltf(const std::string &path)
{
	SubsceneData subscene;
	subscene.parser = make_unique<GLTF::Parser>(path);

	for (auto &mesh : subscene.parser->get_meshes())
		subscene.meshes.push_back(create_imported_mesh(mesh, subscene.parser->get_materials().data()));

	if (!subscene.parser->get_environments().empty())
	{
		auto &env = subscene.parser->get_environments().front();

		Entity *entity = nullptr;
		Util::IntrusivePtr<Skybox> skybox;
		if (!env.cube.path.empty())
		{
			skybox = Util::make_handle<Skybox>(env.cube.path, false);
			entity = scene->create_renderable(skybox, nullptr);
			entity->allocate_component<BackgroundComponent>();

			if (!env.reflection.path.empty() && !env.irradiance.path.empty())
			{
				auto *ibl = entity->allocate_component<IBLComponent>();
				ibl->irradiance_path = env.irradiance.path;
				ibl->reflection_path = env.reflection.path;
				ibl->intensity = env.intensity;
			}
		}

		if (skybox)
			entity->allocate_component<SkyboxComponent>()->skybox = skybox.get();

		if (env.fog.falloff != 0.0f)
		{
			if (!entity)
				entity = scene->create_entity();

			FogParameters params = {};
			params.color = env.fog.color;
			params.falloff = env.fog.falloff;
			entity->allocate_component<EnvironmentComponent>()->fog = params;
		}
	}

	return build_tree_for_subscene(subscene);
}

Scene::NodeHandle SceneLoader::parse_scene_format(const std::string &path, const std::string &json)
{
	Document doc;
	doc.Parse(json);

	if (doc.HasParseError())
		throw logic_error("Failed to parse.");

	auto &scenes = doc["scenes"];
	for (auto itr = scenes.MemberBegin(); itr != scenes.MemberEnd(); ++itr)
	{
		auto gltf_path = Path::relpath(path, itr->value.GetString());
		auto &subscene = subscenes[itr->name.GetString()];
		subscene.parser.reset(new GLTF::Parser(gltf_path));
		auto &parser = *subscene.parser;

		for (auto &mesh : parser.get_meshes())
		{
			SceneFormats::MaterialInfo default_material;
			default_material.uniform_base_color = vec4(0.3f, 1.0f, 0.3f, 1.0f);
			default_material.uniform_metallic = 0.0f;
			default_material.uniform_roughness = 1.0f;
			AbstractRenderableHandle renderable;

			bool skinned = mesh.attribute_layout[ecast(MeshAttribute::BoneIndex)].format != VK_FORMAT_UNDEFINED;
			if (skinned)
			{
				if (mesh.has_material)
					renderable = Util::make_handle<ImportedSkinnedMesh>(mesh,
					                                                    parser.get_materials()[mesh.material_index]);
				else
					renderable = Util::make_handle<ImportedSkinnedMesh>(mesh, default_material);
			}
			else
			{
				if (mesh.has_material)
					renderable = Util::make_handle<ImportedMesh>(mesh,
					                                             parser.get_materials()[mesh.material_index]);
				else
					renderable = Util::make_handle<ImportedMesh>(mesh, default_material);
			}
			subscene.meshes.push_back(renderable);
		}
	}

	vector<Scene::NodeHandle> hierarchy;

	auto &nodes = doc["nodes"];
	for (auto itr = nodes.Begin(); itr != nodes.End(); ++itr)
	{
		auto &elem = *itr;
		bool has_scene = elem.HasMember("scene");

		auto scene_itr = has_scene ? subscenes.find(elem["scene"].GetString()) : end(subscenes);
		if (has_scene && scene_itr == end(subscenes))
			throw logic_error("Scene does not exist.");

		vec3 stride = vec3(0.0f);
		uvec3 instance_size = uvec3(1);
		if (elem.HasMember("grid_stride"))
		{
			auto &grid_stride = elem["grid_stride"];
			stride = vec3(grid_stride[0].GetFloat(), grid_stride[1].GetFloat(), grid_stride[2].GetFloat());
		}

		if (elem.HasMember("grid_size"))
		{
			auto &grid_size = elem["grid_size"];
			instance_size = uvec3(grid_size[0].GetUint(), grid_size[1].GetUint(), grid_size[2].GetUint());
		}

		Transform transform;

		if (elem.HasMember("translation"))
		{
			auto &t = elem["translation"];
			transform.translation = vec3(t[0].GetFloat(), t[1].GetFloat(), t[2].GetFloat());
		}

		if (elem.HasMember("rotation"))
		{
			auto &r = elem["rotation"];
			transform.rotation = normalize(quat(r[3].GetFloat(), r[0].GetFloat(), r[1].GetFloat(), r[2].GetFloat()));
		}

		if (elem.HasMember("scale"))
		{
			auto &s = elem["scale"];
			transform.scale = vec3(s[0].GetFloat(), s[1].GetFloat(), s[2].GetFloat());
		}

		if (has_scene)
		{
			if (all(equal(instance_size, uvec3(1))))
				hierarchy.push_back(build_tree_for_subscene(scene_itr->second));
			else
			{
				auto subroot = scene->create_node();
				for (unsigned z = 0; z < instance_size.z; z++)
				{
					for (unsigned y = 0; y < instance_size.y; y++)
					{
						for (unsigned x = 0; x < instance_size.x; x++)
						{
							auto child = build_tree_for_subscene(scene_itr->second);
							child->transform.translation = vec3(x, y, z) * stride;
							subroot->add_child(child);
						}
					}
				}
				hierarchy.push_back(subroot);
			}
		}
		else
			hierarchy.push_back(scene->create_node());

		hierarchy.back()->transform = transform;
	}

	if (doc.HasMember("animations"))
	{
		unsigned index = 0;
		auto &animations = doc["animations"];
		for (auto itr = animations.Begin(); itr != animations.End(); ++itr)
		{
			auto &animation = *itr;
			SceneFormats::Animation track;

			if (animation.HasMember("axisAngle"))
			{
				auto &rotation = animation["axisAngle"];
				float x = rotation[0].GetFloat();
				float y = rotation[1].GetFloat();
				float z = rotation[2].GetFloat();
				vec3 direction = normalize(vec3(x, y, z));
				float angular_freq = rotation[3].GetFloat();
				float time_for_rotation = 2.0f * pi<float>() / angular_freq;

				SceneFormats::AnimationChannel channel;
				channel.type = SceneFormats::AnimationChannel::Type::Rotation;
				channel.spherical.values.push_back(angleAxis(0.00f * 2.0f * pi<float>(), direction));
				channel.spherical.values.push_back(angleAxis(0.25f * 2.0f * pi<float>(), direction));
				channel.spherical.values.push_back(angleAxis(0.50f * 2.0f * pi<float>(), direction));
				channel.spherical.values.push_back(angleAxis(0.75f * 2.0f * pi<float>(), direction));
				channel.spherical.values.push_back(angleAxis(1.00f * 2.0f * pi<float>(), direction));

				channel.timestamps.push_back(0.00f * time_for_rotation);
				channel.timestamps.push_back(0.25f * time_for_rotation);
				channel.timestamps.push_back(0.50f * time_for_rotation);
				channel.timestamps.push_back(0.75f * time_for_rotation);
				channel.timestamps.push_back(1.00f * time_for_rotation);

				track.channels.push_back(move(channel));
			}
			else if (animation.HasMember("animationData"))
			{
				auto *data_path = animation["animationData"].GetString();
				load_animation(Path::relpath(path, data_path), track);
			}

			track.update_length();

			auto ident = to_string(index);
			AnimationID animation_id = 0;

			if (!track.channels.empty())
				animation_id = animation_system->register_animation(ident, track);

			bool per_instance = false;
			if (animation.HasMember("perInstance"))
				per_instance = animation["perInstance"].GetBool();

			auto &targets = animation["targetNodes"];
			for (auto target_itr = targets.Begin(); target_itr != targets.End(); ++target_itr)
			{
				auto &root = hierarchy[target_itr->GetUint()];

				if (root->get_children().empty() || !per_instance)
				{
					auto state_id = animation_system->start_animation(*root, animation_id, 0.0);
					animation_system->set_repeating(state_id, true);
				}
				else
				{
					for (auto &channel : track.channels)
						if (channel.type == SceneFormats::AnimationChannel::Type::Translation)
							throw logic_error("Cannot use per-instance translation.");

					for (auto &child : root->get_children())
					{
						auto state_id = animation_system->start_animation(*child, animation_id, 0.0);
						animation_system->set_repeating(state_id, true);
					}
				}
			}
		}
	}

	auto hier_itr = begin(hierarchy);
	for (auto itr = nodes.Begin(); itr != nodes.End(); ++itr, ++hier_itr)
	{
		auto &elem = *itr;
		if (elem.HasMember("children"))
		{
			auto &children = elem["children"];
			for (auto child_itr = children.Begin(); child_itr != children.End(); ++child_itr)
			{
				uint32_t index = child_itr->GetUint();
				(*hier_itr)->add_child(hierarchy[index]);
			}
		}
	}

	auto root = scene->create_node();
	for (auto &node : hierarchy)
		if (!node->get_parent())
			root->add_child(node);

	if (doc.HasMember("background"))
	{
		auto &bg = doc["background"];

		Entity *entity = nullptr;

		if (bg.HasMember("skybox"))
		{
			auto &box = bg["skybox"];
			auto texture_path = Path::relpath(path, box["path"].GetString());

			Util::IntrusivePtr<Skybox> skybox;
			AbstractRenderableHandle renderable;
			bool use_ibl = false;

			if (box.HasMember("projection"))
			{
				auto &proj = box["projection"];
				if (strcmp(proj.GetString(), "latlon") == 0)
				{
					skybox = Util::make_handle<Skybox>(texture_path, true);
					renderable = skybox;
					use_ibl = true;
				}
				else if (strcmp(proj.GetString(), "cube") == 0)
				{
					skybox = Util::make_handle<Skybox>(texture_path, false);
					renderable = skybox;
					use_ibl = true;
				}
				else if (strcmp(proj.GetString(), "cylinder") == 0)
				{
					auto cylinder = Util::make_handle<SkyCylinder>(texture_path);
					renderable = cylinder;
					cylinder->set_xz_scale(box["cylinderScale"].GetFloat());
				}
				else
					throw logic_error("Unsupported skybox projection.");
			}
			else
				throw logic_error("Skybox projection must be specified.");

			string reflection;
			string irradiance;

			if (box.HasMember("reflection"))
				reflection = Path::relpath(path, box["reflection"].GetString());
			if (box.HasMember("irradiance"))
				irradiance = Path::relpath(path, box["irradiance"].GetString());

			entity = scene->create_renderable(renderable, nullptr);
			entity->allocate_component<BackgroundComponent>();

			if (use_ibl || (!reflection.empty() && !irradiance.empty()))
			{
				if (skybox)
					entity->allocate_component<SkyboxComponent>()->skybox = skybox.get();

				if (!reflection.empty() && !irradiance.empty())
				{
					auto *ibl = entity->allocate_component<IBLComponent>();
					ibl->irradiance_path = irradiance;
					ibl->reflection_path = reflection;
					ibl->intensity = 1.0f;
				}
			}
		}
		else
			entity = scene->create_entity();

		if (bg.HasMember("fog"))
		{
			auto &fog = bg["fog"];
			auto &color = fog["color"];

			FogParameters params = {};
			params.color = vec3(color[0].GetFloat(), color[1].GetFloat(), color[2].GetFloat());
			params.falloff = fog["falloff"].GetFloat();
			auto *environment = entity->allocate_component<EnvironmentComponent>();
			environment->fog = params;
		}
	}

	const auto read_transform = [](Transform &transform, const Value &value) {
		if (value.HasMember("scale"))
		{
			auto &s = value["scale"];
			transform.scale = vec3(s[0].GetFloat(), s[1].GetFloat(), s[2].GetFloat());
		}

		if (value.HasMember("translation"))
		{
			auto &t = value["translation"];
			transform.translation = vec3(t[0].GetFloat(), t[1].GetFloat(), t[2].GetFloat());
		}

		if (value.HasMember("rotation"))
		{
			auto &r = value["rotation"];
			transform.rotation = normalize(quat(r[3].GetFloat(), r[0].GetFloat(), r[1].GetFloat(), r[2].GetFloat()));
		}
	};

	if (doc.HasMember("terrain"))
	{
		auto &terrain = doc["terrain"];

		Ground::TerrainInfo info;
		info.heightmap = Path::relpath(path, terrain["heightmap"].GetString());
		info.normalmap = Path::relpath(path, terrain["normalmap"].GetString());
		info.occlusionmap = Path::relpath(path, terrain["occlusionmap"].GetString());
		info.base_color = Path::relpath(path, terrain["baseColorTexture"].GetString());
		info.normalmap_fine = Path::relpath(path, terrain["normalTexture"].GetString());
		info.splatmap = Path::relpath(path, terrain["splatmapTexture"].GetString());

		if (terrain.HasMember("bandlimitedPixel"))
			info.bandlimited_pixel = terrain["bandlimitedPixel"].GetBool();

		float tiling_factor = 1.0f;
		if (terrain.HasMember("tilingFactor"))
			tiling_factor = terrain["tilingFactor"].GetFloat();

		if (terrain.HasMember("lodBias"))
			info.lod_bias = terrain["lodBias"].GetFloat();

		if (terrain.HasMember("patchData"))
		{
			auto patch_path = Path::relpath(path, terrain["patchData"].GetString());
			string patch_json;
			if (Global::filesystem()->read_file_to_string(patch_path, patch_json))
			{
				Document patch_doc;
				patch_doc.Parse(patch_json);
				auto &bias = patch_doc["bias"];
				for (auto itr = bias.Begin(); itr != bias.End(); ++itr)
					info.patch_lod_bias.push_back(itr->GetFloat());
				auto &range = patch_doc["range"];
				for (auto itr = range.Begin(); itr != range.End(); ++itr)
					info.patch_range.push_back(vec2((*itr)[0].GetFloat(), (*itr)[1].GetFloat()));
			}
			else
				LOGE("Failed to read patch data from %s\n", patch_path.c_str());
		}

		unsigned size = 1024;
		if (terrain.HasMember("size"))
			size = terrain["size"].GetUint();

		info.normal_size = 1024;
		if (terrain.HasMember("normalSize"))
			info.normal_size = terrain["normalSize"].GetUint();

		auto handles = Ground::add_to_scene(*scene, size, tiling_factor, info);
		read_transform(handles.node->transform, terrain);
		root->add_child(handles.node);
	}

	if (doc.HasMember("planes"))
	{
		auto &planes = doc["planes"];
		for (auto itr = planes.Begin(); itr != planes.End(); ++itr)
		{
			auto &info = *itr;

			auto plane = Util::make_handle<TexturePlane>(
					Path::relpath(path, info["normalMap"].GetString()));

			auto entity = scene->create_renderable(plane, nullptr);

			vec3 center = vec3(info["center"][0].GetFloat(), info["center"][1].GetFloat(), info["center"][2].GetFloat());
			vec3 normal = vec3(info["normal"][0].GetFloat(), info["normal"][1].GetFloat(), info["normal"][2].GetFloat());
			vec3 up = vec3(info["up"][0].GetFloat(), info["up"][1].GetFloat(), info["up"][2].GetFloat());
			vec3 emissive = vec3(info["baseEmissive"][0].GetFloat(), info["baseEmissive"][1].GetFloat(), info["baseEmissive"][2].GetFloat());
			float rad_up = info["radiusUp"].GetFloat();
			float rad_x = info["radiusAcross"].GetFloat();
			float zfar = info["zFar"].GetFloat();

			plane->set_plane(center, normal, up, rad_up, rad_x);
			plane->set_zfar(zfar);

			if (info.HasMember("reflectionName"))
				plane->set_reflection_name(info["reflectionName"].GetString());
			if (info.HasMember("refractionName"))
				plane->set_refraction_name(info["refractionName"].GetString());

			plane->set_resolution_scale(info["resolutionScale"][0].GetFloat(), info["resolutionScale"][1].GetFloat());

			plane->set_base_emissive(emissive);
			entity->free_component<UnboundedComponent>();
			entity->allocate_component<RenderPassSinkComponent>();
			auto *cull_plane = entity->allocate_component<CullPlaneComponent>();
			cull_plane->plane = plane->get_plane();

			auto *rpass = entity->allocate_component<RenderPassComponent>();
			rpass->creator = plane.get();
		}
	}

	return root;
}

}
