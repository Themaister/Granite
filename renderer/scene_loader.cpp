#include "scene_loader.hpp"
#include "gltf.hpp"
#include "importers.hpp"
#define RAPIDJSON_ASSERT(x) do { if (!(x)) throw "JSON error"; } while(0)
#include "rapidjson/document.h"
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
	scene.reset(new Scene);
}

unique_ptr<AnimationSystem> SceneLoader::consume_animation_system()
{
	return move(animation_system);
}

void SceneLoader::load_scene(const std::string &path)
{
	animation_system.reset(new AnimationSystem);
	string json;
	if (!Filesystem::get().read_file_to_string(path, json))
		throw runtime_error("Failed to load GLTF file.");
	parse(path, json);
}

Scene::NodeHandle SceneLoader::build_tree_for_subscene(const SubsceneData &subscene)
{
	auto &parser = *subscene.parser;
	std::vector<Scene::NodeHandle> nodes;
	nodes.reserve(parser.get_nodes().size());

	for (auto &node : parser.get_nodes())
	{
		if (!node.joint)
		{
			Scene::NodeHandle nodeptr;
			if (node.has_skin)
				nodeptr = scene->create_skinned_node(parser.get_skins()[node.skin]);
			else
				nodeptr = scene->create_node();

			nodes.push_back(nodeptr);
			nodeptr->transform.translation = node.transform.translation;
			nodeptr->transform.rotation = node.transform.rotation;
			nodeptr->transform.scale = node.transform.scale;
		}
		else
			nodes.push_back({});
	}

	unsigned i = 0;
	for (auto &node : parser.get_nodes())
	{
		if (nodes[i])
		{
			for (auto &child : node.children)
				nodes[i]->add_child(nodes[child]);
			for (auto &mesh : node.meshes)
				scene->create_renderable(subscene.meshes[mesh], nodes[i].get());
		}
		i++;
	}

	auto root = scene->create_node();
	for (auto &node : nodes)
		if (node && !node->get_parent())
			root->add_child(node);

	return root;
}

void SceneLoader::load_animation(const std::string &path, Importer::Animation &animation)
{
	string str;
	if (!Filesystem::get().read_file_to_string(path, str))
	{
		LOGE("Failed to load file: %s\n", path.c_str());
		return;
	}

	Document doc;
	doc.Parse(str);
	if (doc.HasParseError())
		throw logic_error("Failed to parse.");

	auto &timestamps = doc["timestamps"];
	animation.timestamps.clear();
	for (auto itr = timestamps.Begin(); itr != timestamps.End(); ++itr)
		animation.timestamps.push_back(itr->GetFloat());

	Importer::AnimationChannel channel;

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

		channel.type = Importer::AnimationChannel::Type::Rotation;
		channel.spherical = move(slerp);
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

		channel.type = Importer::AnimationChannel::Type::Translation;
		channel.linear = move(linear);
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

		channel.type = Importer::AnimationChannel::Type::Scale;
		channel.linear = move(linear);
		animation.channels.push_back(move(channel));
	}
}

void SceneLoader::parse(const std::string &path, const std::string &json)
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
			Importer::MaterialInfo default_material;
			default_material.uniform_base_color = vec4(0.0f, 1.0f, 0.0f, 1.0f);
			AbstractRenderableHandle renderable;

			bool skinned = mesh.attribute_layout[ecast(MeshAttribute::BoneIndex)].format != VK_FORMAT_UNDEFINED;
			if (skinned)
			{
				if (mesh.has_material)
					renderable = Util::make_abstract_handle<AbstractRenderable, ImportedSkinnedMesh>(mesh,
					                                                                                 parser.get_materials()[mesh.material_index]);
				else
					renderable = Util::make_abstract_handle<AbstractRenderable, ImportedSkinnedMesh>(mesh, default_material);
			}
			else
			{
				if (mesh.has_material)
					renderable = Util::make_abstract_handle<AbstractRenderable, ImportedMesh>(mesh,
					                                                                          parser.get_materials()[mesh.material_index]);
				else
					renderable = Util::make_abstract_handle<AbstractRenderable, ImportedMesh>(mesh, default_material);
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
			Importer::Animation track;

			if (animation.HasMember("axisAngle"))
			{
				auto &rotation = animation["axisAngle"];
				float x = rotation[0].GetFloat();
				float y = rotation[1].GetFloat();
				float z = rotation[2].GetFloat();
				vec3 direction = normalize(vec3(x, y, z));
				float angular_freq = rotation[3].GetFloat();
				float time_for_rotation = 2.0f * pi<float>() / angular_freq;

				track.timestamps.push_back(0.00f * time_for_rotation);
				track.timestamps.push_back(0.25f * time_for_rotation);
				track.timestamps.push_back(0.50f * time_for_rotation);
				track.timestamps.push_back(0.75f * time_for_rotation);
				track.timestamps.push_back(1.00f * time_for_rotation);

				Importer::AnimationChannel channel;
				channel.type = Importer::AnimationChannel::Type::Rotation;
				channel.spherical.values.push_back(angleAxis(0.00f * 2.0f * pi<float>(), direction));
				channel.spherical.values.push_back(angleAxis(0.25f * 2.0f * pi<float>(), direction));
				channel.spherical.values.push_back(angleAxis(0.50f * 2.0f * pi<float>(), direction));
				channel.spherical.values.push_back(angleAxis(0.75f * 2.0f * pi<float>(), direction));
				channel.spherical.values.push_back(angleAxis(1.00f * 2.0f * pi<float>(), direction));
				track.channels.push_back(move(channel));
			}
			else if (animation.HasMember("animationData"))
			{
				auto *data_path = animation["animationData"].GetString();
				load_animation(Path::relpath(path, data_path), track);
			}

			auto ident = to_string(index);

			if (!track.channels.empty())
				animation_system->register_animation(ident, track);

			bool per_instance = false;
			if (animation.HasMember("perInstance"))
				per_instance = animation["perInstance"].GetBool();

			auto &targets = animation["targetNodes"];
			for (auto itr = targets.Begin(); itr != targets.End(); ++itr)
			{
				auto index = itr->GetUint();
				auto &root = hierarchy[index];

				if (root->get_children().empty() || !per_instance)
					animation_system->start_animation(*root, ident, 0.0, true);
				else
				{
					for (auto &channel : track.channels)
						if (channel.type == Importer::AnimationChannel::Type::Translation)
							throw logic_error("Cannot use per-instance translation.");

					for (auto &child : root->get_children())
						animation_system->start_animation(*child, ident, 0.0, true);
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
			for (auto itr = children.Begin(); itr != children.End(); ++itr)
			{
				uint32_t index = itr->GetUint();
				(*hier_itr)->add_child(hierarchy[index]);
			}
		}
	}

	auto root = scene->create_node();
	for (auto &node : hierarchy)
		if (!node->get_parent())
			root->add_child(node);

	scene->set_root_node(root);

	if (doc.HasMember("background"))
	{
		auto texture_path = Path::relpath(path, doc["background"].GetString());
		auto skybox = Util::make_abstract_handle<AbstractRenderable, Skybox>(texture_path);
		scene->create_renderable(skybox, nullptr);
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
		info.base_color = Path::relpath(path, terrain["baseColorTexture"].GetString());
		//info.normalmap_fine = Path::relpath(path, terrain["normalTexture"].GetString());
		info.splatmap = Path::relpath(path, terrain["splatmapTexture"].GetString());

		float tiling_factor = 1.0f;
		if (terrain.HasMember("tilingFactor"))
			tiling_factor = terrain["tilingFactor"].GetFloat();

		if (terrain.HasMember("lodBias"))
			info.lod_bias = terrain["lodBias"].GetFloat();

		if (terrain.HasMember("patchData"))
		{
			auto patch_path = Path::relpath(path, terrain["patchData"].GetString());
			string json;
			if (Filesystem::get().read_file_to_string(patch_path, json))
			{
				Document patch_doc;
				patch_doc.Parse(json);
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

		auto handles = Ground::add_to_scene(*scene, size, tiling_factor, info);
		read_transform(handles.node->transform, terrain);
		root->add_child(handles.node);
	}
}

}