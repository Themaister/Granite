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

#define NOMINMAX
#include "application.hpp"
#include "application_events.hpp"
#include "application_wsi.hpp"
#include "vulkan_headers.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include "stb_image_write.h"
#include "cli_parser.hpp"
#include "os_filesystem.hpp"
#include "rapidjson_wrapper.hpp"
#include <limits.h>
#include <cmath>
#include "thread_group.hpp"
#include "global_managers_init.hpp"
#include "path_utils.hpp"
#include "thread_group.hpp"
#include "asset_manager.hpp"

#ifdef HAVE_GRANITE_FFMPEG
#include "ffmpeg_encode.hpp"
#endif

#ifdef HAVE_GRANITE_AUDIO
#include "audio_interface.hpp"
#include "audio_mixer.hpp"
#endif

using namespace rapidjson;
using namespace Vulkan;
using namespace Util;

namespace Granite
{
struct WSIPlatformHeadless : Granite::GraniteWSIPlatform
{
public:
	~WSIPlatformHeadless() override
	{
		release_resources();
	}

	void release_resources() override
	{
		for (auto &t : swapchain_tasks)
			t.reset();
		if (last_task_dependency)
			last_task_dependency->wait();
		last_task_dependency.reset();

		auto *em = GRANITE_EVENT_MANAGER();
		if (em)
		{
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
		}

		swapchain_images.clear();
		readback_buffers.clear();
		acquire_semaphore.clear();
#ifdef HAVE_GRANITE_FFMPEG
		ycbcr_pipelines.clear();
#endif
	}

	bool alive(Vulkan::WSI &) override
	{
		return frames < max_frames;
	}

	void poll_input() override
	{
		std::lock_guard<std::mutex> holder{get_input_tracker().get_lock()};
		get_input_tracker().dispatch_current_state(get_frame_timer().get_frame_time());
	}

	void poll_input_async(Granite::InputTrackerHandler *override_handler) override
	{
		std::lock_guard<std::mutex> holder{get_input_tracker().get_lock()};
		get_input_tracker().dispatch_current_state(0.0, override_handler);
	}

	void enable_png_readback(std::string base_path)
	{
		png_readback = std::move(base_path);
	}

	std::vector<const char *> get_instance_extensions() override
	{
		return {};
	}

	VkSurfaceKHR create_surface(VkInstance, VkPhysicalDevice) override
	{
		return VK_NULL_HANDLE;
	}

	uint32_t get_surface_width() override
	{
		return width;
	}

	uint32_t get_surface_height() override
	{
		return height;
	}

	void notify_resize(unsigned width_, unsigned height_)
	{
		resize = true;
		width = width_;
		height = height_;
	}

	void set_max_frames(unsigned max_frames_)
	{
		max_frames = max_frames_;
	}

	bool has_external_swapchain() override
	{
		return true;
	}

	bool init(unsigned width_, unsigned height_)
	{
		width = width_;
		height = height_;
		if (!Context::init_loader(nullptr))
		{
			LOGE("Failed to initialize Vulkan loader.\n");
			return false;
		}

		auto *em = GRANITE_EVENT_MANAGER();
		if (em)
		{
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);
		}

		return true;
	}

	bool init_headless(Application *app_)
	{
		app = app_;

		auto context = Util::make_handle<Context>();

		Context::SystemHandles system_handles;
		system_handles.filesystem = GRANITE_FILESYSTEM();
		system_handles.thread_group = GRANITE_THREAD_GROUP();
		system_handles.timeline_trace_file = system_handles.thread_group->get_timeline_trace_file();
		system_handles.asset_manager = GRANITE_ASSET_MANAGER();
		context->set_system_handles(system_handles);

		context->set_num_thread_indices(GRANITE_THREAD_GROUP()->get_num_threads() + 1);
		const char *khr_surface = VK_KHR_SURFACE_EXTENSION_NAME;
		const char *khr_swapchain = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

		auto name = app->get_name();
		if (name.empty())
			name = Path::basename(Path::get_executable_path());
		VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app_info.pEngineName = "Granite";
		app_info.pApplicationName = name.empty() ? "Granite" : name.c_str();
		app_info.apiVersion = VK_API_VERSION_1_1;
		context->set_application_info(&app_info);

		if (!context->init_instance_and_device(&khr_surface, 1, &khr_swapchain, 1))
			return false;
		if (!app->init_wsi(std::move(context)))
			return false;

		auto &device = app->get_wsi().get_device();

		auto info = ImageCreateInfo::render_target(width, height, VK_FORMAT_R8G8B8A8_SRGB);
		info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.misc |= Vulkan::IMAGE_MISC_MUTABLE_SRGB_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;

		BufferCreateInfo readback = {};
		readback.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		readback.domain = BufferDomain::CachedHost;
		readback.size = width * height * sizeof(uint32_t);

		for (unsigned i = 0; i < SwapchainImages; i++)
		{
			swapchain_images.push_back(device.create_image(info, nullptr));
			readback_buffers.push_back(device.create_buffer(readback, nullptr));
			acquire_semaphore.emplace_back(nullptr);
		}

		// Target present layouts to be more accurate for timing in case PRESENT_SRC forces decompress,
		// and also makes sure pipeline caches are valid w.r.t render passes.
		for (auto &swap : swapchain_images)
			swap->set_swapchain_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		app->get_wsi().init_external_swapchain(swapchain_images);
		return true;
	}

#ifdef HAVE_GRANITE_FFMPEG
	void init_headless_recording(std::string path)
	{
#ifndef HAVE_GRANITE_RENDERER
		LOGE("Need to include system handles in build to encode.\n");
		return;
#endif

		video_encode_path = std::move(path);
		VideoEncoder::Options enc_opts = {};
		enc_opts.width = width;
		enc_opts.height = height;

		double frame_rate = std::round(1.0 / time_step);
		enc_opts.frame_timebase.num = 1;
		enc_opts.frame_timebase.den = int(frame_rate);

#ifdef HAVE_GRANITE_AUDIO
		enc_opts.realtime = true;
		record_stream.reset(Audio::create_default_audio_record_backend("headless", 44100.0f, 2));
		if (record_stream)
			encoder.set_audio_record_stream(record_stream.get());
#endif

		if (!encoder.init(&app->get_wsi().get_device(), video_encode_path.c_str(), enc_opts))
		{
			LOGE("Failed to initialize encoder.\n");
			video_encode_path.clear();
		}

#ifdef HAVE_GRANITE_RENDERER
		for (unsigned i = 0; i < SwapchainImages; i++)
		{
			auto &device = app->get_wsi().get_device();
			FFmpegEncode::Shaders<> shaders;

			shaders.rgb_to_yuv = device.get_shader_manager().register_compute(
					"builtin://shaders/util/rgb_to_yuv.comp")->register_variant({})->get_program();
			shaders.chroma_downsample = device.get_shader_manager().register_compute(
					"builtin://shaders/util/chroma_downsample.comp")->register_variant({})->get_program();

			ycbcr_pipelines.push_back(encoder.create_ycbcr_pipeline(shaders));
		}
#endif

#ifdef HAVE_GRANITE_AUDIO
		record_stream->start();
#endif
	}
#endif

	void set_time_step(double t)
	{
		time_step = t;
	}

	void begin_frame()
	{
		auto &wsi = app->get_wsi();
		wsi.set_external_frame(frame_index, std::move(acquire_semaphore[frame_index]), time_step);
		acquire_semaphore[frame_index] = {};
	}

	void end_frame()
	{
		auto &wsi = app->get_wsi();
		auto &device = wsi.get_device();
		auto release_semaphore = wsi.consume_external_release_semaphore();

		if (release_semaphore && release_semaphore->get_semaphore() != VK_NULL_HANDLE)
		{
			if (swapchain_tasks[frame_index])
			{
				swapchain_tasks[frame_index]->wait();
				swapchain_tasks[frame_index].reset();
			}

			acquire_semaphore[frame_index] = {};

			if (!next_readback_path.empty() || !png_readback.empty())
			{
				OwnershipTransferInfo transfer_info = {};
				transfer_info.old_queue = wsi.get_current_present_queue_type();
				transfer_info.new_queue = CommandBuffer::Type::AsyncTransfer;
				transfer_info.old_image_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
				transfer_info.new_image_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				transfer_info.dst_pipeline_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
				transfer_info.dst_access = VK_ACCESS_TRANSFER_READ_BIT;
				auto cmd = request_command_buffer_with_ownership_transfer(device, *swapchain_images[frame_index],
				                                                          transfer_info, release_semaphore);

				cmd->copy_image_to_buffer(*readback_buffers[frame_index], *swapchain_images[frame_index],
				                          0, {}, {width, height, 1},
				                          0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});

				cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

				Fence readback_fence;
				device.submit(cmd, &readback_fence, 1, &acquire_semaphore[frame_index]);

				if (!next_readback_path.empty())
				{
					swapchain_tasks[frame_index] = GRANITE_THREAD_GROUP()->create_task(
							[this, readback_fence, index = frame_index, frame = this->frames, p = std::make_unique<std::string>(next_readback_path)]() mutable {
								readback_fence->wait();
								dump_frame_single(*p, frame, index);
							});
					next_readback_path.clear();
				}
				else
				{
					swapchain_tasks[frame_index] = GRANITE_THREAD_GROUP()->create_task(
							[this, readback_fence, index = frame_index, frame = this->frames]() mutable {
								readback_fence->wait();
								dump_frame(frame, index);
							});
				}
			}
#ifdef HAVE_GRANITE_FFMPEG
			else if (!video_encode_path.empty())
			{
				auto pts = encoder.sample_realtime_pts();

				OwnershipTransferInfo transfer_info = {};
				transfer_info.old_queue = wsi.get_current_present_queue_type();
				transfer_info.new_queue = CommandBuffer::Type::AsyncCompute;
				transfer_info.old_image_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
				transfer_info.new_image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				transfer_info.dst_pipeline_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
				transfer_info.dst_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
				auto cmd = request_command_buffer_with_ownership_transfer(device, *swapchain_images[frame_index],
				                                                          transfer_info, release_semaphore);

				encoder.process_rgb(*cmd, ycbcr_pipelines[frame_index], swapchain_images[frame_index]->get_view());
				encoder.submit_process_rgb(cmd, ycbcr_pipelines[frame_index]);

				acquire_semaphore[frame_index] = device.request_semaphore(VK_SEMAPHORE_TYPE_BINARY);
				device.submit_empty(CommandBuffer::Type::AsyncCompute, nullptr, acquire_semaphore[frame_index].get());

				swapchain_tasks[frame_index] = GRANITE_THREAD_GROUP()->create_task(
						[this, index = frame_index, pts]() mutable {
							if (!encoder.encode_frame(ycbcr_pipelines[index], pts))
								LOGE("Failed to push frame to encoder.\n");
						});
			}
#endif
			else
			{
				// Do nothing.
				acquire_semaphore[frame_index] = std::move(release_semaphore);
			}

			if (swapchain_tasks[frame_index])
			{
				swapchain_tasks[frame_index]->set_desc("application-headless-readback");
				swapchain_tasks[frame_index]->set_task_class(TaskClass::Background);

				if (last_task_dependency)
					GRANITE_THREAD_GROUP()->add_dependency(*swapchain_tasks[frame_index], *last_task_dependency);

				// Add a dummy task that only serves to chain dependencies.
				last_task_dependency = GRANITE_THREAD_GROUP()->create_task();
				last_task_dependency->set_task_class(TaskClass::Background);
				GRANITE_THREAD_GROUP()->add_dependency(*last_task_dependency, *swapchain_tasks[frame_index]);
				swapchain_tasks[frame_index]->flush();
			}
		}

		release_semaphore = {};
		frame_index = (frame_index + 1) % SwapchainImages;
		frames++;
	}

	void set_next_readback(std::string path)
	{
		next_readback_path = std::move(path);
	}

	void wait_threads()
	{
		GRANITE_THREAD_GROUP()->wait_idle();
	}

private:
	unsigned width = 0;
	unsigned height = 0;
	unsigned frames = 0;
	unsigned max_frames = UINT_MAX;
	unsigned frame_index = 0;
	double time_step = 0.01;
	std::string png_readback;
	std::string video_encode_path;
	enum { SwapchainImages = 4 };

#ifdef HAVE_GRANITE_AUDIO
	std::unique_ptr<Audio::RecordStream> record_stream;
#endif

	std::vector<ImageHandle> swapchain_images;
	std::vector<BufferHandle> readback_buffers;
	std::vector<Semaphore> acquire_semaphore;
	std::string next_readback_path;
	TaskGroupHandle swapchain_tasks[SwapchainImages];
	TaskGroupHandle last_task_dependency;

#ifdef HAVE_GRANITE_FFMPEG
	VideoEncoder encoder;
	std::vector<VideoEncoder::YCbCrPipeline> ycbcr_pipelines;
#endif

	void dump_frame_single(const std::string &path, unsigned frame, unsigned index)
	{
		auto &wsi = app->get_wsi();
		auto &device = wsi.get_device();

		LOGI("Dumping frame: %u (index: %u)\n", frame, index);

		auto *ptr = static_cast<uint32_t *>(device.map_host_buffer(*readback_buffers[index], MEMORY_ACCESS_READ_WRITE_BIT));
		for (unsigned i = 0; i < width * height; i++)
			ptr[i] |= 0xff000000u;

		if (!stbi_write_png(path.c_str(), width, height, 4, ptr, width * 4))
			LOGE("Failed to write PNG to disk.\n");
		device.unmap_host_buffer(*readback_buffers[index], MEMORY_ACCESS_READ_WRITE_BIT);
	}

	void dump_frame(unsigned frame, unsigned index)
	{
		char buffer[64];
		sprintf(buffer, "_%05u.png", frame);
		auto path = png_readback + buffer;
		dump_frame_single(path, frame, index);
	}

	Application *app = nullptr;
};
}

static void print_help()
{
	LOGI("[--png-path <path>] [--stat <output.json>]\n"
	     "[--fs-assets <path>] [--fs-cache <path>] [--fs-builtin <path>]\n"
	     "[--video-encode-path <path>]\n"
	     "[--png-reference-path <path>] [--frames <frames>] [--width <width>] [--height <height>] [--time-step <step>].\n");
}

namespace Granite
{
int application_main_headless(
		bool (*query_application_interface)(ApplicationQuery, void *, size_t),
		Application *(*create_application)(int, char **),
		int argc, char *argv[])
{
	if (argc < 1)
		return 1;

	struct Args
	{
		std::string png_path;
		std::string video_encode_path;
		std::string png_reference_path;
		std::string stat;
		std::string assets;
		std::string cache;
		std::string builtin;
		unsigned max_frames = UINT_MAX;
		unsigned width = 1280;
		unsigned height = 720;
		double time_step = 0.01;
	} args;

	CLICallbacks cbs;
	cbs.add("--frames", [&](CLIParser &parser) { args.max_frames = parser.next_uint(); });
	cbs.add("--width", [&](CLIParser &parser) { args.width = parser.next_uint(); });
	cbs.add("--height", [&](CLIParser &parser) { args.height = parser.next_uint(); });
	cbs.add("--time-step", [&](CLIParser &parser) { args.time_step = parser.next_double(); });
	cbs.add("--png-path", [&](CLIParser &parser) { args.png_path = parser.next_string(); });
	cbs.add("--png-reference-path", [&](CLIParser &parser) { args.png_reference_path = parser.next_string(); });
	cbs.add("--video-encode-path", [&](CLIParser &parser) { args.video_encode_path = parser.next_string(); });
	cbs.add("--fs-assets", [&](CLIParser &parser) { args.assets = parser.next_string(); });
	cbs.add("--fs-builtin", [&](CLIParser &parser) { args.builtin = parser.next_string(); });
	cbs.add("--fs-cache", [&](CLIParser &parser) { args.cache = parser.next_string(); });
	cbs.add("--stat", [&](CLIParser &parser) { args.stat = parser.next_string(); });
	cbs.add("--help", [](CLIParser &parser)
	{
		print_help();
		parser.end();
	});
	cbs.error_handler = [&]() { print_help(); };
	int exit_code;

	if (!Util::parse_cli_filtered(std::move(cbs), argc, argv, exit_code))
		return exit_code;

	ApplicationQueryDefaultManagerFlags flags{Global::MANAGER_FEATURE_DEFAULT_BITS};
	query_application_interface(ApplicationQuery::DefaultManagerFlags, &flags, sizeof(flags));
	Granite::Global::init(flags.manager_feature_flags);

	if (flags.manager_feature_flags & Global::MANAGER_FEATURE_FILESYSTEM_BIT)
	{
		if (!args.assets.empty())
			GRANITE_FILESYSTEM()->register_protocol("assets", std::make_unique<OSFilesystem>(args.assets));
		if (!args.builtin.empty())
			GRANITE_FILESYSTEM()->register_protocol("builtin", std::make_unique<OSFilesystem>(args.builtin));
		if (!args.cache.empty())
			GRANITE_FILESYSTEM()->register_protocol("cache", std::make_unique<OSFilesystem>(args.cache));
	}

	auto app = std::unique_ptr<Application>(create_application(argc, argv));

	if (app)
	{
		auto platform = std::make_unique<WSIPlatformHeadless>();
		if (!platform->init(args.width, args.height))
			return 1;

		auto *p = platform.get();

		if (!app->init_platform(std::move(platform)))
			return 1;

		p->set_max_frames(args.max_frames);
		p->set_time_step(args.time_step);
		p->init_headless(app.get());

		// Ensure all startup work is complete.
		while (app->get_wsi().get_device().query_initialization_progress(Vulkan::Device::InitializationStage::Pipelines) < 100 &&
		       app->poll())
		{
			p->begin_frame();
			app->run_frame();
			p->end_frame();
		}

		if (!args.png_path.empty())
			p->enable_png_readback(args.png_path);

		if (!args.video_encode_path.empty())
		{
#ifdef HAVE_GRANITE_FFMPEG
			p->init_headless_recording(args.video_encode_path);
#else
			LOGE("FFmpeg is not enabled in build.\n");
#endif
		}

#ifdef HAVE_GRANITE_AUDIO
		Global::start_audio_system();
#endif

		// Run warm-up frame.
		if (app->poll())
		{
			p->begin_frame();
			app->run_frame();
			p->end_frame();
		}

		p->wait_threads();
		app->get_wsi().get_device().wait_idle();
		app->get_wsi().get_device().timestamp_log_reset();

		LOGI("=== Begin run ===\n");

		auto start_time = get_current_time_nsecs();
		unsigned rendered_frames = 0;
		while (app->poll())
		{
			p->begin_frame();
			app->run_frame();
			p->end_frame();
			if (!args.video_encode_path.empty() || !args.png_path.empty())
			{
				LOGI("   Queued frame %u (Total time = %.3f ms).\n", rendered_frames,
				     1e-6 * double(get_current_time_nsecs() - start_time));
			}
			rendered_frames++;
		}

		p->wait_threads();
		app->get_wsi().get_device().wait_idle();
		auto end_time = get_current_time_nsecs();

		LOGI("=== End run ===\n");

		struct Report
		{
			std::string tag;
			TimestampIntervalReport report;
		};
		std::vector<Report> reports;
		app->get_wsi().get_device().timestamp_log([&](const std::string &tag, const TimestampIntervalReport &report) {
			reports.push_back({ tag, report });
		});
		app->get_wsi().get_device().timestamp_log_reset();

		if (rendered_frames)
		{
			double usec = 1e-3 * double(end_time - start_time) / rendered_frames;
			LOGI("Average frame time: %.3f usec\n", usec);

			if (!args.stat.empty())
			{
				Document doc;
				doc.SetObject();
				auto &allocator = doc.GetAllocator();

				doc.AddMember("averageFrameTimeUs", usec, allocator);
				doc.AddMember("gpu", StringRef(app->get_wsi().get_context().get_gpu_props().deviceName), allocator);
				doc.AddMember("driverVersion", app->get_wsi().get_context().get_gpu_props().driverVersion, allocator);

				if (!reports.empty())
				{
					Value report_objs(kObjectType);
					for (auto &rep : reports)
					{
						Value report_obj(kObjectType);
						report_obj.AddMember("timePerAccumulationUs", 1e6 * rep.report.time_per_accumulation, allocator);
						report_obj.AddMember("timePerFrameContextUs", 1e6 * rep.report.time_per_frame_context, allocator);
						report_obj.AddMember("accumulationsPerFrameContext", rep.report.accumulations_per_frame_context, allocator);
						report_objs.AddMember(StringRef(rep.tag), report_obj, allocator);
					}
					doc.AddMember("performance", report_objs, allocator);
				}

				StringBuffer buffer;
				PrettyWriter<StringBuffer> writer(buffer);
				doc.Accept(writer);

				if (!GRANITE_FILESYSTEM()->write_string_to_file(args.stat, buffer.GetString()))
					LOGE("Failed to write stat file to disk.\n");
			}
		}

		if (!args.png_reference_path.empty())
		{
			p->set_next_readback(args.png_reference_path);
			p->begin_frame();
			app->run_frame();
			p->end_frame();
		}

		p->wait_threads();

#ifdef HAVE_GRANITE_AUDIO
		Global::stop_audio_system();
#endif

		app.reset();
		Granite::Global::deinit();
		return 0;
	}
	else
		return 1;
}
}
