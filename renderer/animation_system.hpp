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

#pragma once

#include "scene.hpp"
#include "scene_formats.hpp"
#include "generational_handle.hpp"
#include "intrusive_hash_map.hpp"
#include "intrusive_list.hpp"
#include <vector>

namespace Granite
{
class AnimationUnrolled : public Util::IntrusiveHashMapEnabled<AnimationUnrolled>
{
public:
	AnimationUnrolled(const SceneFormats::Animation &animation, float key_frame_rate);
	void animate(Transform * const *transforms, unsigned num_transforms, float offset_time) const;

	unsigned get_num_channels() const;

	bool is_skinned() const;
	uint32_t get_multi_node_index(unsigned channel) const;
	Util::Hash get_skin_compat() const;

	float get_length() const;

private:
	enum ChannelMask
	{
		ROTATION_BIT = 1 << 0,
		TRANSLATION_BIT = 1 << 1,
		SCALE_BIT = 1 << 2
	};

	std::vector<std::vector<quat>> key_frames_rotation;
	std::vector<std::vector<vec3>> key_frames_translation;
	std::vector<std::vector<vec3>> key_frames_scale;
	std::vector<uint8_t> channel_mask;

	std::vector<uint32_t> multi_node_indices;

	unsigned num_samples = 0;
	float frame_rate = 0.0f;
	float inv_frame_rate = 0.0f;
	float length = 0.0f;

	Util::Hash skin_compat = 0;
	bool skinning = false;

	void reserve_num_clips(unsigned count);
	unsigned find_or_allocate_index(uint32_t node_index);

	void animate_single(Transform &transform, unsigned channel, int lo, int hi, float l) const;
};

using AnimationID = Util::GenerationalHandleID;
using AnimationStateID = Util::GenerationalHandleID;

class AnimationSystem
{
public:
	void animate(double frame_time, double elapsed_time);
	void set_fixed_pose(Scene::Node &node, AnimationID id, float offset) const;
	void set_fixed_pose_multi(Scene::NodeHandle *nodes, unsigned num_nodes, AnimationID id, float offset) const;

	AnimationID register_animation(const std::string &name, const SceneFormats::Animation &animation, float key_frame_rate = 60.0f);
	AnimationID register_animation(const std::string &name, AnimationUnrolled animation);
	AnimationID get_animation_id_from_name(const std::string &name) const;

	AnimationStateID start_animation(Scene::Node &node, AnimationID id, double start_time);
	AnimationStateID start_animation_multi(Scene::NodeHandle *nodes, unsigned num_nodes, AnimationID id, double start_time);
	void stop_animation(AnimationStateID id);
	bool animation_is_running(AnimationStateID id) const;

	void set_repeating(AnimationStateID id, bool repeat);
	void set_relative_timing(AnimationStateID id, bool enable);

	void set_completion_callback(AnimationStateID id, std::function<void ()> cb);

private:
	struct AnimationState : Util::IntrusiveListEnabled<AnimationState>
	{
		AnimationState(const AnimationUnrolled &anim,
		               std::vector<Transform *> channel_transforms_,
		               std::vector<Scene::Node *> channel_nodes_,
		               double start_time_);

		AnimationState(const AnimationUnrolled &anim,
		               Scene::Node *node,
		               double start_time_);

		Scene::Node *skinned_node = nullptr;
		AnimationStateID id = 0;
		std::vector<Transform *> channel_transforms;
		std::vector<Scene::Node *> channel_nodes;
		const AnimationUnrolled &animation;
		double start_time = 0.0;
		bool repeating = false;
		bool relative_timing = false;

		std::function<void ()> cb;
	};

	Util::GenerationalHandlePool<AnimationUnrolled> animation_pool;
	Util::IntrusiveHashMap<Util::IntrusivePODWrapper<AnimationID>> animation_map;
	Util::GenerationalHandlePool<AnimationState> animation_state_pool;
	Util::IntrusiveList<AnimationState> active_animation;
};
}