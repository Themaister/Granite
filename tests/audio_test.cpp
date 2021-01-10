#include "audio_mixer.hpp"
#include "timer.hpp"
#include "vorbis_stream.hpp"
#include <chrono>
#include <thread>
#include <cmath>
#include "logging.hpp"
#include "global_managers_init.hpp"
#include "filesystem.hpp"
#include "os_filesystem.hpp"

using namespace Granite;
using namespace Granite::Audio;
using namespace std;

int main()
{
	Global::init();
	GRANITE_FILESYSTEM()->register_protocol("assets", make_unique<OSFilesystem>(ASSET_DIRECTORY));

	auto *stream = create_vorbis_stream("assets://test.ogg");
	Global::audio_backend()->start();

	StreamID id = 0;
	if (stream)
		id = GRANITE_AUDIO_MIXER()->add_mixer_stream(stream);

	for (unsigned i = 0; i < 10000; i++)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		LOGI("Play time: %.3f s\n", GRANITE_AUDIO_MIXER()->get_play_cursor(id));
	}

	Global::audio_backend()->stop();
	std::this_thread::sleep_for(std::chrono::seconds(3));
	Global::audio_backend()->start();
	std::this_thread::sleep_for(std::chrono::seconds(100));
}
