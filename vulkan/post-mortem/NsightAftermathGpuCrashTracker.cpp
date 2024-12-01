//*********************************************************
//
// Copyright (c) 2019-2022, NVIDIA CORPORATION. All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining a
//  copy of this software and associated documentation files (the "Software"),
//  to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  and/or sell copies of the Software, and to permit persons to whom the
//  Software is furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//
//*********************************************************

#include <fstream>
#include <iomanip>
#include <string>
#include <array>
#include "logging.hpp"

#include "NsightAftermathGpuCrashTracker.h"

#ifdef _WIN32
#include <windows.h>
#endif

// Modified from the official sample to fit Granite better.

//*********************************************************
// GpuCrashTracker implementation
//*********************************************************

GpuCrashTracker::GpuCrashTracker(const MarkerMap& markerMap)
	: m_initialized(false)
	, m_mutex()
	, m_shaderDebugInfo()
	, m_markerMap(markerMap)
{
}

GpuCrashTracker::~GpuCrashTracker()
{
	// If initialized, disable GPU crash dumps
	if (m_initialized)
	{
		GFSDK_Aftermath_DisableGpuCrashDumps();
	}
}

// Initialize the GPU Crash Dump Tracker
void GpuCrashTracker::Initialize()
{
	// Enable GPU crash dumps and set up the callbacks for crash dump notifications,
	// shader debug information notifications, and providing additional crash
	// dump description data.Only the crash dump callback is mandatory. The other two
	// callbacks are optional and can be omitted, by passing nullptr, if the corresponding
	// functionality is not used.
	// The DeferDebugInfoCallbacks flag enables caching of shader debug information data
	// in memory. If the flag is set, ShaderDebugInfoCallback will be called only
	// in the event of a crash, right before GpuCrashDumpCallback. If the flag is not set,
	// ShaderDebugInfoCallback will be called for every shader that is compiled.
	AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_EnableGpuCrashDumps(
		GFSDK_Aftermath_Version_API,
		GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan,
		GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks, // Let the Nsight Aftermath library cache shader debug information.
		GpuCrashDumpCallback,                                             // Register callback for GPU crash dumps.
		ShaderDebugInfoCallback,                                          // Register callback for shader debug information.
		CrashDumpDescriptionCallback,                                     // Register callback for GPU crash dump description.
		ResolveMarkerCallback,                                            // Register callback for resolving application-managed markers.
		this));                                                           // Set the GpuCrashTracker object as user data for the above callbacks.

	m_initialized = true;
}

// Handler for GPU crash dump callbacks from Nsight Aftermath
void GpuCrashTracker::OnCrashDump(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize)
{
	// Make sure only one thread at a time...
	std::lock_guard<std::mutex> lock(m_mutex);

	// Write to file for later in-depth analysis with Nsight Graphics.
	WriteGpuCrashDumpToFile(pGpuCrashDump, gpuCrashDumpSize);
}

// Handler for shader debug information callbacks
void GpuCrashTracker::OnShaderDebugInfo(const void* pShaderDebugInfo, const uint32_t shaderDebugInfoSize)
{
	// Make sure only one thread at a time...
	std::lock_guard<std::mutex> lock(m_mutex);

	// Get shader debug information identifier
	GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier = {};
	AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GetShaderDebugInfoIdentifier(
		GFSDK_Aftermath_Version_API,
		pShaderDebugInfo,
		shaderDebugInfoSize,
		&identifier));

	// Store information for decoding of GPU crash dumps with shader address mapping
	// from within the application.
	std::vector<uint8_t> data((uint8_t*)pShaderDebugInfo, (uint8_t*)pShaderDebugInfo + shaderDebugInfoSize);
	m_shaderDebugInfo[identifier].swap(data);

	// Write to file for later in-depth analysis of crash dumps with Nsight Graphics
	WriteShaderDebugInformationToFile(identifier, pShaderDebugInfo, shaderDebugInfoSize);
}

// Handler for GPU crash dump description callbacks
void GpuCrashTracker::OnDescription(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription)
{
	// Add some basic description about the crash. This is called after the GPU crash happens, but before
	// the actual GPU crash dump callback. The provided data is included in the crash dump and can be
	// retrieved using GFSDK_Aftermath_GpuCrashDump_GetDescription().
	addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, "Granite");
	addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationVersion, "v1.0");
}

// Handler for app-managed marker resolve callback
void GpuCrashTracker::OnResolveMarker(const void* pMarkerData, const uint32_t, void** ppResolvedMarkerData, uint32_t* pResolvedMarkerDataSize)
{
	// Important: the pointer passed back via ppResolvedMarkerData must remain valid after this function returns
	// using references for all of the m_markerMap accesses ensures that the pointers refer to the persistent data
	for (auto& map : m_markerMap)
	{
		const auto& foundMarker = map.find((uint64_t)pMarkerData);
		if (foundMarker != map.end())
		{
			const std::string& foundMarkerData = foundMarker->second;
			// std::string::data() will return a valid pointer until the string is next modified
			// we don't modify the string after calling data() here, so the pointer should remain valid
			*ppResolvedMarkerData = (void*)foundMarkerData.data();
			*pResolvedMarkerDataSize = (uint32_t)foundMarkerData.length();
			return;
		}
	}
}

// Helper for writing a GPU crash dump to a file
void GpuCrashTracker::WriteGpuCrashDumpToFile(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize)
{
	// Create a GPU crash dump decoder object for the GPU crash dump.
	GFSDK_Aftermath_GpuCrashDump_Decoder decoder = {};
	AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_CreateDecoder(
		GFSDK_Aftermath_Version_API,
		pGpuCrashDump,
		gpuCrashDumpSize,
		&decoder));

	// Use the decoder object to read basic information, like application
	// name, PID, etc. from the GPU crash dump.
	GFSDK_Aftermath_GpuCrashDump_BaseInfo baseInfo = {};
	AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetBaseInfo(decoder, &baseInfo));

	// Use the decoder object to query the application name that was set
	// in the GPU crash dump description.
	uint32_t applicationNameLength = 0;
	AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetDescriptionSize(
		decoder,
		GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName,
		&applicationNameLength));

	std::vector<char> applicationName(applicationNameLength, '\0');

	AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetDescription(
		decoder,
		GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName,
		uint32_t(applicationName.size()),
		applicationName.data()));

	// Create a unique file name for writing the crash dump data to a file.
	// Note: due to an Nsight Aftermath bug (will be fixed in an upcoming
	// driver release) we may see redundant crash dumps. As a workaround,
	// attach a unique count to each generated file name.
	static int count = 0;
	const std::string baseFileName =
		std::string(applicationName.data())
		+ "-"
		+ std::to_string(baseInfo.pid)
		+ "-"
		+ std::to_string(++count);

	// Write the crash dump data to a file using the .nv-gpudmp extension
	// registered with Nsight Graphics.
	const std::string crashDumpFileName = baseFileName + ".nv-gpudmp";
	std::ofstream dumpFile(crashDumpFileName, std::ios::out | std::ios::binary);
	if (dumpFile)
	{
		dumpFile.write((const char*)pGpuCrashDump, gpuCrashDumpSize);
		dumpFile.close();
		LOGI("Wrote crash dump file to: %s.\n", crashDumpFileName.c_str());
	}

	// Decode the crash dump to a JSON string.
	// Step 1: Generate the JSON and get the size.
	uint32_t jsonSize = 0;
	AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GenerateJSON(
		decoder,
		GFSDK_Aftermath_GpuCrashDumpDecoderFlags_ALL_INFO,
		GFSDK_Aftermath_GpuCrashDumpFormatterFlags_NONE,
		ShaderDebugInfoLookupCallback,
		ShaderLookupCallback,
		ShaderSourceDebugInfoLookupCallback,
		this,
		&jsonSize));

	// Step 2: Allocate a buffer and fetch the generated JSON.
	std::vector<char> json(jsonSize);
	AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetJSON(
		decoder,
		uint32_t(json.size()),
		json.data()));

	// Write the crash dump data as JSON to a file.
	const std::string jsonFileName = crashDumpFileName + ".json";
	std::ofstream jsonFile(jsonFileName, std::ios::out | std::ios::binary);
	if (jsonFile)
	{
		// Write the JSON to the file (excluding string termination)
		jsonFile.write(json.data(), json.size() - 1);
		jsonFile.close();
		LOGI("Wrote crash dump JSON file to: %s.\n", jsonFileName.c_str());
	}

	// Dump active SPIR-V files.
	uint32_t shader_count = 0;
	AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetActiveShadersInfoCount(decoder, &shader_count));
	std::vector<GFSDK_Aftermath_GpuCrashDump_ShaderInfo> shader_infos(shader_count);
	AFTERMATH_CHECK_ERROR(
		GFSDK_Aftermath_GpuCrashDump_GetActiveShadersInfo(decoder, shader_count, shader_infos.data()));

	for (auto &shader : shader_infos)
	{
		GFSDK_Aftermath_ShaderBinaryHash hash;
		AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GetShaderHashForShaderInfo(decoder, &shader, &hash));

		std::lock_guard<std::mutex> holder{ shader_lock };
		auto itr = shader_db.find(hash);
		if (itr != shader_db.end())
		{
			const std::string spirvFilePath = "shader_" + std::to_string(hash) + ".spv";
			std::ofstream spirvFile(spirvFilePath, std::ios::out | std::ios::binary);
			if (spirvFile)
			{
				spirvFile.write((const char *)itr->second.data(), itr->second.size() * sizeof(uint32_t));
				LOGI("Wrote SPIR-V shader file to: %s.\n", spirvFilePath.c_str());
			}
		}
	}

	// Destroy the GPU crash dump decoder object.
	AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_DestroyDecoder(decoder));

#ifdef _WIN32
	char print_buffer[1024];
	char current_dir[1024];

	GetCurrentDirectoryA(sizeof(current_dir), current_dir);
	snprintf(print_buffer, sizeof(print_buffer),
			"GPU hang detected with NV Aftermath. Dump files have been written to %s\\%s. Terminating process ...",
			current_dir, crashDumpFileName.c_str());
	MessageBoxA(nullptr, print_buffer, "VK_ERROR_DEVICE_LOST", MB_OK);
	TerminateProcess(GetCurrentProcess(), 1);
#else
	std::terminate();
#endif
}

void GpuCrashTracker::RegisterShader(const void *code, size_t size)
{
	std::vector<uint32_t> data(static_cast<const uint32_t *>(code),
			static_cast<const uint32_t *>(code) + size / sizeof(uint32_t));

	// Create shader hash for the shader
	const GFSDK_Aftermath_SpirvCode shader{{data.data()}, uint32_t(size)};
	GFSDK_Aftermath_ShaderBinaryHash shaderHash;
	AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GetShaderHashSpirv(
		GFSDK_Aftermath_Version_API,
		&shader,
		&shaderHash));

	// Store the data for shader mapping when decoding GPU crash dumps.
	// cf. FindShaderBinary()
	std::lock_guard<std::mutex> holder{shader_lock};
	shader_db[shaderHash].swap(data);
}

// Helper for writing shader debug information to a file
void GpuCrashTracker::WriteShaderDebugInformationToFile(
		GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier,
		const void* pShaderDebugInfo,
		const uint32_t shaderDebugInfoSize)
{
	// Create a unique file name.
	const std::string filePath = "shader-" + std::to_string(identifier) + ".nvdbg";

	std::ofstream f(filePath, std::ios::out | std::ios::binary);
	if (f)
	{
		f.write((const char*)pShaderDebugInfo, shaderDebugInfoSize);
		LOGI("Wrote shader file to: %s.\n", filePath.c_str());
	}
}

// Handler for shader debug information lookup callbacks.
// This is used by the JSON decoder for mapping shader instruction
// addresses to SPIR-V IL lines or GLSL source lines.
void GpuCrashTracker::OnShaderDebugInfoLookup(
		const GFSDK_Aftermath_ShaderDebugInfoIdentifier& identifier,
		PFN_GFSDK_Aftermath_SetData setShaderDebugInfo) const
{
	auto itr = m_shaderDebugInfo.find(identifier);
	if (itr != m_shaderDebugInfo.end())
		setShaderDebugInfo(itr->second.data(), uint32_t(itr->second.size()));
}

// Handler for shader lookup callbacks.
// This is used by the JSON decoder for mapping shader instruction
// addresses to SPIR-V IL lines or GLSL source lines.
// NOTE: If the application loads stripped shader binaries (ie; --strip-all in spirv-remap),
// Aftermath will require access to both the stripped and the not stripped
// shader binaries.
void GpuCrashTracker::OnShaderLookup(
		const GFSDK_Aftermath_ShaderBinaryHash& shaderHash,
		PFN_GFSDK_Aftermath_SetData setShaderBinary) const
{
	std::lock_guard<std::mutex> holder{shader_lock};
	auto itr = shader_db.find(shaderHash);
	if (itr != shader_db.end())
		setShaderBinary(itr->second.data(), itr->second.size() * sizeof(uint32_t));
}

// Handler for shader source debug info lookup callbacks.
// This is used by the JSON decoder for mapping shader instruction addresses to
// GLSL source lines, if the shaders used by the application were compiled with
// separate debug info data files.
void GpuCrashTracker::OnShaderSourceDebugInfoLookup(
		const GFSDK_Aftermath_ShaderDebugName&,
		PFN_GFSDK_Aftermath_SetData) const
{
	// Granite doesn't do this.
}

// Static callback wrapper for OnCrashDump
void GpuCrashTracker::GpuCrashDumpCallback(
		const void* pGpuCrashDump,
		const uint32_t gpuCrashDumpSize,
		void* pUserData)
{
	auto* pGpuCrashTracker = static_cast<GpuCrashTracker*>(pUserData);
	pGpuCrashTracker->OnCrashDump(pGpuCrashDump, gpuCrashDumpSize);
}

// Static callback wrapper for OnShaderDebugInfo
void GpuCrashTracker::ShaderDebugInfoCallback(
		const void* pShaderDebugInfo,
		const uint32_t shaderDebugInfoSize,
		void* pUserData)
{
	auto* pGpuCrashTracker = static_cast<GpuCrashTracker*>(pUserData);
	pGpuCrashTracker->OnShaderDebugInfo(pShaderDebugInfo, shaderDebugInfoSize);
}

// Static callback wrapper for OnDescription
void GpuCrashTracker::CrashDumpDescriptionCallback(
		PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription,
		void* pUserData)
{
	auto* pGpuCrashTracker = static_cast<GpuCrashTracker*>(pUserData);
	pGpuCrashTracker->OnDescription(addDescription);
}

// Static callback wrapper for OnResolveMarker
void GpuCrashTracker::ResolveMarkerCallback(
		const void* pMarkerData,
		const uint32_t markerDataSize,
		void* pUserData,
		void** ppResolvedMarkerData,
		uint32_t* pResolvedMarkerDataSize)
{
	auto* pGpuCrashTracker = static_cast<GpuCrashTracker*>(pUserData);
	pGpuCrashTracker->OnResolveMarker(pMarkerData, markerDataSize, ppResolvedMarkerData, pResolvedMarkerDataSize);
}

// Static callback wrapper for OnShaderDebugInfoLookup
void GpuCrashTracker::ShaderDebugInfoLookupCallback(
		const GFSDK_Aftermath_ShaderDebugInfoIdentifier* pIdentifier,
		PFN_GFSDK_Aftermath_SetData setShaderDebugInfo,
		void* pUserData)
{
	auto* pGpuCrashTracker = static_cast<GpuCrashTracker*>(pUserData);
	pGpuCrashTracker->OnShaderDebugInfoLookup(*pIdentifier, setShaderDebugInfo);
}

// Static callback wrapper for OnShaderLookup
void GpuCrashTracker::ShaderLookupCallback(
		const GFSDK_Aftermath_ShaderBinaryHash* pShaderHash,
		PFN_GFSDK_Aftermath_SetData setShaderBinary,
		void* pUserData)
{
	auto* pGpuCrashTracker = static_cast<GpuCrashTracker*>(pUserData);
	pGpuCrashTracker->OnShaderLookup(*pShaderHash, setShaderBinary);
}

// Static callback wrapper for OnShaderSourceDebugInfoLookup
void GpuCrashTracker::ShaderSourceDebugInfoLookupCallback(
		const GFSDK_Aftermath_ShaderDebugName* pShaderDebugName,
		PFN_GFSDK_Aftermath_SetData setShaderBinary,
		void* pUserData)
{
	auto* pGpuCrashTracker = static_cast<GpuCrashTracker*>(pUserData);
	pGpuCrashTracker->OnShaderSourceDebugInfoLookup(*pShaderDebugName, setShaderBinary);
}
