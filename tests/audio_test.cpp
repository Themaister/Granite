#include "audio_interface.hpp"
#include "timer.hpp"
#include "vorbis_stream.hpp"
#include <chrono>
#include <thread>
#include <cmath>

using namespace Granite::Audio;
using namespace std;

struct SineAudio : BackendCallback
{
	void mix_samples(float * const *channels, size_t num_frames) override
	{
		float *left = channels[0];
		float *right = channels[1];

		for (size_t i = 0; i < num_frames; i++)
		{
			double v = 0.15 * sin(phase);
			*left++ = float(v);
			*right++ = float(v);
			phase += 0.015;
		}
	}

	double phase = 0.0;
};

int main()
{
	SineAudio cb;
	auto stream = create_vorbis_stream("/tmp/test.ogg");

	auto backend = create_default_audio_backend(44100.0f, 2);
	backend->start(stream.get());
	std::this_thread::sleep_for(std::chrono::seconds(100));
	backend->stop();
	std::this_thread::sleep_for(std::chrono::seconds(3));
	backend->start(stream.get());
	std::this_thread::sleep_for(std::chrono::seconds(100));
}