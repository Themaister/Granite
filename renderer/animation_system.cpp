#include "animation_system.hpp"

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

		for (auto &channel : animation->animation.channels)
		{
			auto &node = *nodes[channel.node_index];
			switch (channel.type)
			{
			case Importer::AnimationChannel::Type::Translation:
				node.transform.translation = channel.linear.sample(index, phase);
				break;
			case Importer::AnimationChannel::Type::Scale:
				node.transform.scale = channel.linear.sample(index, phase);
				break;
			case Importer::AnimationChannel::Type::Rotation:
				node.transform.rotation = channel.spherical.sample(index, phase);
				break;
			}
		}
	}
}

void AnimationSystem::register_animation(const std::string &name, const Importer::Animation &animation)
{
	animation_map[name] = animation;
}

void AnimationSystem::start_animation(const std::string &name, double start_time, bool repeat)
{
	animations.emplace_back(new AnimationState(animation_map[name], start_time, repeat));
}

}