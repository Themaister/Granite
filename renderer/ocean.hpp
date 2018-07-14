/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

#include "abstract_renderable.hpp"
#include "scene.hpp"
#include "vulkan_events.hpp"
#include "fft/glfft.hpp"
#include "glfft_granite_interface.hpp"
#include "application_events.hpp"
#include <vector>

namespace Granite
{
class RenderTextureResource;
class RenderBufferResource;

class Ocean : public AbstractRenderable,
              public PerFrameRefreshable,
              public RenderPassCreator,
              public EventHandler
{
public:
	Ocean();

private:
	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &);
	bool on_frame_tick(const FrameTickEvent &e);
	std::unique_ptr<GLFFT::FFT> height_fft;
	std::unique_ptr<GLFFT::FFT> normal_fft;
	std::unique_ptr<GLFFT::FFT> displacement_fft;
	FFTInterface fft_iface;

	bool has_static_aabb() const override
	{
		return false;
	}

	void get_render_info(const RenderContext &context,
	                     const CachedSpatialTransformComponent *transform,
	                     RenderQueue &queue) const override;

	const RenderContext *context = nullptr;

	void refresh(RenderContext &context) override;

	void add_render_passes(RenderGraph &graph) override;
	void set_base_renderer(Renderer *forward_renderer,
	                       Renderer *deferred_renderer,
	                       Renderer *depth_renderer) override;
	void set_base_render_context(const RenderContext *context) override;
	void setup_render_pass_dependencies(RenderGraph &graph,
	                                    RenderPass &target) override;
	void setup_render_pass_resources(RenderGraph &graph) override;
	void set_scene(Scene *scene) override;

	std::vector<Vulkan::ImageViewHandle> vertex_mip_views;
	std::vector<Vulkan::ImageViewHandle> fragment_mip_views;
	Vulkan::BufferHandle distribution_buffer;
	RenderTextureResource *ocean_lod = nullptr;
	RenderBufferResource *lod_data = nullptr;
	RenderBufferResource *lod_data_counters = nullptr;

	RenderBufferResource *height_fft_input = nullptr;
	RenderBufferResource *displacement_fft_input = nullptr;
	RenderBufferResource *normal_fft_input = nullptr;

	RenderTextureResource *height_fft_output = nullptr;
	RenderTextureResource *displacement_fft_output = nullptr;
	RenderTextureResource *normal_fft_output = nullptr;

	RenderTextureResource *height_displacement_output = nullptr;
	RenderTextureResource *gradient_jacobian_output = nullptr;

	RenderGraph *graph = nullptr;

	void update_lod_pass(Vulkan::CommandBuffer &cmd);
	void update_fft_pass(Vulkan::CommandBuffer &cmd);
	void update_fft_input(Vulkan::CommandBuffer &cmd);
	void compute_fft(Vulkan::CommandBuffer &cmd);

	unsigned grid_width = 32;
	unsigned grid_height = 32;
	vec3 last_camera_position = vec3(0.0f);
	vec2 size = vec2(256.0f);
	vec2 size_normal = vec2(256.0f / 7.3f);
	unsigned grid_resolution = 128;

	unsigned height_fft_size = 256;
	unsigned displacement_fft_size = 128;
	unsigned normal_fft_size = 256;

	double current_time = 0.0;

	struct LOD
	{
		Vulkan::BufferHandle vbo;
		Vulkan::BufferHandle ibo;
		unsigned count;
	};
	std::vector<LOD> quad_lod;

	void build_buffers(Vulkan::Device &device);
	void build_lod(Vulkan::Device &device, unsigned size, unsigned stride);

	void add_lod_update_pass(RenderGraph &graph);
	void add_fft_update_pass(RenderGraph &graph);
};
}