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

#pragma once

#include <vulkan/vulkan.h>
#include <map>
#include <mutex>
#include <vector>
#include <array>

#include "NsightAftermathHelpers.h"

//*********************************************************
// Implements GPU crash dump tracking using the Nsight
// Aftermath API.
//
class GpuCrashTracker
{
public:
    // keep four frames worth of marker history
    const static unsigned int c_markerFrameHistory = 4;
    typedef std::array<std::map<uint64_t, std::string>, c_markerFrameHistory> MarkerMap;

    GpuCrashTracker(const MarkerMap& markerMap);
    ~GpuCrashTracker();

    // Initialize the GPU crash dump tracker.
    void Initialize();

	void RegisterShader(const void *code, size_t size);

private:

    //*********************************************************
    // Callback handlers for GPU crash dumps and related data.
    //

    // Handler for GPU crash dump callbacks.
    void OnCrashDump(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize);

    // Handler for shader debug information callbacks.
    void OnShaderDebugInfo(const void* pShaderDebugInfo, const uint32_t shaderDebugInfoSize);

    // Handler for GPU crash dump description callbacks.
    void OnDescription(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription);

    // Handler for app-managed marker resolve callback
    void OnResolveMarker(const void* pMarkerData, const uint32_t markerDataSize, void** ppResolvedMarkerData, uint32_t* pResolvedMarkerDataSize);

    //*********************************************************
    // Helpers for writing a GPU crash dump and debug information
    // data to files.
    //

    // Helper for writing a GPU crash dump to a file.
    void WriteGpuCrashDumpToFile(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize);

    // Helper for writing shader debug information to a file
    void WriteShaderDebugInformationToFile(
        GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier,
        const void* pShaderDebugInfo,
        const uint32_t shaderDebugInfoSize);

    //*********************************************************
    // Helpers for decoding GPU crash dump to JSON.
    //

    // Handler for shader debug info lookup callbacks.
    void OnShaderDebugInfoLookup(
        const GFSDK_Aftermath_ShaderDebugInfoIdentifier& identifier,
        PFN_GFSDK_Aftermath_SetData setShaderDebugInfo) const;

    // Handler for shader lookup callbacks.
    void OnShaderLookup(
        const GFSDK_Aftermath_ShaderBinaryHash& shaderHash,
        PFN_GFSDK_Aftermath_SetData setShaderBinary) const;

    // Handler for shader source debug info lookup callbacks.
    void OnShaderSourceDebugInfoLookup(
        const GFSDK_Aftermath_ShaderDebugName& shaderDebugName,
        PFN_GFSDK_Aftermath_SetData setShaderBinary) const;

    //*********************************************************
    // Static callback wrappers.
    //

    // GPU crash dump callback.
    static void GpuCrashDumpCallback(
        const void* pGpuCrashDump,
        const uint32_t gpuCrashDumpSize,
        void* pUserData);

    // Shader debug information callback.
    static void ShaderDebugInfoCallback(
        const void* pShaderDebugInfo,
        const uint32_t shaderDebugInfoSize,
        void* pUserData);

    // GPU crash dump description callback.
    static void CrashDumpDescriptionCallback(
        PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription,
        void* pUserData);

    // App-managed marker resolve callback
    static void ResolveMarkerCallback(
        const void* pMarkerData,
        const uint32_t markerDataSize,
        void* pUserData,
        void** ppResolvedMarkerData,
        uint32_t* pResolvedMarkerDataSize);

    // Shader debug information lookup callback.
    static void ShaderDebugInfoLookupCallback(
        const GFSDK_Aftermath_ShaderDebugInfoIdentifier* pIdentifier,
        PFN_GFSDK_Aftermath_SetData setShaderDebugInfo,
        void* pUserData);

    // Shader lookup callback.
    static void ShaderLookupCallback(
        const GFSDK_Aftermath_ShaderBinaryHash* pShaderHash,
        PFN_GFSDK_Aftermath_SetData setShaderBinary,
        void* pUserData);

    // Shader source debug info lookup callback.
    static void ShaderSourceDebugInfoLookupCallback(
        const GFSDK_Aftermath_ShaderDebugName* pShaderDebugName,
        PFN_GFSDK_Aftermath_SetData setShaderBinary,
        void* pUserData);

    //*********************************************************
    // GPU crash tracker state.
    //

    // Is the GPU crash dump tracker initialized?
    bool m_initialized;

    // For thread-safe access of GPU crash tracker state.
    mutable std::mutex m_mutex;

    // List of Shader Debug Information by ShaderDebugInfoIdentifier.
    std::map<GFSDK_Aftermath_ShaderDebugInfoIdentifier, std::vector<uint8_t>> m_shaderDebugInfo;

    // App-managed marker tracking
    const MarkerMap& m_markerMap;

	mutable std::mutex shader_lock;
	std::map<GFSDK_Aftermath_ShaderBinaryHash, std::vector<uint32_t>> shader_db;
};
