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

#include "animation_system.hpp"

using namespace std;

namespace Granite
{
template <typename T, typename Sampler>
static void resample_channel(T *resampled, size_t count, const SceneFormats::AnimationChannel &channel, const Sampler &sampler, float inv_frame_rate)
{
	for (size_t i = 0; i < count; i++)
	{
		float t = float(i) * inv_frame_rate;
		unsigned index;
		float phase;
		float dt;
		channel.get_index_phase(t, index, phase, dt);
		resampled[i] = sampler.sample(index, phase, dt);
	}
}

unsigned AnimationUnrolled::find_or_allocate_index(uint32_t node_index)
{
	auto itr = find(begin(multi_node_indices), end(multi_node_indices), node_index);
	if (itr != end(multi_node_indices))
		return unsigned(itr - begin(multi_node_indices));
	else
	{
		auto index = unsigned(multi_node_indices.size());
		multi_node_indices.push_back(index);
		return index;
	}
}

void AnimationUnrolled::reserve_num_clips(unsigned count)
{
	if (count > key_frames_rotation.size())
	{
		key_frames_rotation.resize(count);
		key_frames_translation.resize(count);
		key_frames_scale.resize(count);
		multi_node_indices.resize(count);
		multi_node_indices.resize(count);
		channel_mask.resize(count);
	}
}

unsigned AnimationUnrolled::get_num_channels() const
{
	return channel_mask.size();
}

Util::Hash AnimationUnrolled::get_skin_compat() const
{
	return skin_compat;
}

float AnimationUnrolled::get_length() const
{
	return length;
}

bool AnimationUnrolled::is_skinned() const
{
	return skinning;
}

uint32_t AnimationUnrolled::get_multi_node_index(unsigned channel) const
{
	return multi_node_indices[channel];
}

void AnimationUnrolled::animate(Transform *const *transforms, unsigned num_transforms, float offset_time) const
{
	if (num_transforms != get_num_channels())
		throw std::logic_error("Incorrect number of transforms.");

	float sample = offset_time * frame_rate;
	float low_sample = muglm::floor(sample);
	int lo = clamp(int(low_sample), 0, int(num_samples) - 1);
	int hi = muglm::min(lo + 1, int(num_samples) - 1);
	float l = sample - low_sample;

	for (unsigned i = 0; i < num_transforms; i++)
	{
		auto *t = transforms[i];
		animate_single(*t, i, lo, hi, l);
	}
}

void AnimationUnrolled::animate_single(Transform &t, unsigned channel, int lo, int hi, float l) const
{
	// The animations should be sampled at such a high rate that doing slerp for rotation is irrelevant.
	auto mask = channel_mask[channel];
	if (mask & ROTATION_BIT)
		t.rotation = normalize(quat(mix(key_frames_rotation[channel][lo].as_vec4(), key_frames_rotation[channel][hi].as_vec4(), l)));
	if (mask & TRANSLATION_BIT)
		t.translation = mix(key_frames_translation[channel][lo], key_frames_translation[channel][hi], l);
	if (mask & SCALE_BIT)
		t.scale = mix(key_frames_scale[channel][lo], key_frames_scale[channel][hi], l);
}

AnimationUnrolled::AnimationUnrolled(const SceneFormats::Animation &animation, float key_frame_rate)
{
	frame_rate = key_frame_rate;
	inv_frame_rate = 1.0f / key_frame_rate;
	size_t size = animation.channels.size();
	key_frames_rotation.reserve(size);
	key_frames_translation.reserve(size);
	key_frames_translation.reserve(size);
	multi_node_indices.reserve(size);
	channel_mask.resize(size);

	float total_length = 0.0f;
	for (auto &c : animation.channels)
		total_length = muglm::max(total_length, c.get_length());

	num_samples = unsigned(muglm::ceil(total_length * key_frame_rate));
	length = total_length;

	skinning = animation.skinning;
	skin_compat = animation.skin_compat;

	for (auto &c : animation.channels)
	{
		unsigned index;
		if (skinning)
		{
			if (!c.joint)
				throw logic_error("Skinned animation must target joints.");
			index = c.joint_index;
		}
		else
		{
			if (c.joint)
				throw logic_error("Non-skinned animation cannot target joints.");
			index = find_or_allocate_index(c.node_index);
		}

		reserve_num_clips(index + 1);

		switch (c.type)
		{
		case SceneFormats::AnimationChannel::Type::CubicScale:
			key_frames_scale[index].resize(num_samples);
			resample_channel(key_frames_scale[index].data(), num_samples, c, c.cubic, inv_frame_rate);
			channel_mask[index] |= SCALE_BIT;
			break;

		case SceneFormats::AnimationChannel::Type::Scale:
			key_frames_scale[index].resize(num_samples);
			resample_channel(key_frames_scale[index].data(), num_samples, c, c.linear, inv_frame_rate);
			channel_mask[index] |= SCALE_BIT;
			break;

		case SceneFormats::AnimationChannel::Type::CubicTranslation:
			key_frames_translation[index].resize(num_samples);
			resample_channel(key_frames_translation[index].data(), num_samples, c, c.cubic, inv_frame_rate);
			channel_mask[index] |= TRANSLATION_BIT;
			break;

		case SceneFormats::AnimationChannel::Type::Translation:
			key_frames_translation[index].resize(num_samples);
			resample_channel(key_frames_translation[index].data(), num_samples, c, c.linear, inv_frame_rate);
			channel_mask[index] |= TRANSLATION_BIT;
			break;

		case SceneFormats::AnimationChannel::Type::Rotation:
			key_frames_rotation[index].resize(num_samples);
			resample_channel(key_frames_rotation[index].data(), num_samples, c, c.spherical, inv_frame_rate);
			channel_mask[index] |= ROTATION_BIT;
			break;
		}
	}
}

AnimationID AnimationSystem::get_animation_id_from_name(const string &name) const
{
	Util::Hasher hasher;
	hasher.string(name);
	auto *entry = animation_map.find(hasher.get());
	return entry ? entry->get() : 0;
}

AnimationID AnimationSystem::register_animation(const std::string &name, AnimationUnrolled animation)
{
	auto id = get_animation_id_from_name(name);
	if (id != 0)
		return id;

	Util::Hasher hasher;
	hasher.string(name);

	id = animation_pool.emplace(move(animation));
	animation_map.emplace_replace(hasher.get(), id);
	return id;
}

bool AnimationSystem::animation_is_running(AnimationStateID id) const
{
	return animation_state_pool.maybe_get(id) != nullptr;
}

void AnimationSystem::stop_animation(AnimationStateID id)
{
	auto *state = animation_state_pool.maybe_get(id);
	if (!state)
		return;

	active_animation.erase(state);
	if (state->cb)
		state->cb();
	animation_state_pool.remove(id);
}

AnimationID AnimationSystem::register_animation(const std::string &name,
                                                const SceneFormats::Animation &animation, float key_frame_rate)
{
	return register_animation(name, AnimationUnrolled(animation, key_frame_rate));
}

AnimationStateID AnimationSystem::start_animation(Scene::Node &node, Granite::AnimationID animation_id,
                                                  double start_time)
{
	auto *animation = animation_pool.maybe_get(animation_id);
	if (!animation)
	{
		LOGE("Animation does not exist!\n");
		return 0;
	}

	AnimationStateID id;
	if (animation->is_skinned())
	{
		if (!node.get_skin() || node.get_skin()->skin.empty() ||
		    node.get_skin()->skin_compat != animation->get_skin_compat())
		{
			LOGE("Skin is not compatible with animation.\n");
			return 0;
		}

		id = animation_state_pool.emplace(*animation, &node, start_time);
	}
	else
	{
		if (animation->get_num_channels() != 1)
		{
			LOGE("Animation has more than one channel of animation.\n");
			return 0;
		}

		std::vector<Transform *> target_transforms = { &node.transform };
		std::vector<Scene::Node *> nodes = { &node };
		id = animation_state_pool.emplace(*animation, move(target_transforms), move(nodes), start_time);
	}

	auto *state = &animation_state_pool.get(id);
	state->id = id;
	active_animation.insert_front(state);
	return id;
}

void AnimationSystem::set_fixed_pose(Scene::Node &node, Granite::AnimationID id, float offset) const
{
	auto *animation = animation_pool.maybe_get(id);
	if (!animation)
	{
		LOGE("Animation does not exist!\n");
		return;
	}

	if (animation->is_skinned())
	{
		if (!node.get_skin() || node.get_skin()->skin.empty() ||
		    node.get_skin()->skin_compat != animation->get_skin_compat())
		{
			LOGE("Skin is not compatible with animation.\n");
			return;
		}

		animation->animate(node.get_skin()->skin.data(), node.get_skin()->skin.size(), offset);
		node.invalidate_cached_transform();
	}
	else
	{
		if (animation->get_num_channels() != 1)
		{
			LOGE("Animation has more than one channel of animation.\n");
			return;
		}

		Transform *t = &node.transform;
		animation->animate(&t, 1, offset);
		node.invalidate_cached_transform();
	}
}

void AnimationSystem::set_fixed_pose_multi(Scene::NodeHandle *nodes, unsigned num_nodes, AnimationID id,
                                           float offset) const
{
	auto *animation = animation_pool.maybe_get(id);
	if (!animation)
	{
		LOGE("Animation does not exist!\n");
		return;
	}

	if (animation->is_skinned())
	{
		LOGE("Cannot use start_animation_multi with skinned animations.\n");
		return;
	}

	// Not very efficient.
	std::vector<Transform *> target_transforms;
	std::vector<Scene::Node *> target_nodes;
	target_transforms.reserve(animation->get_num_channels());
	target_nodes.reserve(animation->get_num_channels());

	for (unsigned channel = 0; channel < animation->get_num_channels(); channel++)
	{
		unsigned index = animation->get_multi_node_index(channel);
		if (index >= num_nodes)
		{
			LOGE("Node index %u is out of range of provided nodes (%u).\n", index, num_nodes);
			return;
		}

		target_transforms.push_back(&nodes[index]->transform);
		nodes[index]->invalidate_cached_transform();
	}

	animation->animate(target_transforms.data(), target_transforms.size(), offset);
}

AnimationStateID AnimationSystem::start_animation_multi(Scene::NodeHandle *nodes, unsigned num_nodes,
                                                        AnimationID animation_id, double start_time)
{
	auto *animation = animation_pool.maybe_get(animation_id);
	if (!animation)
	{
		LOGE("Animation does not exist!\n");
		return 0;
	}

	if (animation->is_skinned())
	{
		LOGE("Cannot use start_animation_multi with skinned animations.\n");
		return 0;
	}

	std::vector<Transform *> target_transforms;
	std::vector<Scene::Node *> target_nodes;
	target_transforms.reserve(animation->get_num_channels());
	target_nodes.reserve(animation->get_num_channels());

	for (unsigned channel = 0; channel < animation->get_num_channels(); channel++)
	{
		unsigned index = animation->get_multi_node_index(channel);
		if (index >= num_nodes)
		{
			LOGE("Node index %u is out of range of provided nodes (%u).\n", index, num_nodes);
			return 0;
		}

		target_transforms.push_back(&nodes[index]->transform);
		target_nodes.push_back(nodes[index].get());
	}

	auto id = animation_state_pool.emplace(*animation, move(target_transforms), move(target_nodes), start_time);
	auto *state = &animation_state_pool.get(id);
	state->id = id;
	active_animation.insert_front(state);
	return id;
}

void AnimationSystem::set_completion_callback(AnimationStateID id, function<void()> cb)
{
	auto *state = animation_state_pool.maybe_get(id);
	if (state)
		state->cb = move(cb);
}

void AnimationSystem::set_repeating(Granite::AnimationStateID id, bool repeat)
{
	auto *state = animation_state_pool.maybe_get(id);
	if (state)
		state->repeating = repeat;
}

void AnimationSystem::set_relative_timing(Granite::AnimationStateID id, bool enable)
{
	auto *state = animation_state_pool.maybe_get(id);
	if (state)
		state->relative_timing = enable;
}

void AnimationSystem::animate(double frame_time, double elapsed_time)
{
	auto itr = active_animation.begin();
	while (itr != active_animation.end())
	{
		bool complete = false;

		float offset;
		if (itr->relative_timing)
		{
			itr->start_time += frame_time;
			offset = float(itr->start_time);
		}
		else
		{
			offset = float(elapsed_time - itr->start_time);
		}

		if (!itr->repeating && offset >= itr->animation.get_length())
			complete = true;

		if (itr->repeating)
			offset = mod(offset, itr->animation.get_length());

		if (itr->animation.is_skinned())
		{
			auto *node = itr->skinned_node;
			itr->animation.animate(node->get_skin()->skin.data(), node->get_skin()->skin.size(), offset);
			node->invalidate_cached_transform();
		}
		else
		{
			itr->animation.animate(itr->channel_transforms.data(), itr->channel_transforms.size(), offset);
			for (auto *node : itr->channel_nodes)
				node->invalidate_cached_transform();
		}

		if (complete)
		{
			auto *state = itr.get();
			itr = active_animation.erase(itr);
			if (state->cb)
				state->cb();
			animation_state_pool.remove(state->id);
		}
		else
			++itr;
	}
}

AnimationSystem::AnimationState::AnimationState(const AnimationUnrolled &anim,
                                                std::vector<Transform *> channel_transforms_,
                                                std::vector<Scene::Node *> channel_nodes_,
                                                double start_time_)
		: channel_transforms(std::move(channel_transforms_)),
		  channel_nodes(std::move(channel_nodes_)),
		  animation(anim),
		  start_time(start_time_)
{
}

AnimationSystem::AnimationState::AnimationState(const Granite::AnimationUnrolled &anim, Granite::Scene::Node *node,
                                                double start_time_)
		: skinned_node(node), animation(anim), start_time(start_time_)
{
}

}