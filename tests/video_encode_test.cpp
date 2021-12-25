#include "ffmpeg.hpp"
#include "context.hpp"
#include "device.hpp"
#include "math.hpp"
#include "muglm/muglm_impl.hpp"

#ifdef HAVE_GRANITE_AUDIO
#include "audio_interface.hpp"
#include "audio_mixer.hpp"
#include "vorbis_stream.hpp"
#endif
#include "global_managers_init.hpp"

using namespace Granite;

int main()
{
	Global::init(Global::MANAGER_FEATURE_DEFAULT_BITS, 1);

	VideoEncoder::Options options = {};
	options.width = 640;
	options.height = 480;
	options.frame_timebase = { 1, 60 };

	if (!Vulkan::Context::init_loader(nullptr))
		return 1;
	Vulkan::Context ctx;
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return 1;
	Vulkan::Device device;
	device.set_context(ctx);

	VideoEncoder encoder;
#ifdef HAVE_GRANITE_AUDIO
	auto *dump_mixer = new Audio::Mixer;
	auto *audio_dump = new Audio::DumpBackend(dump_mixer, 44100.0f, 2, 256);
	Global::install_audio_system(audio_dump, dump_mixer);
	encoder.set_audio_source(audio_dump);
	Global::start_audio_system();

	auto *stream = Audio::create_vorbis_stream("/tmp/test.ogg");
	if (stream)
		dump_mixer->add_mixer_stream(stream);
	else
		LOGE("Failed to open /tmp/test.ogg.\n");
#endif

	if (!encoder.init(&device, "/tmp/test.mkv", options))
	{
		LOGE("Failed to init codec.\n");
		return 1;
	}

	Vulkan::ImageHandle images[2];
	Vulkan::Semaphore sems[2];
	for (auto &img : images)
	{
		auto info = Vulkan::ImageCreateInfo::render_target(640, 480, VK_FORMAT_R8G8B8A8_SRGB);
		info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		img = device.create_image(info);
	}

	for (unsigned i = 0; i < 1000; i++)
	{
		unsigned wrapped = i % 2;
		if (sems[wrapped])
		{
			device.add_wait_semaphore(Vulkan::CommandBuffer::Type::Generic, sems[wrapped],
			                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, true);
			sems[wrapped].reset();
		}

		auto cmd = device.request_command_buffer();

		cmd->image_barrier(*images[wrapped], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		Vulkan::RenderPassInfo rp;
		rp.color_attachments[0] = &images[wrapped]->get_view();
		rp.num_color_attachments = 1;
		rp.store_attachments = 1;
		rp.clear_attachments = 1;
		rp.clear_color[0].float32[0] = 0.5f;
		rp.clear_color[0].float32[1] = 0.2f;
		rp.clear_color[0].float32[2] = 0.1f;
		cmd->begin_render_pass(rp);

		VkClearValue value = {};
		VkClearRect rect = {};
		value.color.float32[0] = 0.2f;
		value.color.float32[1] = 0.4f;
		value.color.float32[2] = 0.7f;
		rect.layerCount = 1;
		float x = 320.0f + 40.0f * cos(float(i) / 100.0f);
		float y = 240.0f + 40.0f * sin(float(i) / 100.0f);
		rect.rect = {{int(x), int(y)}, {50, 40}};
		cmd->clear_quad(0, rect, value);
		cmd->end_render_pass();

		Vulkan::Semaphore sem;
		device.submit(cmd, nullptr, 1, &sem);

		encoder.push_frame(*images[wrapped], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   Vulkan::CommandBuffer::Type::Generic,
		                   sem, sems[wrapped]);

		device.next_frame_context();
	}

	encoder.drain();

#ifdef HAVE_GRANITE_AUDIO
	Global::stop_audio_system();
#endif

	Global::deinit();
}
