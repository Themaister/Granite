#include "animation_system.hpp"

using namespace std;

namespace Granite
{
void AnimationSystem::animate(double t)
{
	for (auto &animation : animations)
	{
		double wrapped_time = fmod(t - animation->start_time, animation->animation.get_length());

		unsigned index;
		float phase;
		animation->animation.get_index_phase(float(wrapped_time), index, phase);

		auto target = begin(animation->channel_targets);
		for (auto &channel : animation->animation.channels)
		{
			auto &node = **target;
			switch (channel.type)
			{
			case Importer::AnimationChannel::Type::Translation:
				node.translation = channel.linear.sample(index, phase);
				break;
			case Importer::AnimationChannel::Type::Scale:
				node.scale = channel.linear.sample(index, phase);
				break;
			case Importer::AnimationChannel::Type::Rotation:
				node.rotation = channel.spherical.sample(index, phase);
				break;
			}
			++target;
		}
	}
}

void AnimationSystem::register_animation(const std::string &name, const Importer::Animation &animation)
{
	animation_map[name] = animation;
}

void AnimationSystem::start_animation(Scene::Node &node, const std::string &name, double start_time, bool repeat)
{
	std::vector<Transform *> target_nodes;
	auto &animation = animation_map[name];
	target_nodes.reserve(animation.channels.size());

	for (auto &channel : animation.channels)
	{
		if (channel.joint)
		{
			if (node.get_skin().skin.empty())
				throw logic_error("Node does not have a skin.");
			if (node.get_skin().skin_compat != animation.skin_compat)
				throw logic_error("Nodes skin is not compatible with animation skin index.");

			target_nodes.push_back(node.get_skin().skin[channel.joint_index]);
		}
		else
			target_nodes.push_back(&node.transform);
	}

	animations.emplace_back(new AnimationState(move(target_nodes), animation, start_time, repeat));
}

void AnimationSystem::start_animation(const std::string &name, double start_time, bool repeat)
{
	std::vector<Transform *> target_nodes;
	auto &animation = animation_map[name];
	target_nodes.reserve(animation.channels.size());

	if (animation.skinning)
		throw logic_error("Cannot start skinning animations without a target base node.");

	for (auto &channel : animation.channels)
	{
		if (channel.joint)
			throw logic_error("Cannot start skinning animations without a target base node.");
		else
			target_nodes.push_back(&nodes[channel.node_index]->transform);
	}

	animations.emplace_back(new AnimationState(move(target_nodes), animation, start_time, repeat));
}

}