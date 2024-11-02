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

#include "android.hpp"
#include "path_utils.hpp"
#include "logging.hpp"
#include <algorithm>
#include <stdexcept>

namespace Granite
{
bool AssetFile::init(AAssetManager *mgr, const std::string &path, FileMode mode)
{
	if (mode != FileMode::ReadOnly)
	{
		LOGE("Asset files must be opened read-only.\n");
		return false;
	}

	asset = AAssetManager_open(mgr, path.c_str(), AASSET_MODE_BUFFER);
	if (!asset)
		return false;

	size = AAsset_getLength64(asset);
	return true;
}

FileHandle AssetFile::open(AAssetManager *mgr, const std::string &path, Granite::FileMode mode)
{
	auto file = Util::make_handle<AssetFile>();
	if (!file->init(mgr, path, mode))
		file.reset();
	return file;
}

FileMappingHandle AssetFile::map_subset(uint64_t offset, size_t range)
{
	if (offset + range > size)
		return {};

	auto *data = static_cast<uint8_t *>(const_cast<void *>(AAsset_getBuffer(asset)));
	if (!data)
		return {};

	return Util::make_handle<FileMapping>(
			reference_from_this(), offset,
			data + offset, range,
			0, range);
}

uint64_t AssetFile::get_size()
{
	return size;
}

FileMappingHandle AssetFile::map_write(size_t)
{
	return {};
}

void AssetFile::unmap(void *, size_t)
{
}

AssetFile::~AssetFile()
{
	if (asset)
		AAsset_close(asset);
}

AssetManagerFilesystem::AssetManagerFilesystem(const std::string &base_)
	: base(base_), mgr(global_asset_manager)
{
}

FileHandle AssetManagerFilesystem::open(const std::string &path, FileMode mode)
{
	return AssetFile::open(mgr, Path::join(base, Path::canonicalize_path(path)), mode);
}

int AssetManagerFilesystem::get_notification_fd() const
{
	return -1;
}

void AssetManagerFilesystem::poll_notifications()
{
}

void AssetManagerFilesystem::uninstall_notification(FileNotifyHandle)
{
}

FileNotifyHandle AssetManagerFilesystem::install_notification(const std::string &,
                                                              std::function<void (const FileNotifyInfo &)>)
{
	return -1;
}

std::vector<ListEntry> AssetManagerFilesystem::list(const std::string &path)
{
	auto directory = Path::join(base, Path::canonicalize_path(path));
	auto *dir = AAssetManager_openDir(mgr, directory.c_str());

	if (!dir)
		return {};

	std::vector<ListEntry> entries;

	const char *entry;
	while ((entry = AAssetDir_getNextFileName(dir)))
	{
		PathType type = PathType::File;
		entries.push_back({ entry, type });
	}

	AAssetDir_close(dir);
	return entries;
}

bool AssetManagerFilesystem::stat(const std::string &path, FileStat &stat)
{
	auto resolved_path = Path::join(base, Path::canonicalize_path(path));

	auto *asset = AAssetManager_open(mgr, resolved_path.c_str(), AASSET_MODE_UNKNOWN);
	if (!asset)
		return false;

	stat.size = AAsset_getLength(asset);
	stat.type = PathType::File;
	stat.last_modified = 0;
	AAsset_close(asset);
	return true;
}

AAssetManager *AssetManagerFilesystem::global_asset_manager;
}
