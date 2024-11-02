/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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
#include "task_composer.hpp"

namespace Granite
{
template <typename T, typename Op>
static void resample_channel(T *resampled, size_t count, const SceneFormats::AnimationChannel &channel, const Op &op, float inv_frame_rate)
{
	for (size_t i = 0; i < count; i++)
	{
		float t = float(i) * inv_frame_rate;
		unsigned index;
		float phase;
		float dt;
		channel.get_index_phase(t, index, phase, dt);
		resampled[i] = op(index, phase, dt);
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
		multi_node_indices.push_back(node_index);
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

void AnimationUnrolled::animate(Transform *transforms, const uint32_t *transform_indices, unsigned num_transforms, float offset_time) const
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
		auto *t = &transforms[transform_indices[i]];
		animate_single(*t, i, lo, hi, l);
	}
}

void AnimationUnrolled::animate_single(Transform &t, unsigned channel, int lo, int hi, float l) const
{
	// The animations should be resampled at such a high rate in runtime (e.g. 60 fps)
	// that doing slerp for rotation is irrelevant.
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

	num_samples = unsigned(muglm::floor(total_length * key_frame_rate)) + 1;
	length = total_length;

	skinning = animation.skinning;
	skin_compat = animation.skin_compat;

	for (auto &c : animation.channels)
	{
		unsigned index;
		if (skinning)
		{
			if (!c.joint)
				throw std::logic_error("Skinned animation must target joints.");
			index = c.joint_index;
		}
		else
		{
			if (c.joint)
				throw std::logic_error("Non-skinned animation cannot target joints.");
			index = find_or_allocate_index(c.node_index);
		}

		reserve_num_clips(index + 1);

		switch (c.type)
		{
		case SceneFormats::AnimationChannel::Type::CubicScale:
			key_frames_scale[index].resize(num_samples);
			resample_channel(key_frames_scale[index].data(), num_samples, c,
			                 [&c](unsigned i, float t, float dt) {
				                 return c.positional.sample_spline(i, t, dt);
			                 }, inv_frame_rate);
			channel_mask[index] |= SCALE_BIT;
			break;

		case SceneFormats::AnimationChannel::Type::Scale:
			key_frames_scale[index].resize(num_samples);
			resample_channel(key_frames_scale[index].data(), num_samples, c,
			                 [&c](unsigned i, float t, float) {
				                 return c.positional.sample(i, t);
			                 }, inv_frame_rate);
			channel_mask[index] |= SCALE_BIT;
			break;

		case SceneFormats::AnimationChannel::Type::CubicTranslation:
			key_frames_translation[index].resize(num_samples);
			resample_channel(key_frames_translation[index].data(), num_samples, c,
			                 [&c](unsigned i, float t, float dt) {
				                 return c.positional.sample_spline(i, t, dt);
			                 }, inv_frame_rate);
			channel_mask[index] |= TRANSLATION_BIT;
			break;

		case SceneFormats::AnimationChannel::Type::Translation:
			key_frames_translation[index].resize(num_samples);
			resample_channel(key_frames_translation[index].data(), num_samples, c,
			                 [&c](unsigned i, float t, float) {
				                 return c.positional.sample(i, t);
			                 }, inv_frame_rate);
			channel_mask[index] |= TRANSLATION_BIT;
			break;

		case SceneFormats::AnimationChannel::Type::CubicRotation:
			key_frames_rotation[index].resize(num_samples);
			resample_channel(key_frames_rotation[index].data(), num_samples, c,
			                 [&c](unsigned i, float t, float dt) {
				                 return c.spherical.sample_spline(i, t, dt);
			                 }, inv_frame_rate);
			channel_mask[index] |= ROTATION_BIT;
			break;

		case SceneFormats::AnimationChannel::Type::Squad:
			key_frames_rotation[index].resize(num_samples);
			resample_channel(key_frames_rotation[index].data(), num_samples, c,
			                 [&c](unsigned i, float t, float) {
				                 return c.spherical.sample_squad(i, t);
			                 }, inv_frame_rate);
			channel_mask[index] |= ROTATION_BIT;
			break;

		case SceneFormats::AnimationChannel::Type::Rotation:
			key_frames_rotation[index].resize(num_samples);
			resample_channel(key_frames_rotation[index].data(), num_samples, c,
			                 [&c](unsigned i, float t, float) {
				                 return c.spherical.sample(i, t);
			                 }, inv_frame_rate);
			channel_mask[index] |= ROTATION_BIT;
			break;
		}
	}
}

AnimationID AnimationSystem::get_animation_id_from_name(const std::string &name) const
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

	id = animation_pool.emplace(std::move(animation));
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

AnimationStateID AnimationSystem::start_animation(Node &node, Granite::AnimationID animation_id,
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

		Util::SmallVector<uint32_t> target_transforms = { node.transform.offset };
		Util::SmallVector<Node *> nodes = { &node };
		id = animation_state_pool.emplace(*animation,
		                                  node.get_transform_base(),
		                                  std::move(target_transforms), std::move(nodes), start_time);
	}

	auto *state = &animation_state_pool.get(id);
	state->id = id;
	active_animation.add(state);
	return id;
}

void AnimationSystem::set_fixed_pose(Node &node, Granite::AnimationID id, float offset) const
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

		animation->animate(node.get_transform_base(),
		                   node.get_skin()->skin.data(), node.get_skin()->skin.size(), offset);

		node.invalidate_cached_transform();
	}
	else
	{
		if (animation->get_num_channels() != 1)
		{
			LOGE("Animation has more than one channel of animation.\n");
			return;
		}

		uint32_t transform_index = node.transform.offset;
		animation->animate(node.get_transform_base(), &transform_index, 1, offset);
		node.invalidate_cached_transform();
	}
}

void AnimationSystem::set_fixed_pose_multi(NodeHandle *nodes, unsigned num_nodes, AnimationID id,
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

	if (!num_nodes)
		return;

	// Not very efficient.
	Util::SmallVector<uint32_t> target_transforms;
	target_transforms.reserve(animation->get_num_channels());

	for (unsigned channel = 0; channel < animation->get_num_channels(); channel++)
	{
		unsigned index = animation->get_multi_node_index(channel);
		if (index >= num_nodes)
		{
			LOGE("Node index %u is out of range of provided nodes (%u).\n", index, num_nodes);
			return;
		}

		target_transforms.push_back(nodes[index]->transform.offset);
		nodes[index]->invalidate_cached_transform();
	}

	animation->animate(nodes[0]->get_transform_base(),
	                   target_transforms.data(), target_transforms.size(), offset);
}

AnimationStateID AnimationSystem::start_animation_multi(NodeHandle *nodes, unsigned num_nodes,
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

	if (!num_nodes)
	{
		LOGE("Number of nodes must not be 0.\n");
		return 0;
	}

	Util::SmallVector<uint32_t> target_transforms;
	Util::SmallVector<Node *> target_nodes;
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

		target_transforms.push_back(nodes[index]->transform.offset);
		target_nodes.push_back(nodes[index].get());
	}

	auto id = animation_state_pool.emplace(*animation, target_nodes.front()->get_transform_base(),
	                                       std::move(target_transforms), std::move(target_nodes), start_time);
	auto *state = &animation_state_pool.get(id);
	state->id = id;
	active_animation.add(state);
	return id;
}

void AnimationSystem::set_completion_callback(AnimationStateID id, std::function<void()> cb)
{
	auto *state = animation_state_pool.maybe_get(id);
	if (state)
		state->cb = std::move(cb);
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

void AnimationSystem::update(AnimationState *anim, double frame_time, double elapsed_time)
{
	bool complete = false;

	double offset;
	if (anim->relative_timing)
	{
		anim->start_time += frame_time;
		offset = anim->start_time;
	}
	else
	{
		offset = elapsed_time - anim->start_time;
	}

	if (!anim->repeating && offset >= anim->animation.get_length())
		complete = true;

	if (anim->repeating)
		offset = mod(offset, double(anim->animation.get_length()));

	if (anim->animation.is_skinned())
	{
		auto *node = anim->skinned_node;
		anim->animation.animate(anim->transforms_base,
		                        node->get_skin()->skin.data(), node->get_skin()->skin.size(), float(offset));
		node->invalidate_cached_transform();
	}
	else
	{
		anim->animation.animate(anim->transforms_base,
		                        anim->channel_transforms.data(), anim->channel_transforms.size(), float(offset));
		for (auto *node : anim->channel_nodes)
			node->invalidate_cached_transform();
	}

	if (complete)
		garbage_collect_animations.push(anim);
}

void AnimationSystem::garbage_collect()
{
	// Cleanup task.
	garbage_collect_animations.for_each_ranged([&](AnimationState * const *states, size_t count) {
		for (size_t i = 0; i < count; i++)
		{
			auto *state = states[i];
			if (state->cb)
				state->cb();
			active_animation.erase(state);
			animation_state_pool.remove(state->id);
		}
	});
	garbage_collect_animations.clear();
}

void AnimationSystem::animate(double frame_time, double elapsed_time)
{
	// TODO: Run multithreaded.
	for (auto *anim : active_animation)
		update(anim, frame_time, elapsed_time);

	garbage_collect();
}

void AnimationSystem::animate(TaskComposer &composer, double frame_time, double elapsed_time)
{
	auto &group = composer.begin_pipeline_stage();
	group.set_desc("animation-update");
	size_t count = active_animation.size();
	constexpr size_t per_batch = 32;
	for (size_t i = 0; i < count; i += per_batch)
	{
		group.enqueue_task([=]() {
			auto itr = active_animation.begin() + i;
			auto end_itr = itr + std::min(per_batch, count - i);
			while (itr != end_itr)
			{
				update(*itr, frame_time, elapsed_time);
				++itr;
			}
		});
	}

	auto &cleanup = composer.begin_pipeline_stage();
	cleanup.set_desc("animation-cleanup");
	cleanup.enqueue_task([this]() {
		garbage_collect();
	});
}

AnimationSystem::AnimationState::AnimationState(const AnimationUnrolled &anim,
                                                Transform *transforms_base_,
                                                Util::SmallVector<uint32_t> channel_transforms_,
                                                Util::SmallVector<Node *> channel_nodes_,
                                                double start_time_)
	: transforms_base(transforms_base_),
	  channel_transforms(std::move(channel_transforms_)),
	  channel_nodes(std::move(channel_nodes_)),
	  animation(anim),
	  start_time(start_time_)
{
}

AnimationSystem::AnimationState::AnimationState(const Granite::AnimationUnrolled &anim, Node *node,
                                                double start_time_)
	: transforms_base(node->get_transform_base()),
	  skinned_node(node), animation(anim), start_time(start_time_)
{
}
}