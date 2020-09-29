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

#include "lights.hpp"
#include "render_components.hpp"
#include "event.hpp"
#include "shader_manager.hpp"
#include "renderer.hpp"
#include "clusterer.hpp"
#include "application_events.hpp"

namespace Granite
{
class RenderTextureResource;
class RenderPass;

class VolumetricFog : public RenderPassCreator, public EventHandler
{
public:
	VolumetricFog();
	void add_texture_dependency(std::string name);
	void add_storage_buffer_dependency(std::string name);
	void set_resolution(unsigned width, unsigned height, unsigned depth);
	void set_z_range(float range);
	void set_fog_density(float density);

	struct FloorLighting
	{
		float position_mod = 0.01f;
		float base_y = -3.0f;
		float y_mod = 1.0f;
		float scatter_mod = 1.0f;
	};
	void set_floor_lighting(const std::string &input, const FloorLighting &info);

	void set_density_mod(float mod)
	{
		density_mod = mod;
	}

	void set_in_scatter_mod(float mod)
	{
		inscatter_mod = mod;
	}

	float get_slice_z_log2_scale() const;
	const Vulkan::ImageView &get_view() const;

private:
	std::vector<std::string> texture_dependencies;
	std::vector<std::string> buffer_dependencies;
	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);
	bool on_frame_tick(const FrameTickEvent &e);
	Vulkan::ImageHandle dither_lut;

	struct
	{
		std::string input;
		FloorLighting info;
		RenderTextureResource *input_resource = nullptr;
		const Vulkan::ImageView *input_view = nullptr;
	} floor;

	unsigned width = 160;
	unsigned height = 92;
	unsigned depth = 64;
	float z_range = 80.0f;
	float slice_z_log2_scale;
	float mod_time = 0.0f;
	float density_mod = 0.5f;
	float inscatter_mod = 0.25f;

	void add_render_passes(RenderGraph &graph) override;
	void setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target) override;
	void setup_render_pass_resources(RenderGraph &graph) override;
	void set_base_renderer(const RendererSuite *) override;
	void set_base_render_context(const RenderContext *context) override;
	void set_scene(Scene *scene) override;

	const Vulkan::ImageView *view = nullptr;
	const RenderContext *context = nullptr;
	RenderTextureResource *fog_volume = nullptr;
	RenderPass *pass = nullptr;

	void build_density(Vulkan::CommandBuffer &cmd, Vulkan::ImageView &fog_density, float freq_mod);
	void build_light_density(Vulkan::CommandBuffer &cmd,
	                         Vulkan::ImageView &light_density,
	                         Vulkan::ImageView &fog_density,
	                         Vulkan::ImageView &fog_density_low_freq,
	                         Vulkan::ImageView *light_density_history);
	void build_fog(Vulkan::CommandBuffer &cmd, Vulkan::ImageView &fog, Vulkan::ImageView &light);

	float slice_extents[1024];
	void compute_slice_extents();
	void build_dither_lut(Vulkan::Device &device);
	unsigned dither_offset = 0;

	mat4 old_projection;
};
}