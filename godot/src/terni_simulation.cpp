// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
#include "terni_simulation.h"

#include <algorithm>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

bool TerniSimulation::start_world() {
    stop_world();
    last_error_ = "";
    world_.instantiate();
    Dictionary options;
    if (device_ >= 0) options["device"] = device_;
    options["ignore_renderer"] = !match_renderer_;
    options["preference"] = preference_;
    options["cache_folder"] = cache_folder_;
    options["cache_only"] = !compile_rules_;
    bool ok = world_->load(game_folder_);
    if (ok) ok = start_in_background_ ? world_->begin_start(options) : world_->start(options);
    if (!ok) {
        fail_start();
        return false;
    }
    if (!start_in_background_) on_started();
    return true;
}

void TerniSimulation::stop_world() {
    if (world_.is_null()) return;
    world_->stop();
    world_.unref();
}

bool TerniSimulation::step() {
    if (!is_running()) return false;
    if (!world_->end_tick() || !world_->tick()) return fail();
    emit_signal("tick_completed");
    return true;
}

void TerniSimulation::on_started() {
    due_ = 0;
    emit_signal("started");
}

void TerniSimulation::fail_start() {
    const String error = world_.is_valid() ? world_->get_last_error() : String();
    last_error_ = error;
    stop_world();
    emit_signal("failed", error);
}

bool TerniSimulation::fail() {
    const String error = world_.is_valid() ? world_->get_last_error() : String();
    last_error_ = error;
    paused_ = true;
    emit_signal("failed", error);
    return false;
}

void TerniSimulation::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    set_process(true);
    if (auto_start_) start_world();
}

void TerniSimulation::_exit_tree() { stop_world(); }

void TerniSimulation::_process(double delta) {
    if (is_starting()) {
        if (!world_->start_ready()) return;
        if (!world_->is_running()) {
            fail_start();
            return;
        }
        on_started();
    }
    if (!is_running()) return;

    // Collect the tick begun in an earlier frame, once the device is done with it.
    if (world_->is_tick_in_flight()) {
        if (!world_->tick_ready()) return;
        if (!world_->end_tick()) {
            fail();
            return;
        }
        emit_signal("tick_completed");
        if (!is_running()) return;
    }
    if (paused_ || ticks_per_second_ <= 0) {
        due_ = 0;
        return;
    }
    // At most one tick per frame: a slow frame must not queue a burst of ticks.
    due_ = std::min(due_ + delta * ticks_per_second_, 1.0);
    if (due_ < 1.0) return;
    due_ -= 1.0;
    if (background_) {
        if (!world_->begin_tick()) fail();
    } else if (world_->tick()) {
        emit_signal("tick_completed");
    } else {
        fail();
    }
}

void TerniSimulation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start_world"), &TerniSimulation::start_world);
    ClassDB::bind_method(D_METHOD("stop_world"), &TerniSimulation::stop_world);
    ClassDB::bind_method(D_METHOD("step"), &TerniSimulation::step);
    ClassDB::bind_method(D_METHOD("get_world"), &TerniSimulation::get_world);
    ClassDB::bind_method(D_METHOD("is_running"), &TerniSimulation::is_running);
    ClassDB::bind_method(D_METHOD("is_starting"), &TerniSimulation::is_starting);
    ClassDB::bind_method(D_METHOD("get_ticks"), &TerniSimulation::get_ticks);
    ClassDB::bind_method(D_METHOD("get_last_error"), &TerniSimulation::get_last_error);

#define TERNI_PROPERTY(type, name, ...)                                                                   \
    ClassDB::bind_method(D_METHOD("set_" #name, "value"), &TerniSimulation::set_##name);                \
    ClassDB::bind_method(D_METHOD("get_" #name), &TerniSimulation::get_##name);                          \
    ADD_PROPERTY(PropertyInfo(type, #name, ##__VA_ARGS__), "set_" #name, "get_" #name)
    TERNI_PROPERTY(Variant::STRING, game_folder, PROPERTY_HINT_DIR);
    TERNI_PROPERTY(Variant::STRING, cache_folder, PROPERTY_HINT_DIR);
    TERNI_PROPERTY(Variant::BOOL, auto_start);
    TERNI_PROPERTY(Variant::FLOAT, ticks_per_second, PROPERTY_HINT_RANGE, "0,1000,0.1");
    TERNI_PROPERTY(Variant::BOOL, paused);
    TERNI_PROPERTY(Variant::BOOL, background);
    TERNI_PROPERTY(Variant::BOOL, start_in_background);
    TERNI_PROPERTY(Variant::INT, device, PROPERTY_HINT_RANGE, "-1,64");
    TERNI_PROPERTY(Variant::BOOL, match_renderer);
    TERNI_PROPERTY(Variant::INT, preference, PROPERTY_HINT_ENUM, "Discrete,Integrated,CPU");
    TERNI_PROPERTY(Variant::BOOL, compile_rules);
#undef TERNI_PROPERTY

    ADD_SIGNAL(MethodInfo("started"));
    ADD_SIGNAL(MethodInfo("tick_completed"));
    ADD_SIGNAL(MethodInfo("failed", PropertyInfo(Variant::STRING, "error")));
}

}  // namespace godot
