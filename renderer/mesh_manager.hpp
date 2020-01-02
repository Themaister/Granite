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

// A scene loader which is design to load "template" glTF scenes and meshes, which can be instantiated
// with its own nodes and entities on-demand.

#include <string>
#include <unordered_map>
#include <vector>
#include "abstract_renderable.hpp"
#include "scene_formats.hpp"
#include "scene.hpp"
#include "intrusive_hash_map.hpp"
#include "animation_system.hpp"

namespace Granite
{
class MeshManager
{
private:
	struct MeshGroup;
public:
	MeshGroup *register_mesh(const std::string &path, AnimationSystem *animation_system = nullptr);
	MeshGroup *register_mesh(const std::function<
			AbstractRenderableHandle(const SceneFormats::Mesh &,
			                         const SceneFormats::MaterialInfo *)> &cb,
	                         const std::string &path,
	                         AnimationSystem *animation_system = nullptr);

	struct SingleHandle
	{
		Entity *entity;
		Scene::NodeHandle root_node;
	};

	struct MultiHandle
	{
		std::vector<Entity *> entities;
		Scene::NodeHandle root_node;
	};

	struct Animation
	{
		AnimationID id;
		std::string name;
	};

	// Instantiates a renderable which contains multiple renderables.
	// This generates multiple entities.
	MultiHandle instantiate_renderables(Scene &scene, MeshGroup *group);

	// Instantiates a lone renderable from the scene.
	// If there are multiple renderables, only the first one will be created.
	// Convenient for scenarios where there is just one renderable.
	SingleHandle instantiate_renderable(Scene &scene, MeshGroup *group);

	const std::vector<Animation> &get_animations(MeshGroup *group);

private:
	struct MeshGroup : Util::IntrusiveHashMapEnabled<MeshGroup>
	{
		std::vector<AbstractRenderableHandle> renderables;
		std::vector<SceneFormats::Node> node_hierarchy;
		std::vector<uint32_t> top_level_nodes;
		std::vector<Animation> animations;
	};
	Util::IntrusiveHashMap<MeshGroup> groups;
};
}
