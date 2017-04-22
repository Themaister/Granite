#pragma once

#include "scene.hpp"
#include "importers.hpp"
#include <vector>

namespace Granite
{
class AnimationSystem
{
public:
	void animate(double t);
	void start_animation(const std::string &name, double start_time, bool repeat);
	void register_animation(const std::string &name, const Importer::Animation &animation);

	void add_node(Scene::NodeHandle node)
	{
		nodes.push_back(node);
	}

private:
	std::vector<Scene::NodeHandle> nodes;
	std::unordered_map<std::string, Importer::Animation> animation_map;

	struct AnimationState
	{
		AnimationState(const Importer::Animation &anim, double start_time, bool repeating)
			: animation(anim), start_time(start_time), repeating(repeating)
		{
		}
		const Importer::Animation &animation;
		double start_time = 0.0;
		bool repeating = false;
	};

	std::vector<std::unique_ptr<AnimationState>> animations;
};
}