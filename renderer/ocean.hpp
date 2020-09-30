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

#include "abstract_renderable.hpp"
#include "scene.hpp"
#include "application_wsi_events.hpp"
#include "fft/glfft.hpp"
#include "glfft_granite_interface.hpp"
#include "application_events.hpp"
#include <vector>

namespace Granite
{
class RenderTextureResource;
class RenderBufferResource;

static constexpr unsigned MaxOceanLayers = 4;

struct OceanConfig
{
	unsigned fft_resolution = 512;
	unsigned displacement_downsample = 1;
	unsigned grid_count = 32;
	unsigned grid_resolution = 128;

	vec2 ocean_size = vec2(512.0f);
	vec2 wind_velocity = vec2(10.0f, 5.0f);
	float normal_mod = 7.3f;
	float amplitude = 0.2f;

	struct
	{
		std::string input;
		float uv_scale = 0.01f;
		float depth[MaxOceanLayers] = { 2.0f, 4.0f, 6.0f, 8.0f };
		float emissive_mod = 1.0f;
		bool bandlimited_pixel = false;
		bool input_is_render_graph = false;
	} refraction;
};

class Ocean : public AbstractRenderable,
              public PerFrameRefreshable,
              public RenderPassCreator,
              public EventHandler
{
public:
	Ocean(const OceanConfig &config);

	struct Handles
	{
		Entity *entity;
		Ocean *ocean;
	};

	static Handles add_to_scene(Scene &scene, const OceanConfig &config = {});

	enum { FrequencyBands = 8 };
	void set_frequency_band_amplitude(unsigned band, float amplitude);
	void set_frequency_band_modulation(bool enable);

private:
	OceanConfig config;

	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &);
	bool on_frame_tick(const FrameTickEvent &e);
	std::unique_ptr<GLFFT::FFT> height_fft;
	std::unique_ptr<GLFFT::FFT> normal_fft;
	std::unique_ptr<GLFFT::FFT> displacement_fft;
	FFTInterface fft_iface;
	FFTDeferredCommandBuffer deferred_cmd;

	float frequency_bands[FrequencyBands];
	bool freq_band_modulation = false;

	bool has_static_aabb() const override
	{
		return false;
	}

	void get_render_info(const RenderContext &context,
	                     const RenderInfoComponent *transform,
	                     RenderQueue &queue) const override;

	const RenderContext *context = nullptr;

	void refresh(const RenderContext &context, TaskComposer &composer) override;

	void add_render_passes(RenderGraph &graph) override;
	void set_base_renderer(const RendererSuite *suite) override;
	void set_base_render_context(const RenderContext *context) override;
	void setup_render_pass_dependencies(RenderGraph &graph,
	                                    RenderPass &target) override;
	void setup_render_pass_resources(RenderGraph &graph) override;
	void set_scene(Scene *scene) override;

	std::vector<Vulkan::ImageViewHandle> vertex_mip_views;
	std::vector<Vulkan::ImageViewHandle> fragment_mip_views;
	std::vector<Vulkan::ImageViewHandle> normal_mip_views;

	Vulkan::BufferHandle distribution_buffer;
	Vulkan::BufferHandle distribution_buffer_displacement;
	Vulkan::BufferHandle distribution_buffer_normal;
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

	void build_lod_map(Vulkan::CommandBuffer &cmd);
	void cull_blocks(Vulkan::CommandBuffer &cmd);
	void init_counter_buffer(Vulkan::CommandBuffer &cmd);
	void update_lod_pass(Vulkan::CommandBuffer &cmd);
	void update_fft_pass(Vulkan::CommandBuffer &cmd);
	void update_fft_input(Vulkan::CommandBuffer &cmd);
	void compute_fft(Vulkan::CommandBuffer &cmd);
	void bake_maps(Vulkan::CommandBuffer &cmd);
	void generate_mipmaps(Vulkan::CommandBuffer &cmd);

	vec3 last_camera_position = vec3(0.0f);
	double current_time = 0.0;

	vec2 wind_direction;
	float phillips_L;

	struct LOD
	{
		Vulkan::BufferHandle vbo;
		Vulkan::BufferHandle ibo;
		unsigned count;
	};
	std::vector<LOD> quad_lod;
	Vulkan::BufferHandle border_vbo;
	Vulkan::BufferHandle border_ibo;
	unsigned border_count = 0;
	VkIndexType index_type = VK_INDEX_TYPE_UINT16;

	void build_buffers(Vulkan::Device &device);
	void build_lod(Vulkan::Device &device, unsigned size, unsigned stride);
	void init_distributions(Vulkan::Device &device);
	void build_border(std::vector<vec3> &positions, std::vector<uint16_t> &indices,
	                  ivec2 base, ivec2 dx, ivec2 dy);
	void build_corner(std::vector<vec3> &positions, std::vector<uint16_t> &indices,
	                  ivec2 base, ivec2 dx, ivec2 dy);
	void build_fill_edge(std::vector<vec3> &positions, std::vector<uint16_t> &indices,
	                     vec2 base_outer, vec2 end_outer,
	                     ivec2 base_inner, ivec2 delta, ivec2 corner_delta);

	void add_lod_update_pass(RenderGraph &graph);
	void add_fft_update_pass(RenderGraph &graph);
	vec2 get_snapped_grid_center() const;
	vec2 get_grid_size() const;
	ivec2 get_grid_base_coord() const;
	vec2 heightmap_world_size() const;
	vec2 normalmap_world_size() const;

	Vulkan::ImageView *refraction = nullptr;
	RenderTextureResource *refraction_resource = nullptr;
};
}