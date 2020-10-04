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

#include "global_managers.hpp"

#include "thread_group.hpp"
#include "filesystem.hpp"
#include "event.hpp"
#include "ui_manager.hpp"
#include "common_renderer_data.hpp"
#include "message_queue.hpp"
#include <thread>

#ifdef HAVE_GRANITE_AUDIO
#include "audio_interface.hpp"
#include "audio_mixer.hpp"
#include "audio_events.hpp"
#endif

#ifdef HAVE_GRANITE_PHYSICS
#include "physics_system.hpp"
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
	Util::MessageQueue *logging;
#ifdef HAVE_GRANITE_AUDIO
	Audio::Backend *audio_backend;
	Audio::Mixer *audio_mixer;
#endif
#ifdef HAVE_GRANITE_PHYSICS
	PhysicsSystem *physics;
#endif
};

static thread_local GlobalManagers global_managers;

GlobalManagersHandle create_thread_context()
{
	return GlobalManagersHandle(new GlobalManagers(global_managers));
}

void delete_thread_context(GlobalManagers *managers)
{
	delete managers;
}

void GlobalManagerDeleter::operator()(GlobalManagers *managers)
{
	delete_thread_context(managers);
}

void set_thread_context(const GlobalManagers &managers)
{
	global_managers = managers;
	if (managers.thread_group)
		managers.thread_group->refresh_global_timeline_trace_file();
}

void clear_thread_context()
{
	global_managers = {};
}

Util::MessageQueue *message_queue()
{
	if (!global_managers.logging)
		global_managers.logging = new Util::MessageQueue;

	return global_managers.logging;
}

Filesystem *filesystem()
{
	if (!global_managers.filesystem)
	{
		LOGI("Filesystem was not initialized. Lazily initializing.\n");
		global_managers.filesystem = new Filesystem;
	}

	return global_managers.filesystem;
}

EventManager *event_manager()
{
	if (!global_managers.event_manager)
	{
		LOGI("Event manager was not initialized. Lazily initializing.\n");
		global_managers.event_manager = new EventManager;
	}

	return global_managers.event_manager;
}

ThreadGroup *thread_group()
{
	if (!global_managers.thread_group)
	{
		LOGI("Thread group was not initialized. Lazily initializing.\n"
		     "This is potentially dangerous if worker threads use globals.\n");
		global_managers.thread_group = new ThreadGroup();
		global_managers.thread_group->start(std::thread::hardware_concurrency());
	}

	return global_managers.thread_group;
}

UI::UIManager *ui_manager()
{
	if (!global_managers.ui_manager)
	{
		LOGI("UI manager was not initialized. Lazily initializing.\n");
		global_managers.ui_manager = new UI::UIManager;
	}

	return global_managers.ui_manager;
}

CommonRendererData *common_renderer_data()
{
	if (!global_managers.common_renderer_data)
	{
		LOGI("Common GPU data was not initialized. Lazily initializing.\n");
		global_managers.common_renderer_data = new CommonRendererData;
	}

	return global_managers.common_renderer_data;
}

#ifdef HAVE_GRANITE_AUDIO
Audio::Backend *audio_backend() { return global_managers.audio_backend; }
Audio::Mixer *audio_mixer() { return global_managers.audio_mixer; }

void install_audio_system(Audio::Backend *backend, Audio::Mixer *mixer)
{
	delete global_managers.audio_mixer;
	global_managers.audio_mixer = mixer;

	delete global_managers.audio_backend;
	global_managers.audio_backend = backend;
}
#endif

#ifdef HAVE_GRANITE_PHYSICS
PhysicsSystem *physics()
{
	if (!global_managers.physics)
	{
		LOGI("Physics system was not initialized. Lazily initializing.\n");
		global_managers.physics = new PhysicsSystem;
	}

	return global_managers.physics;
}
#endif

void init(ManagerFeatureFlags flags, unsigned max_threads)
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

	bool kick_threads = false;
	if (flags & MANAGER_FEATURE_THREAD_GROUP_BIT)
	{
		if (!global_managers.thread_group)
		{
			global_managers.thread_group = new ThreadGroup;
			kick_threads = true;
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

	if (flags & MANAGER_FEATURE_LOGGING_BIT)
	{
		if (!global_managers.logging)
			global_managers.logging = new Util::MessageQueue;
	}

#ifdef HAVE_GRANITE_PHYSICS
	if (flags & MANAGER_FEATURE_PHYSICS_BIT)
	{
		if (!global_managers.physics)
			global_managers.physics = new PhysicsSystem;
	}
#endif

#ifdef HAVE_GRANITE_AUDIO
	if (!global_managers.audio_mixer)
		global_managers.audio_mixer = new Audio::Mixer;
	if (!global_managers.audio_backend)
		global_managers.audio_backend = Audio::create_default_audio_backend(*global_managers.audio_mixer, 44100.0f, 2);
#endif

	// Kick threads after all global managers are set up.
	if (kick_threads)
	{
		unsigned cpu_threads = std::thread::hardware_concurrency();
		if (cpu_threads > max_threads)
			cpu_threads = max_threads;
		if (const char *env = getenv("GRANITE_NUM_WORKER_THREADS"))
			cpu_threads = strtoul(env, nullptr, 0);
		global_managers.thread_group->start(cpu_threads);
	}
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

#ifdef HAVE_GRANITE_PHYSICS
	delete global_managers.physics;
#endif

	delete global_managers.common_renderer_data;
	delete global_managers.ui_manager;
	delete global_managers.thread_group;
	delete global_managers.filesystem;
	delete global_managers.event_manager;
	delete global_managers.logging;

	global_managers.common_renderer_data = nullptr;
	global_managers.filesystem = nullptr;
	global_managers.event_manager = nullptr;
	global_managers.thread_group = nullptr;
	global_managers.ui_manager = nullptr;
	global_managers.logging = nullptr;
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

