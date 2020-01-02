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
#include "fs-netfs.hpp"
#include "network.hpp"
#include "../filesystem.hpp"
#include "netfs.hpp"
#include <unordered_map>
#include <future>
#include <thread>

namespace Granite
{
struct FSReader;
class NetworkFile : public File
{
public:
	static NetworkFile *open(Looper &looper, const std::string &path, FileMode mode);
	~NetworkFile();
	void *map() override;
	void *map_write(size_t size) override;
	void unmap() override;
	size_t get_size() override;
	bool reopen() override;

private:
	NetworkFile() = default;
	bool init(Looper &looper, const std::string &path, FileMode mode);
	std::string path;
	FileMode mode;
	Looper *looper = nullptr;
	std::future<std::vector<uint8_t>> future;
	std::vector<uint8_t> buffer;
	bool has_buffer = false;
	bool need_flush = false;
};

struct FSNotifyCommand;
class NetworkFilesystem : public FilesystemBackend
{
public:
	NetworkFilesystem();
	~NetworkFilesystem();
	std::vector<ListEntry> list(const std::string &path) override;
	std::unique_ptr<File> open(const std::string &path, FileMode mode) override;
	bool stat(const std::string &path, FileStat &stat) override;

	FileNotifyHandle install_notification(const std::string &path, std::function<void (const FileNotifyInfo &)> func) override;

	void uninstall_notification(FileNotifyHandle handle) override;

	void poll_notifications() override;

	int get_notification_fd() const override
	{
		return -1;
	}

private:
	std::thread looper_thread;
	Looper looper;
	void looper_entry();
	FSNotifyCommand *notify = nullptr;

	std::unordered_map<FileNotifyHandle, std::function<void (const FileNotifyInfo &)>> handlers;
	std::mutex lock;
	std::vector<FileNotifyInfo> pending;

	void setup_notification();
	void signal_notification(const FileNotifyInfo &info);
};
}
