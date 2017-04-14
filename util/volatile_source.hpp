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