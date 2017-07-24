/* Copyright (c) 2017 Hans-Kristian Arntzen
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
#include "intrusive.hpp"
#include "util.hpp"
#include "path.hpp"
#include <string>

namespace Util
{
using namespace Granite;
template <typename T>
class VolatileSource : public IntrusivePtrEnabled<VolatileSource<T>>
{
public:
	VolatileSource(const std::string &path)
		: path(path)
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

	void init()
	{
		if (path.empty())
			return;

		auto file = Filesystem::get().open(path);
		if (!file)
		{
			LOGE("Failed to open volatile file: %s\n", path.c_str());
			throw std::runtime_error("file open error");
		}

		auto *self = static_cast<T *>(this);
		void *data = file->map();
		size_t size = file->get_size();
		if (data && size)
			self->update(data, size);

		auto paths = Path::protocol_split(path);
		auto *proto = Filesystem::get().get_backend(paths.first);
		if (proto)
		{
			// Listen to directory so we can track file moves properly.
			notify_handle = proto->install_notification(Path::basedir(paths.second), [&](const FileNotifyInfo &info) {
				if (info.type == FileNotifyType::FileDeleted)
					return;
				if (info.path != path)
					return;

				auto *self = static_cast<T *>(this);
				try
				{
					auto file = Filesystem::get().open(info.path);
					if (!file)
						return;

					void *data = file->map();
					size_t size = file->get_size();
					if (data && size)
						self->update(data, size);
					if (data)
						file->unmap();
				}
				catch (const std::exception &e)
				{
					LOGE("Caught update exception: %s\n", e.what());
				}
			});
		}
	}

private:
	void update(const void *data, size_t size);
	Granite::FileNotifyHandle notify_handle = -1;
	Granite::FilesystemBackend *notify_backend = nullptr;
};

template <typename T>
using VolatileHandle = IntrusivePtr<VolatileSource<T>>;
}