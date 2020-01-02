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

namespace Granite
{
class MMapFile : public File
{
public:
	static MMapFile *open(const std::string &path, FileMode mode);
	~MMapFile();
	void *map() override;
	void *map_write(size_t map_size) override;
	void unmap() override;
	size_t get_size() override;
	bool reopen() override;

private:
	MMapFile() = default;
	bool init(const std::string &path, FileMode mode);
	int fd = -1;
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

	struct VirtualHandler
	{
		std::string path;
		std::function<void (const FileNotifyInfo &)> func;
		FileNotifyHandle virtual_handle;
	};

	struct Handler
	{
		std::vector<VirtualHandler> funcs;
		bool directory;
	};
	std::unordered_map<FileNotifyHandle, Handler> handlers;
	std::unordered_map<FileNotifyHandle, FileNotifyHandle> virtual_to_real;
	int notify_fd;
	FileNotifyHandle virtual_handle = 0;
};
}
