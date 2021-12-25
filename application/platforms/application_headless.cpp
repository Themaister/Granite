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
#include "thread_latch.hpp"

#ifdef HAVE_GRANITE_FFMPEG
#include "ffmpeg.hpp"
#endif

#ifdef HAVE_GRANITE_AUDIO
#include "audio_interface.hpp"
#include "audio_mixer.hpp"
#endif

using namespace rapidjson;

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

using namespace std;
using namespace Vulkan;
using namespace Util;

namespace Granite
{
class FrameWorker
{
public:
	FrameWorker();
	~FrameWorker();

	void wait();
	void set_work(function<void ()> work);

private:
	thread thr;
	condition_variable cond;
	mutex cond_lock;
	function<void ()> func;
	bool working = false;
	bool dead = false;

	void thread_loop();
};

FrameWorker::FrameWorker()
{
	thr = thread(&FrameWorker::thread_loop, this);
}

void FrameWorker::wait()
{
	unique_lock<mutex> u{cond_lock};
	cond.wait(u, [this]() -> bool {
		return !working;
	});
}

void FrameWorker::set_work(function<void()> work)
{
	wait();
	func = move(work);
	unique_lock<mutex> u{cond_lock};
	working = true;
	cond.notify_one();
}

void FrameWorker::thread_loop()
{
	for (;;)
	{
		{
			unique_lock<mutex> u{cond_lock};
			cond.wait(u, [this]() -> bool {
				return working || dead;
			});

			if (dead)
				return;
		}

		if (func)
			func();

		lock_guard<mutex> holder{cond_lock};
		working = false;
		cond.notify_one();
	}
}

FrameWorker::~FrameWorker()
{
	{
		lock_guard<mutex> holder{cond_lock};
		dead = true;
		cond.notify_one();
	}

	if (thr.joinable())
		thr.join();
}

struct WSIPlatformHeadless : Granite::GraniteWSIPlatform
{
public:
	float get_estimated_frame_presentation_duration() override
	{
		return 0.0f;
	}

	~WSIPlatformHeadless() override
	{
		release_resources();
	}

	void release_resources() override
	{
		for (auto &thread : worker_threads)
			thread->wait();

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
		readback_fence.clear();
	}

	bool alive(Vulkan::WSI &) override
	{
		return frames < max_frames;
	}

	void poll_input() override
	{
		get_input_tracker().dispatch_current_state(get_frame_timer().get_frame_time());
	}

	void enable_png_readback(string base_path)
	{
		png_readback = std::move(base_path);
	}

	void enable_video_encode(string path)
	{
		video_encode_path = std::move(path);
#ifndef HAVE_GRANITE_FFMPEG
		LOGE("HAVE_GRANITE_FFMPEG is not defined. Video encode not supported.\n");
#endif
	}

	vector<const char *> get_instance_extensions() override
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

		auto &wsi = app->get_wsi();
		auto context = make_unique<Context>();

		Context::SystemHandles system_handles;
		system_handles.filesystem = GRANITE_FILESYSTEM();
		system_handles.thread_group = GRANITE_THREAD_GROUP();
		system_handles.timeline_trace_file = system_handles.thread_group->get_timeline_trace_file();
		context->set_system_handles(system_handles);

		context->set_num_thread_indices(GRANITE_THREAD_GROUP()->get_num_threads() + 1);
		const char *khr_surface = VK_KHR_SURFACE_EXTENSION_NAME;
		const char *khr_swapchain = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
		if (!context->init_instance_and_device(&khr_surface, 1, &khr_swapchain, 1))
			return false;
		wsi.init_external_context(move(context));

		auto &device = wsi.get_device();

		auto info = ImageCreateInfo::render_target(width, height, VK_FORMAT_R8G8B8A8_SRGB);
		info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;

		BufferCreateInfo readback = {};
		readback.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		readback.domain = BufferDomain::CachedHost;
		readback.size = width * height * sizeof(uint32_t);

		for (unsigned i = 0; i < SwapchainImages; i++)
		{
			swapchain_images.push_back(device.create_image(info, nullptr));
			readback_buffers.push_back(device.create_buffer(readback, nullptr));
			acquire_semaphore.push_back(Semaphore(nullptr));
			worker_threads.push_back(make_unique<FrameWorker>());
			readback_fence.push_back({});
		}

		// Target present layouts to be more accurate for timing in case PRESENT_SRC forces decompress,
		// and also makes sure pipeline caches are valid w.r.t render passes.
		for (auto &swap : swapchain_images)
			swap->set_swapchain_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

#ifdef HAVE_GRANITE_FFMPEG
		if (!video_encode_path.empty())
		{
			VideoEncoder::Options enc_opts = {};
			enc_opts.width = width;
			enc_opts.height = height;

			double frame_rate = std::round(1.0 / time_step);
			enc_opts.frame_timebase.num = 1;
			enc_opts.frame_timebase.den = int(frame_rate);

#ifdef HAVE_GRANITE_AUDIO
			auto *mixer = new Audio::Mixer;
			auto *audio_dumper = new Audio::DumpBackend(
					mixer, 48000.0f, 2,
					unsigned(std::ceil(48000.0f / frame_rate)));
			Global::install_audio_system(audio_dumper, mixer);
			encoder.set_audio_source(audio_dumper);
#endif

			if (!encoder.init(&device, video_encode_path.c_str(), enc_opts))
			{
				LOGE("Failed to initialize encoder.\n");
				video_encode_path.clear();
			}
		}
#endif

		wsi.init_external_swapchain(swapchain_images);
		return true;
	}

	void set_time_step(double t)
	{
		time_step = t;
	}

	void begin_frame()
	{
		auto &wsi = app->get_wsi();
		wsi.set_external_frame(frame_index, acquire_semaphore[frame_index], time_step);
		acquire_semaphore[frame_index].reset();
		worker_threads[frame_index]->wait();
	}

	void end_frame()
	{
		auto &wsi = app->get_wsi();
		auto &device = wsi.get_device();
		auto release_semaphore = wsi.consume_external_release_semaphore();

		if (release_semaphore && release_semaphore->get_semaphore() != VK_NULL_HANDLE)
		{
			if (next_readback_cb || !png_readback.empty())
			{
				OwnershipTransferInfo transfer_info = {};
				transfer_info.old_queue = CommandBuffer::Type::AsyncGraphics;
				transfer_info.new_queue = CommandBuffer::Type::AsyncTransfer;
				transfer_info.old_image_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
				transfer_info.new_image_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				transfer_info.dst_pipeline_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				transfer_info.dst_access = VK_ACCESS_TRANSFER_READ_BIT;
				auto cmd = request_command_buffer_with_ownership_transfer(device, *swapchain_images[frame_index],
				                                                          transfer_info, release_semaphore);

				cmd->copy_image_to_buffer(*readback_buffers[frame_index], *swapchain_images[frame_index],
				                          0, {}, {width, height, 1},
				                          0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});

				cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

				thread_latches[frame_index].wait_latch_cleared();
				device.submit(cmd, &readback_fence[frame_index], 1, &acquire_semaphore[frame_index]);
				thread_latches[frame_index].set_latch();

				if (next_readback_cb)
				{
					worker_threads[frame_index]->set_work([this, cb = next_readback_cb, index = frame_index]() {
						cb(index);
						thread_latches[index].clear_latch();
					});
					next_readback_cb = {};
				}
				else
				{
					worker_threads[frame_index]->set_work([this, index = frame_index, frame = this->frames]() {
						dump_frame(frame, index);
						thread_latches[index].clear_latch();
					});
				}
			}
#ifdef HAVE_GRANITE_FFMPEG
			else if (!video_encode_path.empty())
			{
				acquire_semaphore[frame_index].reset();
				if (!encoder.push_frame(*swapchain_images[frame_index], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				                        Vulkan::CommandBuffer::Type::AsyncGraphics,
				                        release_semaphore, acquire_semaphore[frame_index]))
				{
					LOGE("Failed to push frame to encoder.\n");
					video_encode_path.clear();
				}
			}
#endif
			else
			{
				acquire_semaphore[frame_index] = release_semaphore;
			}
		}

		release_semaphore.reset();
		frame_index = (frame_index + 1) % SwapchainImages;
		frames++;
	}

	void set_next_readback(const std::string &path)
	{
		next_readback_cb = [this, path](unsigned rb_index) {
			auto &wsi = app->get_wsi();
			auto &device = wsi.get_device();

			readback_fence[rb_index]->wait();
			readback_fence[rb_index].reset();

			auto *ptr = static_cast<uint32_t *>(device.map_host_buffer(*readback_buffers[rb_index], MEMORY_ACCESS_READ_WRITE_BIT));
			for (unsigned i = 0; i < width * height; i++)
				ptr[i] |= 0xff000000u;

			if (!stbi_write_png(path.c_str(), width, height, 4, ptr, width * 4))
				LOGE("Failed to write PNG to disk.\n");
			device.unmap_host_buffer(*readback_buffers[rb_index], MEMORY_ACCESS_READ_WRITE_BIT);
		};
	}

	void wait_threads()
	{
		for (auto &thread : worker_threads)
			thread->wait();
#ifdef HAVE_GRANITE_FFMPEG
		encoder.drain();
#endif
	}

private:
	unsigned width = 0;
	unsigned height = 0;
	unsigned frames = 0;
	unsigned max_frames = UINT_MAX;
	unsigned frame_index = 0;
	double time_step = 0.01;
	string png_readback;
	string video_encode_path;
	enum { SwapchainImages = 4 };

	vector<ImageHandle> swapchain_images;
	vector<BufferHandle> readback_buffers;
	vector<Semaphore> acquire_semaphore;
	vector<Fence> readback_fence;
	vector<unique_ptr<FrameWorker>> worker_threads;
	std::function<void (unsigned)> next_readback_cb;
	ThreadLatch thread_latches[SwapchainImages];

#ifdef HAVE_GRANITE_FFMPEG
	VideoEncoder encoder;
#endif

	void dump_frame(unsigned frame, unsigned index)
	{
		auto &wsi = app->get_wsi();
		auto &device = wsi.get_device();

		readback_fence[index]->wait();
		readback_fence[index].reset();

		LOGI("Dumping frame: %u (index: %u)\n", frame, index);

		auto *ptr = static_cast<uint32_t *>(device.map_host_buffer(*readback_buffers[index], MEMORY_ACCESS_READ_WRITE_BIT));
		for (unsigned i = 0; i < width * height; i++)
			ptr[i] |= 0xff000000u;

		char buffer[64];
		sprintf(buffer, "_%05u.png", frame);
		auto path = png_readback + buffer;
		if (!stbi_write_png(path.c_str(), width, height, 4, ptr, width * 4))
			LOGE("Failed to write PNG to disk.\n");
		device.unmap_host_buffer(*readback_buffers[index], MEMORY_ACCESS_READ_WRITE_BIT);
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
int application_main_headless(Application *(*create_application)(int, char **), int argc, char *argv[])
{
	if (argc < 1)
		return 1;

	struct Args
	{
		string png_path;
		string video_encode_path;
		string png_reference_path;
		string stat;
		string assets;
		string cache;
		string builtin;
		unsigned max_frames = UINT_MAX;
		unsigned width = 1280;
		unsigned height = 720;
		double time_step = 0.01;
	} args;

	std::vector<char *> filtered_argv;
	filtered_argv.push_back(argv[0]);

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
	cbs.default_handler = [&](const char *arg) { filtered_argv.push_back(const_cast<char *>(arg)); };
	cbs.error_handler = [&]() { print_help(); };
	CLIParser parser(move(cbs), argc - 1, argv + 1);
	parser.ignore_unknown_arguments();
	if (!parser.parse())
		return 1;
	else if (parser.is_ended_state())
		return 0;

	filtered_argv.push_back(nullptr);

	Granite::Global::init(Granite::Global::MANAGER_FEATURE_DEFAULT_BITS);

	if (!args.assets.empty())
		GRANITE_FILESYSTEM()->register_protocol("assets", make_unique<OSFilesystem>(args.assets));
	if (!args.builtin.empty())
		GRANITE_FILESYSTEM()->register_protocol("builtin", make_unique<OSFilesystem>(args.builtin));
	if (!args.cache.empty())
		GRANITE_FILESYSTEM()->register_protocol("cache", make_unique<OSFilesystem>(args.cache));

	auto app = unique_ptr<Application>(
			create_application(int(filtered_argv.size() - 1), filtered_argv.data()));

	if (app)
	{
		auto platform = make_unique<WSIPlatformHeadless>();
		if (!platform->init(args.width, args.height))
			return 1;

		auto *p = platform.get();

		if (!app->init_wsi(move(platform)))
			return 1;

		if (!args.png_path.empty())
			p->enable_png_readback(args.png_path);
		if (!args.video_encode_path.empty())
			p->enable_video_encode(args.video_encode_path);
		p->set_max_frames(args.max_frames);
		p->set_time_step(args.time_step);
		p->init_headless(app.get());

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
