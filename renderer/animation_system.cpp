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
			auto *transform = target->first;
			auto *node = target->second;
			node->invalidate_cached_transform();

			switch (channel.type)
			{
			case Importer::AnimationChannel::Type::Translation:
				transform->translation = channel.linear.sample(index, phase);
				break;
			case Importer::AnimationChannel::Type::Scale:
				transform->scale = channel.linear.sample(index, phase);
				break;
			case Importer::AnimationChannel::Type::Rotation:
				transform->rotation = channel.spherical.sample(index, phase);
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
	std::vector<std::pair<Transform *, Scene::Node *>> target_nodes;
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

			target_nodes.push_back({ node.get_skin().skin[channel.joint_index], &node });
		}
		else
			target_nodes.push_back({ &node.transform, &node });
	}

	animations.emplace_back(new AnimationState(move(target_nodes), animation, start_time, repeat));
}

void AnimationSystem::start_animation(const std::string &name, double start_time, bool repeat)
{
	std::vector<std::pair<Transform *, Scene::Node *>> target_nodes;
	auto &animation = animation_map[name];
	target_nodes.reserve(animation.channels.size());

	if (animation.skinning)
		throw logic_error("Cannot start skinning animations without a target base node.");

	for (auto &channel : animation.channels)
	{
		if (channel.joint)
			throw logic_error("Cannot start skinning animations without a target base node.");
		else
			target_nodes.push_back({ &nodes[channel.node_index]->transform, nodes[channel.node_index].get() });
	}

	animations.emplace_back(new AnimationState(move(target_nodes), animation, start_time, repeat));
}

}