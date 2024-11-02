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
#include "filesystem.hpp"
#include <unordered_map>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Granite
{
class MappedFile final : public File
{
public:
	static FileHandle open(const std::string &path, FileMode mode);
	~MappedFile() override;

	FileMappingHandle map_subset(uint64_t offset, size_t range) override;
	FileMappingHandle map_write(size_t size) override;
	void unmap(void *mapped, size_t range) override;
	uint64_t get_size() override;

private:
	bool init(const std::string &path, FileMode mode);
	HANDLE file = INVALID_HANDLE_VALUE;
	HANDLE file_mapping = nullptr;
	uint64_t size = 0;
	std::string rename_from_on_close;
	std::string rename_to_on_close;
};

class OSFilesystem : public FilesystemBackend
{
public:
	OSFilesystem(const std::string &base);
	~OSFilesystem();
	std::vector<ListEntry> list(const std::string &path) override;
	FileHandle open(const std::string &path, FileMode mode) override;
	bool stat(const std::string &path, FileStat &stat) override;
	FileNotifyHandle install_notification(const std::string &path, std::function<void (const FileNotifyInfo &)> func) override;
	void uninstall_notification(FileNotifyHandle handle) override;
	void poll_notifications() override;
	int get_notification_fd() const override;
	std::string get_filesystem_path(const std::string &path) override;

	bool remove(const std::string &str) override;
	bool move_yield(const std::string &dst, const std::string &src) override;
	bool move_replace(const std::string &dst, const std::string &src) override;

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
