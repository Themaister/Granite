#include "audio_mixer.hpp"
#include "audio_interface.hpp"
#include "timer.hpp"
#include "vorbis_stream.hpp"
#include <chrono>
#include <thread>
#include <cmath>
#include "logging.hpp"
#include "global_managers_init.hpp"
#include "filesystem.hpp"
#include "os_filesystem.hpp"
#include "dsp/dsp.hpp"
#include "dsp/sinc_resampler.hpp"

using namespace Granite;
using namespace Granite::Audio;

int main()
{
	Global::init(/*Granite::Global::MANAGER_FEATURE_AUDIO_BIT |*/
	             Granite::Global::MANAGER_FEATURE_FILESYSTEM_BIT);
	GRANITE_FILESYSTEM()->register_protocol("assets", std::make_unique<OSFilesystem>(ASSET_DIRECTORY));

	auto *stream = create_vorbis_stream("assets://test.ogg");
#if 0
	Global::audio_backend()->start();

	StreamID id = 0;
	if (stream)
		id = GRANITE_AUDIO_MIXER()->add_mixer_stream(stream);

	for (unsigned i = 0; i < 100; i++)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		LOGI("Play time: %.3f s\n", GRANITE_AUDIO_MIXER()->get_play_cursor(id));
		size_t write_avail = 0;
		size_t max_write_avail = 0;
		uint32_t latency_usec = 0;
		if (GRANITE_AUDIO_BACKEND()->get_buffer_status(write_avail, max_write_avail, latency_usec))
			LOGI("Avail: %zu, Max avail: %zu, Latency: %u us.\n", write_avail, max_write_avail, latency_usec);
	}

	Global::audio_backend()->stop();
	std::this_thread::sleep_for(std::chrono::seconds(3));
	Global::audio_backend()->start();
	std::this_thread::sleep_for(std::chrono::seconds(4));
#else
	auto *backend = create_default_audio_backend(nullptr, 44100.0f, 2);
	float actual_sample_rate = backend->get_sample_rate();
	float l[256];
	float r[256];

	Granite::Audio::DSP::SincResampler l_resamp(actual_sample_rate, 44100.0f,
	                                            DSP::SincResampler::Quality::Medium);
	Granite::Audio::DSP::SincResampler r_resamp(actual_sample_rate, 44100.0f,
	                                            DSP::SincResampler::Quality::Medium);

	size_t max_output = l_resamp.get_maximum_output_for_input_frames(256);
	std::vector<float> resampled_l(max_output);
	std::vector<float> resampled_r(max_output);
	std::vector<float> interleaved_buffer(2 * max_output);

	stream->setup(44100.0f, 2, 256);
	backend->start();
	for (unsigned i = 0; i < 10000; i++)
	{
		float *channels[2] = { l, r };
		float gains[2] = { 1.0f, 1.0f };
		memset(l, 0, sizeof(l));
		memset(r, 0, sizeof(r));
		size_t read = stream->accumulate_samples(channels, gains, 256);
		if (read < 256)
			break;

		size_t resamp_frames = l_resamp.process_input_frames(resampled_l.data(), l, 256);
		r_resamp.process_input_frames(resampled_r.data(), r, 256);

		Granite::Audio::DSP::interleave_stereo_f32(interleaved_buffer.data(), resampled_l.data(), resampled_r.data(), resamp_frames);
		size_t written = backend->write_frames_interleaved(interleaved_buffer.data(), resamp_frames, true);
		if (written < 256)
			break;

		size_t write_avail = 0;
		size_t max_write_avail = 0;
		uint32_t latency_usec = 0;
		if (backend->get_buffer_status(write_avail, max_write_avail, latency_usec))
			LOGI("Avail: %zu, Max avail: %zu, Latency: %u us.\n", write_avail, max_write_avail, latency_usec);
	}
	backend->stop();
	delete backend;
#endif
}
