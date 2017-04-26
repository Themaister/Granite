#pragma once

#include "scene.hpp"
#include "gltf.hpp"
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

private:
	struct SubsceneData
	{
		std::unique_ptr<GLTF::Parser> parser;
		std::vector<AbstractRenderableHandle> meshes;
	};
	std::unordered_map<std::string, SubsceneData> subscenes;

	std::unique_ptr<Scene> scene;
	void parse(const std::string &path, const std::string &json);

	Scene::NodeHandle build_tree_for_subscene(const SubsceneData &subscene);
};
}