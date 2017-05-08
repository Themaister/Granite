#pragma once

#include "scene.hpp"
#include "gltf.hpp"
#include "animation_system.hpp"
#include <memory>
#include <string>

namespace Granite
{
class SceneLoader
{
public:
	SceneLoader();
	void load_scene(const std::string &path);

	Scene &get_scene()
	{
		return *scene;
	}

	std::unique_ptr<AnimationSystem> consume_animation_system();

private:
	struct SubsceneData
	{
		std::unique_ptr<GLTF::Parser> parser;
		std::vector<AbstractRenderableHandle> meshes;
	};
	std::unordered_map<std::string, SubsceneData> subscenes;

	std::unique_ptr<Scene> scene;
	std::unique_ptr<AnimationSystem> animation_system;
	void parse(const std::string &path, const std::string &json);

	Scene::NodeHandle build_tree_for_subscene(const SubsceneData &subscene);
	void load_animation(const std::string &path, Importer::Animation &animation);
};
}