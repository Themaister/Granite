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

#include "global_managers.hpp"
#include "environment.hpp"
#include "logging.hpp"
#include <thread>
#include <assert.h>
#include <stdlib.h>

namespace Granite
{
namespace Global
{
// Could use unique_ptr here, but would be nice to avoid global ctor/dtor.
struct GlobalManagers
{
	Factory *factory;

	FilesystemInterface *filesystem;
	AssetManagerInterface *asset_manager;
	MaterialManagerInterface *material_manager;
	EventManagerInterface *event_manager;
	ThreadGroupInterface *thread_group;
	UI::UIManagerInterface *ui_manager;
	CommonRendererDataInterface *common_renderer_data;
	Util::MessageQueueInterface *logging;
	Audio::BackendInterface *audio_backend;
	Audio::MixerInterface *audio_mixer;
	PhysicsSystemInterface *physics;
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
		managers.thread_group->set_thread_context();
	if (managers.logging)
		Util::set_thread_logging_interface(managers.logging);
}

void clear_thread_context()
{
	global_managers = {};
}

Util::MessageQueueInterface *message_queue()
{
	return global_managers.logging;
}

FilesystemInterface *filesystem()
{
	return global_managers.filesystem;
}

AssetManagerInterface *asset_manager()
{
	return global_managers.asset_manager;
}

MaterialManagerInterface *material_manager()
{
	return global_managers.material_manager;
}

EventManagerInterface *event_manager()
{
	return global_managers.event_manager;
}

ThreadGroupInterface *thread_group()
{
	return global_managers.thread_group;
}

UI::UIManagerInterface *ui_manager()
{
	return global_managers.ui_manager;
}

CommonRendererDataInterface *common_renderer_data()
{
	return global_managers.common_renderer_data;
}

Audio::BackendInterface *audio_backend() { return global_managers.audio_backend; }
Audio::MixerInterface *audio_mixer() { return global_managers.audio_mixer; }

void install_audio_system(Audio::BackendInterface *backend, Audio::MixerInterface *mixer)
{
	delete global_managers.audio_mixer;
	global_managers.audio_mixer = mixer;
	delete global_managers.audio_backend;
	global_managers.audio_backend = backend;
}

PhysicsSystemInterface *physics()
{
	return global_managers.physics;
}

void init(Factory &factory, ManagerFeatureFlags flags, unsigned max_threads, float audio_sample_rate)
{
	assert(!global_managers.factory || global_managers.factory == &factory);
	global_managers.factory = &factory;

	if (flags & MANAGER_FEATURE_EVENT_BIT)
	{
		if (!global_managers.event_manager)
			global_managers.event_manager = factory.create_event_manager();
	}

	if (flags & MANAGER_FEATURE_FILESYSTEM_BIT)
	{
		if (!global_managers.filesystem)
			global_managers.filesystem = factory.create_filesystem();
	}

	if (flags & MANAGER_FEATURE_ASSET_MANAGER_BIT)
	{
		if (!global_managers.asset_manager)
			global_managers.asset_manager = factory.create_asset_manager();
	}

	if (flags & MANAGER_FEATURE_MATERIAL_MANAGER_BIT)
	{
		if (!global_managers.material_manager)
			global_managers.material_manager = factory.create_material_manager();
	}

	bool kick_threads = false;
	if (flags & MANAGER_FEATURE_THREAD_GROUP_BIT)
	{
		if (!global_managers.thread_group)
		{
			global_managers.thread_group = factory.create_thread_group();
			kick_threads = true;
		}
	}

	if (flags & MANAGER_FEATURE_UI_MANAGER_BIT)
	{
		if (!global_managers.ui_manager)
			global_managers.ui_manager = factory.create_ui_manager();
	}

	if (flags & MANAGER_FEATURE_COMMON_RENDERER_DATA_BIT)
	{
		if (!global_managers.common_renderer_data)
			global_managers.common_renderer_data = factory.create_common_renderer_data();
	}

	if (flags & MANAGER_FEATURE_LOGGING_BIT)
	{
		if (!global_managers.logging)
			global_managers.logging = factory.create_message_queue();
		Util::set_thread_logging_interface(global_managers.logging);
	}

	if (flags & MANAGER_FEATURE_PHYSICS_BIT)
	{
		if (!global_managers.physics)
			global_managers.physics = factory.create_physics_system();
	}

	if (flags & MANAGER_FEATURE_AUDIO_MIXER_BIT)
	{
		if (!global_managers.audio_mixer)
			global_managers.audio_mixer = factory.create_audio_mixer();
	}

	if (flags & MANAGER_FEATURE_AUDIO_BACKEND_BIT)
	{
		if (!global_managers.audio_backend)
			global_managers.audio_backend = factory.create_audio_backend(global_managers.audio_mixer, audio_sample_rate, 2);
	}

	// Kick threads after all global managers are set up.
	if (kick_threads)
	{
		unsigned cpu_threads = std::thread::hardware_concurrency();
		cpu_threads = cpu_threads > 1 ? (cpu_threads - 1u) : 1u;

		if (cpu_threads > max_threads)
			cpu_threads = max_threads;
		cpu_threads = Util::get_environment_uint("GRANITE_NUM_WORKER_THREADS", cpu_threads);

		unsigned background_cpu_threads = (cpu_threads + 1) / 2;

		global_managers.thread_group->start(cpu_threads, background_cpu_threads,
		                                    [ctx = std::shared_ptr<GlobalManagers>(create_thread_context())] {
			                                    set_thread_context(*ctx);
		                                    });
	}
}

void deinit()
{
	if (!global_managers.factory)
		return;

	if (global_managers.audio_backend)
		global_managers.audio_backend->stop();

	delete global_managers.audio_backend;
	delete global_managers.audio_mixer;
	delete global_managers.physics;
	delete global_managers.common_renderer_data;
	delete global_managers.ui_manager;
	delete global_managers.thread_group;
	delete global_managers.material_manager;
	delete global_managers.asset_manager;
	delete global_managers.filesystem;
	delete global_managers.event_manager;
	delete global_managers.logging;

	global_managers.audio_backend = nullptr;
	global_managers.audio_mixer = nullptr;
	global_managers.physics = nullptr;
	global_managers.common_renderer_data = nullptr;
	global_managers.filesystem = nullptr;
	global_managers.material_manager = nullptr;
	global_managers.asset_manager = nullptr;
	global_managers.event_manager = nullptr;
	global_managers.thread_group = nullptr;
	global_managers.ui_manager = nullptr;
	global_managers.logging = nullptr;

	global_managers.factory = nullptr;
}

void start_audio_system()
{
	if (!global_managers.audio_backend)
		return;

	if (!global_managers.audio_backend->start())
	{
		LOGE("Failed to start audio subsystem!\n");
		return;
	}

	if (global_managers.event_manager && global_managers.audio_mixer)
		global_managers.audio_mixer->event_start(*global_managers.event_manager);
}

void stop_audio_system()
{
	if (!global_managers.audio_backend)
		return;

	if (!global_managers.audio_backend->stop())
		LOGE("Failed to stop audio subsystem!\n");

	if (global_managers.event_manager && global_managers.audio_mixer)
		global_managers.audio_mixer->event_stop(*global_managers.event_manager);
}

FilesystemInterface *Factory::create_filesystem() { return nullptr; }
AssetManagerInterface *Factory::create_asset_manager() { return nullptr; }
MaterialManagerInterface *Factory::create_material_manager() { return nullptr; }
EventManagerInterface *Factory::create_event_manager() { return nullptr; }
ThreadGroupInterface *Factory::create_thread_group() { return nullptr; }
CommonRendererDataInterface *Factory::create_common_renderer_data() { return nullptr; }
PhysicsSystemInterface *Factory::create_physics_system() { return nullptr; }
Audio::BackendInterface *Factory::create_audio_backend(Audio::MixerInterface *, float, unsigned) { return nullptr; }
Audio::MixerInterface *Factory::create_audio_mixer() { return nullptr; }
UI::UIManagerInterface *Factory::create_ui_manager() { return nullptr; }
Util::MessageQueueInterface *Factory::create_message_queue() { return nullptr; }
}
}
