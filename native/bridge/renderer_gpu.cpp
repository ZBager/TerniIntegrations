// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
//
// The ID of the GPU a host engine renders with, read through the engine's own graphics
// API objects, so the simulation can run on the same GPU.
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>
#ifdef _MSC_VER
#pragma comment(lib, "dxgi.lib")
#endif
#endif

#include <cstddef>
#include <cstring>
#include <string>
#include <vulkan/vulkan.h>

#include "terni_bridge.h"

namespace terni_bridge {

namespace {
bool any(const uint8_t* bytes, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (bytes[i] != 0) return true;
    return false;
}
}  // namespace

bool vulkan_renderer_gpu(void* instance, void* get_instance_proc_addr, void* physical_device, RendererGpu& gpu) {
    gpu = RendererGpu();
    if (!instance || !physical_device) return false;
    const auto get_proc = get_instance_proc_addr ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(get_instance_proc_addr)
                                                 : &vkGetInstanceProcAddr;
    const auto inst = static_cast<VkInstance>(instance);
    const auto phys = static_cast<VkPhysicalDevice>(physical_device);
    const auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get_proc(inst, "vkGetPhysicalDeviceProperties"));
    if (!properties) return false;
    VkPhysicalDeviceProperties p;
    properties(phys, &p);
    gpu.vendor_id = p.vendorID;
    gpu.name = p.deviceName;
    // The IDs need Vulkan 1.1 (or an extension) on the renderer's instance; without it
    // drivers leave them zero.
    auto properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(get_proc(inst, "vkGetPhysicalDeviceProperties2"));
    if (!properties2)
        properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(get_proc(inst, "vkGetPhysicalDeviceProperties2KHR"));
    if (properties2) {
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        p2.pNext = &id;
        properties2(phys, &p2);
        gpu.has_uuid = any(id.deviceUUID, 16);
        std::memcpy(gpu.uuid, id.deviceUUID, 16);
        gpu.has_luid = id.deviceLUIDValid == VK_TRUE && any(id.deviceLUID, 8);
        std::memcpy(gpu.luid, id.deviceLUID, 8);
    }
    return true;
}

#ifdef _WIN32
namespace {
bool describe(IDXGIAdapter* adapter, RendererGpu& gpu) {
    DXGI_ADAPTER_DESC desc;
    if (FAILED(adapter->GetDesc(&desc))) return false;
    static_assert(sizeof(desc.AdapterLuid) == 8, "an adapter LUID is 8 bytes");
    std::memcpy(gpu.luid, &desc.AdapterLuid, 8);  // LowPart then HighPart, as Vulkan's deviceLUID
    gpu.has_luid = any(gpu.luid, 8);
    gpu.vendor_id = desc.VendorId;
    char name[256];
    const int n = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
    gpu.name = n > 0 ? std::string(name) : std::string();
    return true;
}
}  // namespace
#endif

bool dxgi_renderer_gpu(unsigned adapter_index, RendererGpu& gpu) {
    gpu = RendererGpu();
#ifdef _WIN32
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return false;
    IDXGIAdapter1* adapter = nullptr;
    bool ok = false;
    if (SUCCEEDED(factory->EnumAdapters1(adapter_index, &adapter))) {
        ok = describe(adapter, gpu);
        adapter->Release();
    }
    factory->Release();
    return ok;
#else
    (void)adapter_index;
    return false;
#endif
}

bool dxgi_adapter_gpu(void* adapter, RendererGpu& gpu) {
    gpu = RendererGpu();
#ifdef _WIN32
    return adapter && describe(static_cast<IDXGIAdapter*>(adapter), gpu);
#else
    (void)adapter;
    return false;
#endif
}

}  // namespace terni_bridge
