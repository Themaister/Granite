/* Copyright (c) 2017-2021 Hans-Kristian Arntzen
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

#include "volumetric_diffuse.hpp"
#include "device.hpp"
#include "scene.hpp"
#include "task_composer.hpp"
#include "render_context.hpp"
#include <random>

namespace Granite
{
void VolumetricDiffuseLightManager::set_scene(Scene *scene)
{
	volumetric_diffuse = &scene->get_entity_pool().get_component_group<VolumetricDiffuseLightComponent>();
}

void VolumetricDiffuseLightManager::set_render_suite(const RendererSuite *suite_)
{
	suite = suite_;
}

void VolumetricDiffuseLightManager::refresh(const RenderContext &context, TaskComposer &composer)
{
	if (!volumetric_diffuse)
		return;
	auto &group = composer.begin_pipeline_stage();
	auto &device = context.get_device();

	for (auto &light_tuple : *volumetric_diffuse)
	{
		auto *light = get_component<VolumetricDiffuseLightComponent>(light_tuple);
		if (light->light.get_volume_view())
			continue;

		group.enqueue_task([light, &device]() {
			uvec3 res = light->light.get_resolution();
			auto info = Vulkan::ImageCreateInfo::immutable_3d_image(res.x * 6, res.y, res.z, VK_FORMAT_R8G8B8A8_SRGB);

			std::vector<u8vec4> data(res.x * res.y * res.z * 6);
			std::mt19937 rnd(109);

			for (auto &d : data)
				for (auto &c : d.data)
					c = uint8_t(rnd());

			const Vulkan::ImageInitialData initial = { data.data(), res.x * 6, res.y };
			auto volume = device.create_image(info, &initial);
			light->light.set_volume(std::move(volume));
		});
	}
}
}