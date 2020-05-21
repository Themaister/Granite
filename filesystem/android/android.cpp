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

#include "android.hpp"
#include "path.hpp"
#include "logging.hpp"
#include <stdexcept>
#include <algorithm>

using namespace std;

namespace Granite
{
bool AssetFile::init(AAssetManager *mgr, const string &path, FileMode mode)
{
	if (mode != FileMode::ReadOnly)
	{
		LOGE("Asset files must be opened read-only.\n");
		return false;
	}

	asset = AAssetManager_open(mgr, path.c_str(), AASSET_MODE_BUFFER);
	if (!asset)
		return false;

	size = AAsset_getLength(asset);
	return true;
}

AssetFile *AssetFile::open(AAssetManager *mgr, const std::string &path, Granite::FileMode mode)
{
	auto *file = new AssetFile();
	if (!file->init(mgr, path, mode))
	{
		delete file;
		return nullptr;
	}
	else
		return file;
}

bool AssetFile::reopen()
{
	return true;
}

void *AssetFile::map()
{
	if (!mapped)
		mapped = const_cast<void *>(AAsset_getBuffer(asset));
	return mapped;
}

size_t AssetFile::get_size()
{
	return size;
}

void *AssetFile::map_write(size_t)
{
	return nullptr;
}

void AssetFile::unmap()
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

unique_ptr<File> AssetManagerFilesystem::open(const std::string &path, FileMode mode)
{
	return unique_ptr<File>(AssetFile::open(mgr, Path::join(base, Path::canonicalize_path(path)), mode));
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

FileNotifyHandle AssetManagerFilesystem::install_notification(const string &,
                                                              function<void (const FileNotifyInfo &)>)
{
	return -1;
}

vector<ListEntry> AssetManagerFilesystem::list(const string &path)
{
	auto directory = Path::join(base, Path::canonicalize_path(path));
	auto *dir = AAssetManager_openDir(mgr, directory.c_str());

	if (!dir)
		return {};

	vector<ListEntry> entries;

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
