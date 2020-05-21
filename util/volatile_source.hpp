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

#pragma once

#include "filesystem.hpp"
#include "global_managers.hpp"
#include "intrusive.hpp"
#include "path.hpp"
#include "logging.hpp"
#include <string>

namespace Util
{
template <typename T>
class VolatileSource : public IntrusivePtrEnabled<VolatileSource<T>>
{
public:
	explicit VolatileSource(const std::string &path_)
		: path(Granite::Path::enforce_protocol(path_))
	{
	}

	VolatileSource() = default;

	~VolatileSource()
	{
		deinit();
	}

protected:
	std::string path;
	void deinit()
	{
		if (notify_backend && notify_handle >= 0)
			notify_backend->uninstall_notification(notify_handle);
		notify_backend = nullptr;
		notify_handle = -1;
	}

	bool init()
	{
		if (path.empty())
			return false;

		auto file = Granite::Global::filesystem()->open(path);
		if (!file)
		{
			LOGE("Failed to open volatile file: %s\n", path.c_str());
			return false;
		}

		auto *self = static_cast<T *>(this);
		self->update(move(file));

		auto paths = Granite::Path::protocol_split(path);
		auto *proto = Granite::Global::filesystem()->get_backend(paths.first);
		if (proto)
		{
			// Listen to directory so we can track file moves properly.
			notify_handle = proto->install_notification(Granite::Path::basedir(paths.second), [&](const Granite::FileNotifyInfo &info) {
				if (info.type == Granite::FileNotifyType::FileDeleted)
					return;
				if (info.path != path)
					return;

				try
				{
					auto f = Granite::Global::filesystem()->open(info.path);
					if (!file)
						return;
					auto *s = static_cast<T *>(this);
					s->update(std::move(f));
				}
				catch (const std::exception &e)
				{
					LOGE("Caught update exception: %s\n", e.what());
				}
			});
		}

		return true;
	}

private:
	Granite::FileNotifyHandle notify_handle = -1;
	Granite::FilesystemBackend *notify_backend = nullptr;
};

template <typename T>
using VolatileHandle = IntrusivePtr<VolatileSource<T>>;
}