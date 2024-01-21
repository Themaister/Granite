/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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

#include "slang_compiler.hpp"
#include "small_vector.hpp"
#include "slang.h"
#include "slang-com-ptr.h"
#include "intrusive.hpp"
#include "path_utils.hpp"

namespace Granite
{
using namespace slang;

template <typename T>
using Ptr = Slang::ComPtr<T>;

SlangCompiler::SlangCompiler(FilesystemInterface &iface_)
	: iface(iface_)
{
}

void SlangCompiler::set_source(std::string path)
{
	source_path = std::move(path);

	auto ext = Path::ext(source_path);
	if (ext == "slang")
	{
		entry_point = "main";
	}
	else
	{
		entry_point = ext;
		source_path.resize(source_path.size() - (ext.size() + 1));
	}
}

void SlangCompiler::add_include_directory(std::string path)
{
	include_dirs.push_back(std::move(path));
}

struct SlangBlob : ISlangBlob, Util::IntrusivePtrEnabled<SlangBlob>
{
	explicit SlangBlob(std::string blob_)
		: blob(std::move(blob_))
	{
	}

	SlangResult queryInterface(const SlangUUID &uuid, void **ptr) SLANG_MCALL  override
	{
		if (uuid == ISlangBlob::getTypeGuid())
		{
			add_reference();
			*ptr = static_cast<ISlangBlob *>(this);
			return SLANG_OK;
		}
		else
			return SLANG_E_NO_INTERFACE;
	}

	uint32_t addRef() SLANG_MCALL override
	{
		add_reference();
		return 1;
	}

	uint32_t release() SLANG_MCALL override
	{
		release_reference();
		return 0;
	}

	const void *getBufferPointer() SLANG_MCALL override
	{
		return blob.data();
	}

	size_t getBufferSize() SLANG_MCALL override
	{
		return blob.size();
	}

	std::string blob;
};

struct FS : ISlangFileSystem, Util::IntrusivePtrEnabled<FS>
{
	explicit FS(FilesystemInterface &iface_)
		: iface(iface_)
	{
	}

	SlangResult loadFile(const char *path, ISlangBlob **outBlob) SLANG_MCALL override
	{
		std::string str;
		if (iface.load_text_file(path, str))
		{
			*outBlob = new SlangBlob(std::move(str));
			dep_paths.insert(path);
			return 0;
		}
		else
			return SLANG_E_NOT_FOUND;
	}

	SlangResult queryInterface(const SlangUUID &uuid, void **ptr) SLANG_MCALL override
	{
		if (uuid == ISlangFileSystem::getTypeGuid())
		{
			add_reference();
			*ptr = static_cast<ISlangFileSystem *>(this);
			return SLANG_OK;
		}
		else if (uuid == ISlangCastable::getTypeGuid())
		{
			add_reference();
			*ptr = static_cast<ISlangCastable *>(this);
			return SLANG_OK;
		}
		else
			return SLANG_E_NO_INTERFACE;
	}

	uint32_t addRef() SLANG_MCALL override
	{
		add_reference();
		return 1;
	}

	uint32_t release() SLANG_MCALL override
	{
		release_reference();
		return 0;
	}

	void *castAs(const SlangUUID &uuid) SLANG_MCALL override
	{
		if (uuid == ISlangFileSystem::getTypeGuid())
			return static_cast<ISlangFileSystem *>(this);
		else if (uuid == ISlangCastable::getTypeGuid())
			return static_cast<ISlangCastable *>(this);
		else
			return nullptr;
	}

	FilesystemInterface &iface;
	std::unordered_set<std::string> dep_paths;
};

struct GlobalSession
{
	IGlobalSession *global;
	~GlobalSession()
	{
		if (global)
			global->release();
	}

	IGlobalSession *get()
	{
		if (!global)
			createGlobalSession(&global);
		return global;
	}
};

static thread_local GlobalSession global_session;

std::vector<uint32_t> SlangCompiler::compile(
		std::string &error_message,
		const std::vector<std::pair<std::string, int>> *defines)
{
	TargetDesc target = {};
	SessionDesc desc = {};
	auto fs = Util::make_handle<FS>(iface);

	Util::SmallVector<const char *> search_paths;
	search_paths.reserve(include_dirs.size());
	for (auto &d : include_dirs)
		search_paths.push_back(d.c_str());

	target.format = SLANG_SPIRV;
	target.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

	Util::SmallVector<PreprocessorMacroDesc> defs;
	Util::SmallVector<std::string> values;
	if (defines)
	{
		defs.reserve(defines->size());
		values.reserve(defines->size());
		for (auto &d : *defines)
		{
			values.push_back(std::to_string(d.second));

			PreprocessorMacroDesc macro = {};
			macro.name = d.first.c_str();
			macro.value = values.back().c_str();
			defs.push_back(macro);
		}
	}

	desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
	desc.targetCount = 1;
	desc.targets = &target;
	desc.searchPaths = search_paths.data();
	desc.searchPathCount = SlangInt(search_paths.size());
	desc.fileSystem = fs.get();
	desc.preprocessorMacroCount = SlangInt(defs.size());
	desc.preprocessorMacros = defs.data();

	Ptr<ISession> session;
	if (SLANG_FAILED(global_session.get()->createSession(desc, session.writeRef())))
	{
		error_message = "Failed to create session.";
		return {};
	}

	Ptr<ISlangBlob> source_blob;

	if (SLANG_FAILED(fs->loadFile(source_path.c_str(), source_blob.writeRef())))
	{
		error_message = "Failed to load file: ";
		error_message += source_path;
		error_message += ".";
		return {};
	}

	Ptr<ISlangBlob> diag;
	Ptr<IModule> module(session->loadModuleFromSource("main", source_path.c_str(), source_blob, diag.writeRef()));

	if (!module)
	{
		error_message = "Failed to load module from source. ";
		if (diag)
		{
			error_message.insert(error_message.end(),
			                     static_cast<const char *>(diag->getBufferPointer()),
			                     static_cast<const char *>(diag->getBufferPointer()) + diag->getBufferSize());
		}
		return {};
	}

	Ptr<IEntryPoint> entry;
	if (SLANG_FAILED(module->findEntryPointByName(entry_point.c_str(), entry.writeRef())))
	{
		error_message = "Failed to find entry point.";
		return {};
	}

	Ptr<IBlob> code, err;
	if (SLANG_FAILED(entry->getEntryPointCode(0, 0, code.writeRef(), err.writeRef())))
	{
		error_message = {
			static_cast<const char *>(err->getBufferPointer()),
			static_cast<const char *>(err->getBufferPointer()) + err->getBufferSize()
		};
		return {};
	}

	dependencies = std::move(fs->dep_paths);

	return {
		static_cast<const uint32_t *>(code->getBufferPointer()),
		static_cast<const uint32_t *>(code->getBufferPointer()) + (code->getBufferSize() / sizeof(uint32_t)),
	};
}

}