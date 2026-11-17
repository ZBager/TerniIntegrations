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

    void _ready() override;
    void _exit_tree() override;
    void _process(double delta) override;

    // properties
    void set_game_folder(const String& v) { game_folder_ = v; }
    String get_game_folder() const { return game_folder_; }
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

protected:
    static void _bind_methods();

private:
    Ref<TerniWorld> world_;
    String last_error_;
    double due_ = 0;

    String game_folder_ = "res://sim";
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

    void on_started();
    void fail_start();
    bool fail();
};

}  // namespace godot
