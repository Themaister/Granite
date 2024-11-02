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

#pragma once

#include <stdint.h>
#include <functional>
#include <memory>
#include <limits.h>
#include "global_managers_interface.hpp"

namespace Granite
{
namespace Global
{
enum ManagerFeatureFlagBits
{
	MANAGER_FEATURE_FILESYSTEM_BIT = 1 << 0,
	MANAGER_FEATURE_EVENT_BIT = 1 << 1,
	MANAGER_FEATURE_THREAD_GROUP_BIT = 1 << 2,
	MANAGER_FEATURE_UI_MANAGER_BIT = 1 << 3,
	MANAGER_FEATURE_AUDIO_MIXER_BIT = 1 << 4,
	MANAGER_FEATURE_AUDIO_BACKEND_BIT = 1 << 5,
	MANAGER_FEATURE_COMMON_RENDERER_DATA_BIT = 1 << 6,
	MANAGER_FEATURE_PHYSICS_BIT = 1 << 7,
	MANAGER_FEATURE_LOGGING_BIT = 1 << 8,
	MANAGER_FEATURE_ASSET_MANAGER_BIT = 1 << 9,
	MANAGER_FEATURE_MATERIAL_MANAGER_BIT = 1 << 10,
	MANAGER_FEATURE_DEFAULT_BITS = (MANAGER_FEATURE_FILESYSTEM_BIT |
	                                MANAGER_FEATURE_ASSET_MANAGER_BIT |
	                                MANAGER_FEATURE_MATERIAL_MANAGER_BIT |
	                                MANAGER_FEATURE_EVENT_BIT |
	                                MANAGER_FEATURE_THREAD_GROUP_BIT |
	                                MANAGER_FEATURE_COMMON_RENDERER_DATA_BIT |
	                                MANAGER_FEATURE_UI_MANAGER_BIT |
	                                MANAGER_FEATURE_AUDIO_MIXER_BIT |
	                                MANAGER_FEATURE_AUDIO_BACKEND_BIT)
};
using ManagerFeatureFlags = uint32_t;

// Decouple creation from global TLS storage.
// This avoids some nasty cyclical dependencies.
class Factory
{
public:
	virtual ~Factory() = default;

	virtual FilesystemInterface *create_filesystem();
	virtual AssetManagerInterface *create_asset_manager();
	virtual MaterialManagerInterface *create_material_manager();
	virtual EventManagerInterface *create_event_manager();
	virtual ThreadGroupInterface *create_thread_group();
	virtual CommonRendererDataInterface *create_common_renderer_data();
	virtual PhysicsSystemInterface *create_physics_system();
	virtual Audio::BackendInterface *create_audio_backend(Audio::MixerInterface *mixer,
	                                                      float sample_rate,
	                                                      unsigned channels);
	virtual Audio::MixerInterface *create_audio_mixer();
	virtual UI::UIManagerInterface *create_ui_manager();
	virtual Util::MessageQueueInterface *create_message_queue();
};

void init(Factory &factory, ManagerFeatureFlags flags = MANAGER_FEATURE_DEFAULT_BITS,
          unsigned max_threads = UINT_MAX, float audio_sample_rate = -1.0f);
void deinit();

// Used if the application wants to use multiple instances of Granite in the same process.
// This allows each thread to be associated to a global context.
struct GlobalManagers;
struct GlobalManagerDeleter
{
	void operator()(GlobalManagers *managers);
};
using GlobalManagersHandle = std::unique_ptr<GlobalManagers, GlobalManagerDeleter>;
GlobalManagersHandle create_thread_context();
void delete_thread_context(GlobalManagers *managers);
void set_thread_context(const GlobalManagers &managers);
void clear_thread_context();

void start_audio_system();
void stop_audio_system();
void install_audio_system(Audio::BackendInterface *backend, Audio::MixerInterface *mixer);

Util::MessageQueueInterface *message_queue();
FilesystemInterface *filesystem();
AssetManagerInterface *asset_manager();
MaterialManagerInterface *material_manager();
EventManagerInterface *event_manager();
ThreadGroupInterface *thread_group();
UI::UIManagerInterface *ui_manager();
CommonRendererDataInterface *common_renderer_data();
Audio::BackendInterface *audio_backend();
Audio::MixerInterface *audio_mixer();
PhysicsSystemInterface *physics();
}
}

#define GRANITE_MESSAGE_QUEUE() static_cast<::Util::MessageQueue *>(::Granite::Global::message_queue())
#define GRANITE_FILESYSTEM() static_cast<::Granite::Filesystem *>(::Granite::Global::filesystem())
#define GRANITE_ASSET_MANAGER() static_cast<::Granite::AssetManager *>(::Granite::Global::asset_manager())
#define GRANITE_MATERIAL_MANAGER() static_cast<::Granite::MaterialManager *>(::Granite::Global::material_manager())
#define GRANITE_EVENT_MANAGER() static_cast<::Granite::EventManager *>(::Granite::Global::event_manager())
#define GRANITE_THREAD_GROUP() static_cast<::Granite::ThreadGroup *>(::Granite::Global::thread_group())
#define GRANITE_UI_MANAGER() static_cast<::Granite::UI::UIManager *>(::Granite::Global::ui_manager())
#define GRANITE_COMMON_RENDERER_DATA() static_cast<::Granite::CommonRendererData *>(::Granite::Global::common_renderer_data())
#define GRANITE_AUDIO_BACKEND() static_cast<::Granite::Audio::Backend *>(::Granite::Global::audio_backend())
#define GRANITE_AUDIO_MIXER() static_cast<::Granite::Audio::Mixer *>(::Granite::Global::audio_mixer())
#define GRANITE_PHYSICS() static_cast<::Granite::PhysicsSystem *>(::Granite::Global::physics())
