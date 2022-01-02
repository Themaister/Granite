/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#include "global_managers_init.hpp"
#include "global_managers.hpp"
#include "event.hpp"
#include "thread_group.hpp"
#include "filesystem.hpp"
#include "common_renderer_data.hpp"
#include "ui_manager.hpp"
#ifdef HAVE_GRANITE_AUDIO
#include "audio_mixer.hpp"
#include "audio_interface.hpp"
#endif
#ifdef HAVE_GRANITE_PHYSICS
#include "physics_system.hpp"
#endif

namespace Granite
{
namespace Global
{
struct FactoryImplementation : Factory
{
	FilesystemInterface *create_filesystem() override
	{
		return new Filesystem;
	}

	EventManagerInterface *create_event_manager() override
	{
		return new EventManager;
	}

	ThreadGroupInterface *create_thread_group() override
	{
		return new ThreadGroup;
	}

	CommonRendererDataInterface *create_common_renderer_data() override
	{
		return new CommonRendererData;
	}

	UI::UIManagerInterface *create_ui_manager() override
	{
		return new UI::UIManager;
	}

	Audio::MixerInterface *create_audio_mixer() override
	{
#ifdef HAVE_GRANITE_AUDIO
		return new Audio::Mixer;
#else
		return nullptr;
#endif
	}

	Audio::BackendInterface *create_audio_backend(Audio::MixerInterface *iface, float sample_rate, unsigned channels) override
	{
#ifdef HAVE_GRANITE_AUDIO
		if (iface)
			return Audio::create_default_audio_backend(static_cast<Audio::Mixer *>(iface), sample_rate, channels);
		else
			return nullptr;
#else
		(void)iface;
		(void)sample_rate;
		(void)channels;
		return nullptr;
#endif
	}

	PhysicsSystemInterface *create_physics_system() override
	{
#ifdef HAVE_GRANITE_PHYSICS
		return new PhysicsSystem;
#else
		return nullptr;
#endif
	}
};

static FactoryImplementation factory;

void init(ManagerFeatureFlags flags, unsigned max_threads)
{
	init(factory, flags, max_threads);
}
}
}

