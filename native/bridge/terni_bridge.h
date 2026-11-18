// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
#pragma once
// Terni behind an interface that needs neither C++20 nor exceptions: only standard types
// that every C++14 compiler has, and no call throws. Engine plugins whose engine headers
// do not compile as C++20, or that are built without exceptions, include this header
// and link terni_bridge and terni_runtime.
// Every call that can fail clears error() first and sets it on failure.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace terni_bridge {

struct Device {
    int index = 0;
    std::string name, type, why_not;
    bool cpu = false, usable = false;
    // What a rule that reads or writes past the end of a field does on this device
    // (terni::BufferRobustness): 0 undefined, 1 stays inside the buffer in a way the driver
    // chooses, 2 reads 0 and writes nothing, the same on every device.
    int robustness = 0;
};

struct StartOptions {
    int device = -1;
    int preference = 0;  // terni::DevicePreference
    std::string cache_dir;
    bool cache_only = true;
    bool validation = false;
};

struct Modifier {
    uint32_t slot = 0xFFFFFFFFu, version = 0;
};

struct Contribution {
    Modifier modifier;
    int bundle = 0, entity = 0, row = 0, value = 0;
};

struct Health {
    double usual_ms = 0, recent_ms = 0;
    uint64_t ticks = 0;
    bool slow = false;
};

// A hash of one field, or of one entity type's fields (World::field_hashes()).
struct Hash {
    std::string name;
    uint64_t hash = 0;
};

// GPU time of one stage (World::stage_times()).
struct StageTime {
    std::string name;
    bool ran = false;     // in the last tick
    double last_ms = 0;   // in the last tick
    uint64_t runs = 0;    // ticks it ran in, since the start or the last move_to_device()
    double total_ms = 0;  // over those ticks
};

struct FieldInfo {
    int type = 0;  // terni::FieldType
    int size = 0;  // values per row
    int words = 0; // 32-bit words per value
};

std::vector<Device> devices(std::string& error);
bool can_compile_rules();
// The checksum of the game a save was made with. False if the data is not an intact save.
bool save_checksum(const uint8_t* data, size_t size, uint64_t& checksum, std::string& error);

// ---- the renderer's GPU ----
// What the host's renderer says about its GPU.
struct RendererGpu {
    bool has_uuid = false;  // Vulkan physical device UUID (not all zeros)
    uint8_t uuid[16] = {};
    bool has_luid = false;  // adapter LUID (DirectX, or Vulkan on Windows)
    uint8_t luid[8] = {};
    uint32_t vendor_id = 0;  // PCI vendor ID, and the device's name: the last resort
    std::string name;
};
// From a Vulkan renderer's own objects: VkInstance, its vkGetInstanceProcAddr and the
// VkPhysicalDevice it renders with. A null vkGetInstanceProcAddr means the Vulkan
// loader's, which a renderer that uses the system loader shares. False if they are missing.
bool vulkan_renderer_gpu(void* instance, void* get_instance_proc_addr, void* physical_device, RendererGpu& gpu);
// For a DirectX renderer: DXGI adapter number `adapter_index`. False if there is no such
// adapter, and always on systems other than Windows.
bool dxgi_renderer_gpu(unsigned adapter_index, RendererGpu& gpu);
// For a DirectX renderer that gives its adapter object (an IDXGIAdapter*). Windows only.
bool dxgi_adapter_gpu(void* adapter, RendererGpu& gpu);
// The usable device that is the renderer's GPU, or -1: matched by UUID, then LUID, then by
// vendor and name if exactly one usable device has them.
int find_device(const RendererGpu& gpu, std::string& error);

class World {
public:
    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    const std::string& error() const { return error_; }

    bool load(const std::string& folder, std::vector<std::string>& errors, std::vector<std::string>& warnings);
    // The game's folder with mod folders on top, in order: the game's first.
    bool load(const std::vector<std::string>& folders, std::vector<std::string>& errors, std::vector<std::string>& warnings);
    bool set_capacity(int entity, int rows);
    bool start(const StartOptions& options);
    void stop();
    void unload();  // stop() and forget the game too
    bool loaded() const;
    bool running() const;

    bool tick();
    bool begin_tick();
    bool tick_ready();
    bool end_tick();
    bool tick_in_flight() const;
    uint64_t ticks() const;
    double last_tick_ms() const;
    double last_tick_gpu_ms() const;

    std::vector<std::string> entities() const;
    int find_entity(const std::string& name) const;
    int capacity(int entity) const;
    bool dynamic(int entity) const;
    std::vector<std::string> fields(int entity) const;
    bool field_info(int entity, const std::string& field, FieldInfo& info) const;
    int find_row(int entity, const std::string& id) const;
    std::string row_id(int entity, int row) const;
    bool define(const std::string& name, int32_t& raw) const;
    std::vector<std::string> bundles() const;
    int find_bundle(const std::string& name) const;
    std::vector<std::string> modifier_keys() const;
    int find_modifier_key(const std::string& name) const;
    uint64_t checksum() const;

    bool read_field(int entity, const std::string& field, int first_row, int rows, std::vector<int32_t>& words);
    bool write_field(int entity, const std::string& field, int row, const int32_t* words, size_t count);
    bool write_values(int entity, const std::string& field, int row, int index, const int32_t* words, size_t count);
    int subscribe(int entity, const std::string& field, int first_row, int rows);  // -1: failed
    const int32_t* subscribed(int id, int& words) const;                           // null: failed

    int create_entity(int entity);  // -1: full, or failed
    bool destroy_entity(int entity, int row);
    bool alive(int entity, int row) const;
    int generation(int entity, int row) const;
    int count(int entity) const;
    int alive_count(int entity) const;
    // Of a dynamic entity type: one bit per row (row r is bit r % 32 of word r / 32). Valid
    // until the next call that creates, destroys, ticks or loads. Null: failed.
    const uint32_t* alive_bits(int entity, int& words) const;
    int free_count(int entity);  // rows create_entity() would find now; -1: failed

    Modifier add_modifier(int bundle, int entity, int row, int32_t scale, int duration);
    bool set_modifier_scale(Modifier m, int32_t scale);
    bool remove_modifier(Modifier m);
    bool modifier_alive(Modifier m) const;
    int32_t modifier_total(int key, int entity, int row);
    std::vector<Contribution> modifier_breakdown(int key, int entity, int row);

    bool save(std::vector<uint8_t>& data);
    bool load_save(const uint8_t* data, size_t size);
    // by_name: a save of another version of the game (or other mods) is fitted to this one
    // by name; `fitted` gets one line per thing fitted.
    bool load_save(const uint8_t* data, size_t size, bool by_name, std::vector<std::string>& fitted);
    uint64_t state_hash();
    // Computed on the device: one hash per field, or per entity type. Cheap enough to
    // compare every few ticks. Collect a tick in flight first.
    bool field_hashes(std::vector<Hash>& hashes);
    bool entity_hashes(std::vector<Hash>& hashes);
    std::vector<StageTime> stage_times() const;

    std::string device_name() const;
    Health health() const;
    bool move_to_device(int device);
    std::string report() const;

    // The device was lost (a rule ran too long and the driver reset the GPU, or the GPU
    // went away). The call that found it failed with the reason, and the world stopped;
    // start() again, on this device or another, and load the last save.
    bool device_lost() const;
    // Makes the next call that uses the device find it lost, for testing how a game
    // recovers. Does nothing if the world is not running.
    void simulate_device_lost();

private:
    struct Impl;
    Impl* impl_;
    mutable std::string error_;
};

}  // namespace terni_bridge
