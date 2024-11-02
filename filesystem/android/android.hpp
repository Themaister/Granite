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
#include "../filesystem.hpp"
#include <unordered_map>
#include <android/asset_manager_jni.h>

namespace Granite
{
class AssetFile final : public File
{
public:
	static FileHandle open(AAssetManager *mgr, const std::string &path, FileMode mode);
	~AssetFile() override;
	FileMappingHandle map_subset(uint64_t offset, size_t range) override;
	FileMappingHandle map_write(size_t size) override;
	void unmap(void *mapped, size_t mapped_range) override;
	uint64_t get_size() override;

private:
	bool init(AAssetManager *mgr, const std::string &path, FileMode mode);
	AAsset *asset = nullptr;
	size_t size = 0;
};

class AssetManagerFilesystem : public FilesystemBackend
{
public:
	AssetManagerFilesystem(const std::string &base);
	std::vector<ListEntry> list(const std::string &path) override;
	FileHandle open(const std::string &path, FileMode mode) override;
	bool stat(const std::string &path, FileStat &stat) override;
	FileNotifyHandle install_notification(const std::string &path, std::function<void (const FileNotifyInfo &)> func) override;
	void uninstall_notification(FileNotifyHandle handle) override;
	void poll_notifications() override;
	int get_notification_fd() const override;

	static AAssetManager *global_asset_manager;

private:
	std::string base;
	AAssetManager *mgr;
};
}
