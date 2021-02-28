#include "dsp/sinc_resampler.hpp"
#include <stdlib.h>
#include "global_managers_init.hpp"
#include "filesystem.hpp"

using namespace Granite::Audio::DSP;

static void test_reported_sizes()
{
	SincResampler resampler_up(1.1256523423432f, 1.0f, SincResampler::Quality::High);
	SincResampler resampler_down(0.7878237482374f, 1.0f, SincResampler::Quality::High);

	float out_buffer[16 * 1024] = {};
	float in_buffer[16 * 1024] = {};

	for (size_t i = 1; i < 8092; i++)
	{
		{
			size_t max_output = resampler_up.get_maximum_output_for_input_frames(i);
			size_t rendered_output = resampler_up.process_and_accumulate_input_frames(out_buffer, in_buffer, i);
			if (rendered_output > max_output)
				exit(EXIT_FAILURE);
		}

		{
			size_t max_output = resampler_down.get_maximum_output_for_input_frames(i);
			size_t rendered_output = resampler_down.process_and_accumulate_input_frames(out_buffer, in_buffer, i);
			if (rendered_output > max_output)
				exit(EXIT_FAILURE);
		}
	}
}

int main(int argc, char **argv)
{
	test_reported_sizes();
	if (argc != 4)
		return EXIT_FAILURE;

	Granite::Global::init(Granite::Global::MANAGER_FEATURE_FILESYSTEM_BIT);
	auto *fs = GRANITE_FILESYSTEM();
	auto file = fs->open(argv[1], Granite::FileMode::ReadOnly);
	if (!file)
		return EXIT_FAILURE;

	auto out_up = fs->open(argv[2], Granite::FileMode::WriteOnly);
	auto out_down = fs->open(argv[3], Granite::FileMode::WriteOnly);

	SincResampler resampler_up(2.3f, 1.0f, SincResampler::Quality::High);
	SincResampler resampler_down(0.4f, 1.0f, SincResampler::Quality::High);
	size_t num_samples = file->get_size() / sizeof(float);

	size_t required_out_up = resampler_up.get_maximum_output_for_input_frames(num_samples);
	size_t required_out_down = resampler_down.get_maximum_output_for_input_frames(num_samples);

	auto *inputs = static_cast<const float *>(file->map());
	auto *output_up = static_cast<float *>(out_up->map_write(required_out_up * sizeof(float)));
	auto *output_down = static_cast<float *>(out_down->map_write(required_out_down * sizeof(float)));

	for (size_t i = 0; i < num_samples; i += 256)
	{
		size_t to_process = std::min<size_t>(256, num_samples - i);
		output_up += resampler_up.process_input_frames(output_up, inputs, to_process);
		output_down += resampler_down.process_input_frames(output_down, inputs, to_process);
		inputs += to_process;
	}
}