// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
#pragma once

#include <godot_cpp/classes/node.hpp>

#include "terni_world.h"

namespace godot {

// Runs a Terni game folder from a scene: loads it, starts it on a device and ticks it at
// a steady rate without holding up frames. Other nodes use get_world() to read and write
// the simulation, and the signals to follow it. Does nothing in the editor.
class TerniSimulation : public Node {
    GDCLASS(TerniSimulation, Node)

public:
    bool start_world();
    void stop_world();
    bool step();

    Ref<TerniWorld> get_world() const { return world_; }
    bool is_running() const { return world_.is_valid() && world_->is_running(); }
    bool is_starting() const { return world_.is_valid() && world_->is_starting(); }
    int64_t get_ticks() const { return world_.is_valid() ? world_->get_ticks() : 0; }
    String get_last_error() const { return last_error_; }
    bool is_device_lost() const { return world_.is_valid() && world_->is_device_lost(); }

    void _ready() override;
    void _exit_tree() override;
    void _process(double delta) override;

    // properties
    void set_game_folder(const String& v) { game_folder_ = v; }
    String get_game_folder() const { return game_folder_; }
    void set_mod_folders(const PackedStringArray& v) { mod_folders_ = v; }
    PackedStringArray get_mod_folders() const { return mod_folders_; }
    void set_cache_folder(const String& v) { cache_folder_ = v; }
    String get_cache_folder() const { return cache_folder_; }
    void set_auto_start(bool v) { auto_start_ = v; }
    bool get_auto_start() const { return auto_start_; }
    void set_ticks_per_second(double v) { ticks_per_second_ = v; }
    double get_ticks_per_second() const { return ticks_per_second_; }
    void set_paused(bool v) { paused_ = v; }
    bool get_paused() const { return paused_; }
    void set_background(bool v) { background_ = v; }
    bool get_background() const { return background_; }
    void set_start_in_background(bool v) { start_in_background_ = v; }
    bool get_start_in_background() const { return start_in_background_; }
    void set_device(int64_t v) { device_ = v; }
    int64_t get_device() const { return device_; }
    void set_match_renderer(bool v) { match_renderer_ = v; }
    bool get_match_renderer() const { return match_renderer_; }
    void set_preference(int64_t v) { preference_ = v; }
    int64_t get_preference() const { return preference_; }
    void set_compile_rules(bool v) { compile_rules_ = v; }
    bool get_compile_rules() const { return compile_rules_; }
    void set_spin_us(int64_t v) { spin_us_ = v; }
    int64_t get_spin_us() const { return spin_us_; }

protected:
    static void _bind_methods();

private:
    Ref<TerniWorld> world_;
    String last_error_;
    double due_ = 0;

    String game_folder_ = "res://sim";
    PackedStringArray mod_folders_;
    String cache_folder_;
    bool auto_start_ = true;
    double ticks_per_second_ = 10;
    bool paused_ = false;
    bool background_ = true;
    bool start_in_background_ = true;
    int64_t device_ = -1;
    bool match_renderer_ = true;
    int64_t preference_ = TerniWorld::PREFER_DISCRETE;
    bool compile_rules_ = true;
    int64_t spin_us_ = 0;

    double report_due_ = 0;
    PackedStringArray monitors_;  // Performance custom monitors added for this simulation

    void on_started();
    void add_monitors();
    void remove_monitors();
    double monitor_tick() const;
    double monitor_stage(const String& stage) const;
    void on_device_lost(const String& error);
    void fail_start();
    bool fail();
    void report_to_debugger(double delta);

public:
    // The editor's Terni debugger tab asks for one field to watch (see report_to_debugger).
    static bool on_debugger_message(const String& message, const Array& data);
    static void register_debugger_capture();
};

}  // namespace godot
