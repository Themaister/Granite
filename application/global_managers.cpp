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

#include "global_managers.hpp"

#include "thread_group.hpp"
#include "filesystem.hpp"
#include "event.hpp"
#include "ui_manager.hpp"
#include "common_renderer_data.hpp"
#include <thread>

#ifdef HAVE_GRANITE_AUDIO
#include "audio_interface.hpp"
#include "audio_mixer.hpp"
#include "audio_events.hpp"
#endif

namespace Granite
{
namespace Global
{

// Could use unique_ptr here, but would be nice to avoid global ctor/dtor.
struct GlobalManagers
{
	Filesystem *filesystem;
	EventManager *event_manager;
	ThreadGroup *thread_group;
	UI::UIManager *ui_manager;
	CommonRendererData *common_renderer_data;
#ifdef HAVE_GRANITE_AUDIO
	Audio::Backend *audio_backend;
	Audio::Mixer *audio_mixer;
#endif
};

static GlobalManagers global_managers;

Filesystem *filesystem()
{
	if (!global_managers.filesystem)
	{
		LOGI("Filesystem was not initialized. Lazily initializing. This is not thread safe!\n");
		global_managers.filesystem = new Filesystem;
	}

	return global_managers.filesystem;
}

EventManager *event_manager()
{
	if (!global_managers.event_manager)
	{
		LOGI("Event manager was not initialized. Lazily initializing. This is not thread safe!\n");
		global_managers.event_manager = new EventManager;
	}

	return global_managers.event_manager;
}

ThreadGroup *thread_group()
{
	if (!global_managers.thread_group)
	{
		LOGI("Thread group was not initialized. Lazily initializing. This is not thread safe!\n");
		global_managers.thread_group = new ThreadGroup;
		global_managers.thread_group->start(std::thread::hardware_concurrency());
	}

	return global_managers.thread_group;
}

UI::UIManager *ui_manager()
{
	if (!global_managers.ui_manager)
	{
		LOGI("UI manager was not initialized. Lazily initializing. This is not thread safe!\n");
		global_managers.ui_manager = new UI::UIManager;
	}

	return global_managers.ui_manager;
}

CommonRendererData *common_renderer_data()
{
	if (!global_managers.common_renderer_data)
	{
		LOGI("Common GPU data was not initialized. Lazily initializing. This is not thread safe!\n");
		global_managers.common_renderer_data = new CommonRendererData;
	}

	return global_managers.common_renderer_data;
}

#ifdef HAVE_GRANITE_AUDIO
Audio::Backend *audio_backend() { return global_managers.audio_backend; }
Audio::Mixer *audio_mixer() { return global_managers.audio_mixer; }

void install_audio_system(Audio::Backend *backend, Audio::Mixer *mixer)
{
	if (global_managers.audio_mixer)
		delete global_managers.audio_mixer;
	global_managers.audio_mixer = mixer;

	if (global_managers.audio_backend)
		delete global_managers.audio_backend;
	global_managers.audio_backend = backend;
}
#endif

void init(ManagerFeatureFlags flags)
{
	if (flags & MANAGER_FEATURE_EVENT_BIT)
	{
		if (!global_managers.event_manager)
			global_managers.event_manager = new EventManager;
	}

	if (flags & MANAGER_FEATURE_FILESYSTEM_BIT)
	{
		if (!global_managers.filesystem)
			global_managers.filesystem = new Filesystem;
	}

	if (flags & MANAGER_FEATURE_THREAD_GROUP_BIT)
	{
		if (!global_managers.thread_group)
		{
			global_managers.thread_group = new ThreadGroup;
			global_managers.thread_group->start(std::thread::hardware_concurrency());
		}
	}

	if (flags & MANAGER_FEATURE_UI_MANAGER_BIT)
	{
		if (!global_managers.ui_manager)
			global_managers.ui_manager = new UI::UIManager;
	}

	if (flags & MANAGER_FEATURE_COMMON_RENDERER_DATA_BIT)
	{
		if (!global_managers.common_renderer_data)
			global_managers.common_renderer_data = new CommonRendererData;
	}

#ifdef HAVE_GRANITE_AUDIO
	if (!global_managers.audio_mixer)
		global_managers.audio_mixer = new Audio::Mixer;
	if (!global_managers.audio_backend)
		global_managers.audio_backend = Audio::create_default_audio_backend(*global_managers.audio_mixer, 44100.0f, 2);
#endif
}

void deinit()
{
#ifdef HAVE_GRANITE_AUDIO
	if (global_managers.audio_backend)
		global_managers.audio_backend->stop();

	delete global_managers.audio_backend;
	delete global_managers.audio_mixer;
	global_managers.audio_backend = nullptr;
	global_managers.audio_mixer = nullptr;
#endif

	delete global_managers.common_renderer_data;
	delete global_managers.ui_manager;
	delete global_managers.thread_group;
	delete global_managers.filesystem;
	delete global_managers.event_manager;

	global_managers.common_renderer_data = nullptr;
	global_managers.filesystem = nullptr;
	global_managers.event_manager = nullptr;
	global_managers.thread_group = nullptr;
	global_managers.ui_manager = nullptr;
}

void start_audio_system()
{
#ifdef HAVE_GRANITE_AUDIO
	if (!global_managers.audio_backend)
		return;

	if (!global_managers.audio_backend->start())
	{
		LOGE("Failed to start audio subsystem!\n");
		return;
	}

	if (global_managers.event_manager)
		global_managers.event_manager->enqueue_latched<Audio::MixerStartEvent>(*global_managers.audio_mixer);
#endif
}

void stop_audio_system()
{
#ifdef HAVE_GRANITE_AUDIO
	if (!global_managers.audio_backend)
		return;

	if (!global_managers.audio_backend->stop())
		LOGE("Failed to stop audio subsystem!\n");
	if (global_managers.event_manager)
		global_managers.event_manager->dequeue_latched(Audio::MixerStartEvent::get_type_id());
#endif
}

}
}

