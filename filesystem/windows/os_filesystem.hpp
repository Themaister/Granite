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
#include "../filesystem.hpp"
#include <unordered_map>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Granite
{
class MappedFile : public File
{
public:
	static MappedFile *open(const std::string &path, FileMode mode);
	~MappedFile();

	virtual void *map() override;
	virtual void *map_write(size_t size) override;
	virtual void unmap() override;
	virtual size_t get_size() override;
	virtual bool reopen() override;

private:
	bool init(const std::string &path, FileMode mode);
	MappedFile() = default;
	HANDLE file = nullptr;
	void *mapped = nullptr;
	size_t size = 0;
};

class OSFilesystem : public FilesystemBackend
{
public:
	OSFilesystem(const std::string &base);
	~OSFilesystem();
	std::vector<ListEntry> list(const std::string &path) override;
	std::unique_ptr<File> open(const std::string &path, FileMode mode) override;
	bool stat(const std::string &path, FileStat &stat) override;
	FileNotifyHandle install_notification(const std::string &path, std::function<void (const FileNotifyInfo &)> func) override;
	void uninstall_notification(FileNotifyHandle handle) override;
	void poll_notifications() override;
	int get_notification_fd() const override;
	std::string get_filesystem_path(const std::string &path) override;

private:
	std::string base;

	struct Handler
	{
		std::string path;
		std::function<void (const FileNotifyInfo &)> func;
		HANDLE handle = nullptr;
		HANDLE event = nullptr;
		DWORD async_buffer[1024];
		OVERLAPPED overlapped;
	};

	std::unordered_map<FileNotifyHandle, Handler> handlers;
	FileNotifyHandle handle_id = 0;
	void kick_async(Handler &handler);
};
}