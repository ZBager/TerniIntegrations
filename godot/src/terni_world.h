// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

namespace terni_bridge {
class World;
}

namespace godot {

// A Terni simulation: a game folder (schema, rules, starting state) run as compute
// shaders on a Vulkan device of its own, with the same results on every device. load()
// reads the folder, start() runs it, tick() advances it by one tick. Calls that return
// bool return true when they worked; after false, get_last_error() says why (it is also
// pushed as an error). Call everything from one thread.
class TerniWorld : public RefCounted {
    GDCLASS(TerniWorld, RefCounted)

public:
    enum FieldType { FIELD_QUANTITY, FIELD_RATE, FIELD_AGG, FIELD_INT, FIELD_COUNT, FIELD_REF, FIELD_MONEY64 };
    enum DevicePreference { PREFER_DISCRETE, PREFER_INTEGRATED, PREFER_CPU };
    // What a rule that reads or writes past the end of a field does on a device.
    enum BufferRobustness { ROBUSTNESS_NONE, ROBUSTNESS_BOUNDED, ROBUSTNESS_DEFINED };
    static constexpr int64_t QUANTITY_ONE = 1 << 8;
    static constexpr int64_t RATE_ONE = 1 << 16;

    TerniWorld();
    ~TerniWorld();

    // devices and tools
    static Array get_devices();
    static int64_t find_renderer_device();
    static String get_software_driver();
    static void stop_all();
    static void unload_all();
    static Dictionary compile_rules(const String& game_folder, const String& cache_folder, const PackedStringArray& mods);
    static int64_t save_checksum(const PackedByteArray& data);
    static double to_float(int64_t raw, int64_t type);
    static int64_t from_float(double value, int64_t type);
    static int64_t to_int64(int64_t low, int64_t high);

    // loading and running
    bool load(const String& game_folder, const PackedStringArray& mods);
    bool set_capacity(int64_t entity, int64_t rows);
    bool start(const Dictionary& options);
    bool begin_start(const Dictionary& options);
    bool start_ready();
    bool end_start();
    bool is_starting() const;
    void stop();
    bool is_loaded() const;
    bool is_running() const;
    String get_game_folder() const { return folder_; }
    PackedStringArray get_mod_folders() const { return mods_; }
    String get_last_error() const { return last_error_; }
    PackedStringArray get_errors() const { return errors_; }
    PackedStringArray get_warnings() const { return warnings_; }

    // ticks
    bool tick();
    bool begin_tick();
    bool tick_ready();
    bool end_tick();
    bool is_tick_in_flight() const;
    int64_t get_ticks() const;
    double get_last_tick_ms() const;
    double get_last_tick_gpu_ms() const;

    // what the game is made of
    PackedStringArray get_entities() const;
    int64_t find_entity(const String& name) const;
    int64_t entity(const String& name) const;
    int64_t get_capacity(int64_t entity) const;
    bool is_dynamic(int64_t entity) const;
    PackedStringArray get_fields(int64_t entity) const;
    bool has_field(int64_t entity, const String& field) const;
    int64_t get_field_type(int64_t entity, const String& field) const;
    int64_t get_field_size(int64_t entity, const String& field) const;
    int64_t find_row(int64_t entity, const String& id) const;
    String get_row_id(int64_t entity, int64_t row) const;
    Variant get_define(const String& name) const;
    PackedStringArray get_bundles() const;
    int64_t find_bundle(const String& name) const;
    PackedStringArray get_modifier_keys() const;
    int64_t find_modifier_key(const String& name) const;
    int64_t get_checksum() const;

    // fields
    PackedInt32Array read_field(int64_t entity, const String& field, int64_t first_row, int64_t rows);
    bool write_field(int64_t entity, const String& field, int64_t row, const PackedInt32Array& words);
    bool write_value(int64_t entity, const String& field, int64_t row, int64_t raw, int64_t index);
    bool write_values(int64_t entity, const String& field, int64_t row, int64_t index, const PackedInt32Array& words);
    int64_t subscribe(int64_t entity, const String& field, int64_t first_row, int64_t rows);
    PackedInt32Array get_subscribed(int64_t id) const;

    // entities that come and go
    int64_t create_entity(int64_t entity);
    bool destroy_entity(int64_t entity, int64_t row);
    bool is_alive(int64_t entity, int64_t row) const;
    int64_t get_generation(int64_t entity, int64_t row) const;
    int64_t get_count(int64_t entity) const;
    int64_t get_alive_count(int64_t entity) const;
    PackedInt32Array get_alive_bits(int64_t entity) const;
    int64_t get_free_count(int64_t entity);

    // modifiers: a handle is Vector2i(slot, version)
    Vector2i add_modifier(int64_t bundle, int64_t entity, int64_t row, int64_t scale, int64_t duration);
    bool set_modifier_scale(const Vector2i& modifier, int64_t scale);
    bool remove_modifier(const Vector2i& modifier);
    bool is_modifier_alive(const Vector2i& modifier) const;
    int64_t get_modifier_total(int64_t key, int64_t entity, int64_t row);
    Array get_modifier_breakdown(int64_t key, int64_t entity, int64_t row);

    // saves and state
    PackedByteArray save();
    bool load_save(const PackedByteArray& data, bool by_name);
    PackedStringArray get_fitted_lines() const { return fitted_; }
    int64_t get_state_hash();
    Dictionary get_field_hashes();
    Dictionary get_entity_hashes();
    Array get_stage_times() const;

    // the device
    String get_device_name() const;
    Dictionary get_health() const;
    bool move_to_device(int64_t device);
    String get_report() const;
    bool is_device_lost() const;
    void simulate_device_lost();

protected:
    static void _bind_methods();

private:
    struct StartJob;
    terni_bridge::World* bridge_;
    mutable StartJob* start_ = nullptr;
    String folder_;
    PackedStringArray mods_;
    mutable String last_error_;
    PackedStringArray errors_, warnings_, fitted_;
    mutable bool lost_signalled_ = false;

    terni_bridge::World* bridge() const;  // waits for a start in flight
    bool fail(const String& what) const;
    void report() const;                  // records the bridge's error, if the last call failed,
                                          // and signals a lost device
    StartJob* prepare_start(const Dictionary& options);
    bool finish_start() const;
};

}  // namespace godot

VARIANT_ENUM_CAST(TerniWorld::FieldType);
VARIANT_ENUM_CAST(TerniWorld::DevicePreference);
VARIANT_ENUM_CAST(TerniWorld::BufferRobustness);
