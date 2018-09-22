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

#pragma once

#include <stdint.h>

namespace Granite
{
class Filesystem;
class ThreadGroup;
class EventManager;
namespace UI
{
class UIManager;
}

namespace Audio
{
class Backend;
class Mixer;
}

namespace Global
{
enum ManagerFeatureFlagBits
{
	MANAGER_FEATURE_FILESYSTEM_BIT = 1 << 0,
	MANAGER_FEATURE_EVENT_BIT = 1 << 1,
	MANAGER_FEATURE_THREAD_GROUP_BIT = 1 << 2,
	MANAGER_FEATURE_UI_MANAGER_BIT = 1 << 3,
	MANAGER_FEATURE_AUDIO_BIT = 1 << 4,
	MANAGER_FEATURE_ALL_BITS = 0x7fffffff
};
using ManagerFeatureFlags = uint32_t;

void init(ManagerFeatureFlags flags = MANAGER_FEATURE_ALL_BITS);
void deinit();

Filesystem *filesystem();
EventManager *event_manager();
ThreadGroup *thread_group();
UI::UIManager *ui_manager();
#ifdef HAVE_GRANITE_AUDIO
Audio::Backend *audio_backend();
Audio::Mixer *audio_mixer();
#endif
}

}