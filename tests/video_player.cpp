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

#include "ffmpeg_decode.hpp"
#include "application.hpp"
#include "application_wsi_events.hpp"
#include "abstract_renderable.hpp"
#include "render_context.hpp"
#include "render_queue.hpp"
#include "render_components.hpp"
#include "renderer.hpp"
#include "scene_loader.hpp"
#ifdef HAVE_GRANITE_AUDIO
#include "audio_mixer.hpp"
#endif

using namespace Granite;

struct TexInstanceInfo
{
	mat4 mvp;
	const Vulkan::ImageView *view;
};

struct TexStaticInfo
{
	Vulkan::Program *program;
};

static void video_frame_render(Vulkan::CommandBuffer &cmd, const RenderQueueData *infos, unsigned num_instances)
{
	cmd.set_program(static_cast<const TexStaticInfo *>(infos[0].render_info)->program);
	for (unsigned i = 0; i < num_instances; i++)
	{
		auto *instance = static_cast<const TexInstanceInfo *>(infos[i].instance_data);
		cmd.set_texture(2, 0, *instance->view, Vulkan::StockSampler::DefaultGeometryFilterClamp);
		*cmd.allocate_typed_constant_data<mat4>(3, 0, 1) = instance->mvp;

		cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		cmd.set_cull_mode(VK_CULL_MODE_NONE);
		cmd.draw(4);
	}
}

struct VideoTextureRenderable : AbstractRenderable
{
	explicit VideoTextureRenderable(const char *path)
	{
		VideoDecoder::DecodeOptions opts;
		opts.mipgen = true;
#ifdef HAVE_GRANITE_AUDIO
		if (!decoder.init(GRANITE_AUDIO_MIXER(), path, opts))
#else
		if (!decoder.init(nullptr, path, opts))
#endif
		{
			throw std::runtime_error("Failed to open file");
		}
	}

	void get_render_info(const RenderContext &context, const RenderInfoComponent *transform, RenderQueue &queue) const override
	{
		if (!frame.view)
			return;

		auto mvp = context.get_render_parameters().view_projection * transform->get_world_transform();
		auto *info = queue.allocate_one<TexInstanceInfo>();
		info->mvp = mvp;
		info->view = frame.view;

		auto *static_info = queue.push<TexStaticInfo>(Queue::Opaque, 1, 1, video_frame_render, info);
		if (static_info)
		{
			auto *temp = context.get_device().get_shader_manager().register_graphics(
					"assets://shaders/video.vert", "assets://shaders/video.frag");
			auto *variant = temp->register_variant({});
			static_info->program = variant->get_program();
		}
	}

	bool has_static_aabb() const override
	{
		return true;
	}

	const AABB *get_static_aabb() const override
	{
		return &aabb;
	}

	AABB aabb{vec3(-1.0f, -0.001f, -1.0f),
	          vec3(1.0f, 0.001f, 1.0f)};

	void shift_frame()
	{
		if (frame.view)
		{
			// If we never actually read the image and discarded it,
			// we just forward the acquire semaphore directly to release.
			// This resolves any write-after-write hazard for the image.
			VK_ASSERT(frame.sem);
			decoder.release_video_frame(frame.index, std::move(frame.sem));
		}

		frame = std::move(next_frame);
		next_frame = {};
		need_acquire = true;
	}

	bool update(Vulkan::Device &device, double elapsed_time)
	{
		// Based on the audio PTS, we want to display a video frame that is slightly larger.
		double target_pts = decoder.get_estimated_audio_playback_timestamp(elapsed_time);
		if (target_pts < 0.0)
			target_pts = elapsed_time;

		// Update the latest frame. We want the closest PTS to target_pts.
		if (!next_frame.view)
			if (decoder.try_acquire_video_frame(next_frame) < 0 && target_pts > frame.pts)
				return false;

		while (next_frame.view)
		{
			// If we have two candidates, shift out frame if next_frame PTS is closer.
			double d_current = std::abs(frame.pts - target_pts);
			double d_next = std::abs(next_frame.pts - target_pts);

			// In case we get two frames with same PTS for whatever reason, ensure forward progress.
			// The less-equal check is load-bearing.
			if (d_next <= d_current || !frame.view)
			{
				shift_frame();

				// Try to catch up quickly by skipping frames if we have to.
				// Defer any EOF handling to next frame.
				decoder.try_acquire_video_frame(next_frame);
			}
			else
				break;
		}

		if (need_acquire)
		{
			// When we have committed to display this video frame,
			// inject the wait semaphore.
			device.add_wait_semaphore(
					Vulkan::CommandBuffer::Type::Generic, std::move(frame.sem),
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, true);
			frame.sem = {};
			need_acquire = false;
		}

		return true;
	}

	void begin(Vulkan::Device &device)
	{
		FFmpegDecode::Shaders<> shaders;
		auto *comp = device.get_shader_manager().register_compute("builtin://shaders/util/yuv_to_rgb.comp");
		shaders.yuv_to_rgb = comp->register_variant({})->get_program();

		if (!decoder.begin_device_context(&device, shaders))
			LOGE("Failed to begin device context.\n");
		if (!decoder.play())
			LOGE("Failed to begin playback.\n");
	}

	void end()
	{
		frame = {};
		next_frame = {};
		decoder.stop();
		decoder.end_device_context();
	}

	VideoDecoder decoder;
	VideoFrame frame, next_frame;
	bool need_acquire = false;
};

struct VideoPlayerApplication : Application, EventHandler
{
	VideoPlayerApplication(const char *gltf_path, const char *video_path)
		: renderer(RendererType::GeneralForward, nullptr)
	{
		if (gltf_path)
			scene_loader.load_scene(gltf_path);

		auto &scene = scene_loader.get_scene();
		auto video = Util::make_handle<VideoTextureRenderable>(video_path);
		auto node = scene.create_node();
		scene.create_renderable(video, node.get());

		float aspect = float(video->decoder.get_width()) / float(video->decoder.get_height());
		float x_scale = 1.0f * aspect;
		float z_scale = 1.0f;
		node->get_transform().scale = vec3(x_scale, 1.0f, z_scale);
		node->get_transform().rotation = angleAxis(muglm::half_pi<float>(), vec3(0.0f, 1.0f, 0.0f)) * angleAxis(muglm::half_pi<float>(), vec3(1.0f, 0.0f, 0.0f));
		node->get_transform().translation = vec3(0.0f, 1.0f, -0.5f);
		node->invalidate_cached_transform();
		auto root = scene.get_root_node();
		if (root)
			root->add_child(std::move(node));
		else
			scene.set_root_node(std::move(node));

		videos.push_back(std::move(video));

		EVENT_MANAGER_REGISTER_LATCH(VideoPlayerApplication, on_module_created, on_module_destroyed, Vulkan::DeviceShaderModuleReadyEvent);
		EVENT_MANAGER_REGISTER(VideoPlayerApplication, on_key_pressed, KeyboardEvent);

		fps_camera.set_position(vec3(5.0f, 2.0f, 0.0f));
		fps_camera.look_at(vec3(5.0f, 2.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f));
		fps_camera.set_depth_range(0.1f, 500.0f);
	}

	bool on_key_pressed(const KeyboardEvent &e)
	{
		if (e.get_key_state() == KeyState::Pressed)
		{
			double seek_offset = 0.0;
			bool drop_frame = false;

			if (e.get_key() == Key::R)
			{
				for (auto &video : videos)
				{
					if (!video->decoder.seek(0.0))
						LOGE("Failed to rewind.\n");
					else
						drop_frame = true;
				}
			}
			else if (e.get_key() == Key::Space)
			{
				for (auto &video : videos)
					video->decoder.set_paused(!video->decoder.get_paused());
			}
			else if (e.get_key() == Key::H)
				seek_offset = -10.0;
			else if (e.get_key() == Key::L)
				seek_offset = +10.0;
			else if (e.get_key() == Key::K)
				seek_offset = +60.0;
			else if (e.get_key() == Key::J)
				seek_offset = -60.0;

			if (seek_offset != 0.0)
			{
				for (auto &video : videos)
				{
					auto ts = video->decoder.get_estimated_audio_playback_timestamp_raw();
					if (ts >= 0.0)
					{
						if (video->decoder.seek(ts + seek_offset))
							drop_frame = true;
						else
							LOGE("Failed to seek.\n");
					}
				}
			}

			if (drop_frame)
			{
				for (auto &video : videos)
				{
					video->frame = {};
					video->next_frame = {};
				}
			}
		}

		return true;
	}

	void on_module_created(const Vulkan::DeviceShaderModuleReadyEvent &e)
	{
		for (auto &video : videos)
			video->begin(e.get_device());
	}

	void on_module_destroyed(const Vulkan::DeviceShaderModuleReadyEvent &)
	{
		for (auto &video : videos)
			video->end();
	}

	void render_frame(double, double elapsed_time) override
	{
		auto &device = get_wsi().get_device();

		auto &scene = scene_loader.get_scene();
		scene.update_all_transforms();

		for (auto &video : videos)
			if (!video->update(device, elapsed_time))
				request_shutdown();

		context.set_device(&device);
		context.set_camera(fps_camera);
		context.set_lighting_parameters(&lighting);
		lighting.directional.direction = normalize(vec3(1.0f, 1.0f, 1.0f));
		lighting.directional.color = normalize(vec3(2.0f, 1.5f, 1.0f));
		renderer.set_mesh_renderer_options_from_lighting(lighting);

		renderer.begin(queue);

		visible.clear();
		scene.gather_visible_opaque_renderables(context.get_visibility_frustum(),
												visible);

		queue.push_renderables(context, visible.data(), visible.size());

		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(Vulkan::SwapchainRenderPass::Depth);
		rp.clear_color[0].float32[0] = 0.01f;
		rp.clear_color[0].float32[1] = 0.02f;
		rp.clear_color[0].float32[2] = 0.03f;

		cmd->begin_render_pass(rp);
		renderer.flush(*cmd, queue, context);
		cmd->end_render_pass();

		output_sems.resize(videos.size());
		device.submit(cmd, nullptr, output_sems.size(), output_sems.data());
		for (size_t i = 0, n = videos.size(); i < n; i++)
			videos[i]->frame.sem = std::move(output_sems[i]);
		output_sems.clear();
	}

	Util::SmallVector<Util::IntrusivePtr<VideoTextureRenderable>> videos;

	SceneLoader scene_loader;
	FPSCamera fps_camera;
	RenderContext context;
	RenderQueue queue;
	Renderer renderer;
	LightingParameters lighting = {};
	VisibilityList visible;
	Util::SmallVector<Vulkan::Semaphore> output_sems;
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	if (argc != 3 && argc != 2)
		return nullptr;

	try
	{
		const char *gltf_path = nullptr;
		const char *video_path = nullptr;
		if (argc == 3)
		{
			gltf_path = argv[1];
			video_path = argv[2];
		}
		else
			video_path = argv[1];

		auto *app = new VideoPlayerApplication(gltf_path, video_path);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
} // namespace Granite

