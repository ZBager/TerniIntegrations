// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
#include "terni_simulation.h"

#include <algorithm>
#include <string>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/engine_debugger.hpp>
#include <godot_cpp/classes/performance.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

namespace {
// What the editor's Terni tab watches: a simulation node, an entity type and a field.
// Plain C++ strings: Godot types may not be globals (they would be made before godot-cpp
// is ready, and destroyed after it is gone).
std::string watch_node, watch_entity, watch_field;

std::string to_std(const String& s) {
    const CharString utf8 = s.utf8();
    return std::string(utf8.get_data(), static_cast<size_t>(utf8.length()));
}
constexpr double REPORT_SECONDS = 0.5;
constexpr int64_t MAX_ROWS = 64;
}  // namespace

bool TerniSimulation::on_debugger_message(const String& message, const Array& data) {
    if (message.trim_prefix("terni:") != "watch" || data.size() < 3) return false;
    watch_node = to_std(data[0]);
    watch_entity = to_std(data[1]);
    watch_field = to_std(data[2]);
    return true;
}

void TerniSimulation::register_debugger_capture() {
    EngineDebugger* debugger = EngineDebugger::get_singleton();
    if (debugger && debugger->is_active() && !debugger->has_capture("terni"))
        debugger->register_message_capture("terni", callable_mp_static(&TerniSimulation::on_debugger_message));
}

// Twice a second, when the game runs from the editor: this simulation's state for the
// editor's Terni tab, and the values of the watched field if it is one of ours. Never
// while a tick is in flight (reading would wait for it).
void TerniSimulation::report_to_debugger(double delta) {
    EngineDebugger* debugger = EngineDebugger::get_singleton();
    if (!debugger || !debugger->is_active()) return;
    report_due_ -= delta;
    if (report_due_ > 0) return;
    report_due_ = REPORT_SECONDS;
    register_debugger_capture();

    Dictionary state;
    state["node"] = String(get_path());
    state["folder"] = world_.is_valid() ? world_->get_game_folder() : game_folder_;
    state["running"] = is_running();
    state["starting"] = is_starting();
    state["error"] = last_error_;
    state["device_lost"] = is_device_lost();
    if (is_running()) {
        state["device"] = world_->get_device_name();
        state["ticks"] = world_->get_ticks();
        state["gpu_ms"] = world_->get_last_tick_gpu_ms();
        state["slow"] = bool(world_->get_health()["slow"]);
        state["stages"] = world_->get_stage_times();
        // Hashed on the device: the tick in flight would have to be waited for.
        if (!world_->is_tick_in_flight()) state["hashes"] = world_->get_entity_hashes();
        Array entities;
        const PackedStringArray names = world_->get_entities();
        for (int64_t i = 0; i < names.size(); ++i) {
            Dictionary e;
            e["name"] = names[i];
            e["count"] = world_->get_count(i);
            e["capacity"] = world_->get_capacity(i);
            e["dynamic"] = world_->is_dynamic(i);
            e["alive"] = world_->get_alive_count(i);
            e["fields"] = world_->get_fields(i);
            entities.push_back(e);
        }
        state["entities"] = entities;
        const String entity_name = String::utf8(watch_entity.c_str()), field = String::utf8(watch_field.c_str());
        if (watch_node == to_std(String(get_path())) && !world_->is_tick_in_flight()) {
            const int64_t entity = world_->find_entity(entity_name);
            if (entity >= 0 && world_->has_field(entity, field)) {
                const int64_t rows = std::min(world_->get_count(entity), MAX_ROWS);
                Dictionary watched;
                watched["entity"] = entity_name;
                watched["field"] = field;
                watched["type"] = world_->get_field_type(entity, field);
                watched["size"] = world_->get_field_size(entity, field);
                watched["rows"] = world_->get_count(entity);
                watched["words"] = world_->read_field(entity, field, 0, rows);
                PackedStringArray ids;
                for (int64_t r = 0; r < rows; ++r) ids.push_back(world_->get_row_id(entity, r));
                watched["ids"] = ids;
                state["watched"] = watched;
            }
        }
    }
    Array message;
    message.push_back(state);
    debugger->send_message("terni:state", message);
}

bool TerniSimulation::start_world() {
    stop_world();
    last_error_ = "";
    world_.instantiate();
    world_->connect("device_lost", callable_mp(this, &TerniSimulation::on_device_lost));
    Dictionary options;
    if (device_ >= 0) options["device"] = device_;
    options["ignore_renderer"] = !match_renderer_;
    options["preference"] = preference_;
    options["cache_folder"] = cache_folder_;
    options["cache_only"] = !compile_rules_;
    bool ok = world_->load(game_folder_, mod_folders_);
    if (ok) ok = start_in_background_ ? world_->begin_start(options) : world_->start(options);
    if (!ok) {
        fail_start();
        return false;
    }
    if (!start_in_background_) on_started();
    return true;
}

void TerniSimulation::stop_world() {
    remove_monitors();
    if (world_.is_null()) return;
    world_->stop();
    world_.unref();
}

// Godot's Monitors tab (in the Debugger panel) shows the GPU time of the tick and of each stage.
void TerniSimulation::add_monitors() {
    remove_monitors();
    Performance* performance = Performance::get_singleton();
    if (!performance || !is_running()) return;
    const String category = "Terni " + String(get_name()) + "/";
    const String tick = category + String("tick (GPU ms)");
    if (performance->has_custom_monitor(tick)) return;  // another simulation of the same name has them
    performance->add_custom_monitor(tick, callable_mp(this, &TerniSimulation::monitor_tick));
    monitors_.push_back(tick);
    const Array stages = world_->get_stage_times();
    for (int64_t i = 0; i < stages.size(); ++i) {
        const String stage = Dictionary(stages[i])["name"];
        const String id = category + stage + String(" (GPU ms)");
        if (performance->has_custom_monitor(id)) continue;
        performance->add_custom_monitor(id, callable_mp(this, &TerniSimulation::monitor_stage).bind(stage));
        monitors_.push_back(id);
    }
}

void TerniSimulation::remove_monitors() {
    Performance* performance = Performance::get_singleton();
    for (int64_t i = 0; i < monitors_.size(); ++i)
        if (performance && performance->has_custom_monitor(monitors_[i])) performance->remove_custom_monitor(monitors_[i]);
    monitors_.clear();
}

double TerniSimulation::monitor_tick() const { return is_running() ? world_->get_last_tick_gpu_ms() : 0.0; }

double TerniSimulation::monitor_stage(const String& stage) const {
    if (!is_running()) return 0.0;
    const Array stages = world_->get_stage_times();
    for (int64_t i = 0; i < stages.size(); ++i) {
        const Dictionary t = stages[i];
        if (String(t["name"]) == stage) return double(t["last_ms"]);
    }
    return 0.0;
}

void TerniSimulation::on_device_lost(const String& error) {
    last_error_ = error;
    remove_monitors();
    emit_signal("device_lost", error);
}

bool TerniSimulation::step() {
    if (!is_running()) return false;
    if (!world_->end_tick() || !world_->tick()) return fail();
    emit_signal("tick_completed");
    return true;
}

void TerniSimulation::on_started() {
    due_ = 0;
    add_monitors();
    emit_signal("started");
}

void TerniSimulation::fail_start() {
    const String error = world_.is_valid() ? world_->get_last_error() : String();
    last_error_ = error;
    stop_world();
    emit_signal("failed", error);
}

bool TerniSimulation::fail() {
    if (is_device_lost()) return false;  // device_lost was emitted instead
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
    report_to_debugger(delta);
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
    ClassDB::bind_method(D_METHOD("is_device_lost"), &TerniSimulation::is_device_lost);

// A property with its setter and getter; `info` is its PropertyInfo.
#define TERNI_PROPERTY(name, info)                                                           \
    ClassDB::bind_method(D_METHOD("set_" #name, "value"), &TerniSimulation::set_##name);   \
    ClassDB::bind_method(D_METHOD("get_" #name), &TerniSimulation::get_##name);             \
    ADD_PROPERTY(info, "set_" #name, "get_" #name)
    TERNI_PROPERTY(game_folder, PropertyInfo(Variant::STRING, "game_folder", PROPERTY_HINT_DIR));
    TERNI_PROPERTY(mod_folders, PropertyInfo(Variant::PACKED_STRING_ARRAY, "mod_folders", PROPERTY_HINT_TYPE_STRING,
                                             String::num_int64(Variant::STRING) + "/" + String::num_int64(PROPERTY_HINT_DIR) + ":"));
    TERNI_PROPERTY(cache_folder, PropertyInfo(Variant::STRING, "cache_folder", PROPERTY_HINT_DIR));
    TERNI_PROPERTY(auto_start, PropertyInfo(Variant::BOOL, "auto_start"));
    TERNI_PROPERTY(ticks_per_second, PropertyInfo(Variant::FLOAT, "ticks_per_second", PROPERTY_HINT_RANGE, "0,1000,0.1"));
    TERNI_PROPERTY(paused, PropertyInfo(Variant::BOOL, "paused"));
    TERNI_PROPERTY(background, PropertyInfo(Variant::BOOL, "background"));
    TERNI_PROPERTY(start_in_background, PropertyInfo(Variant::BOOL, "start_in_background"));
    TERNI_PROPERTY(device, PropertyInfo(Variant::INT, "device", PROPERTY_HINT_RANGE, "-1,64"));
    TERNI_PROPERTY(match_renderer, PropertyInfo(Variant::BOOL, "match_renderer"));
    TERNI_PROPERTY(preference, PropertyInfo(Variant::INT, "preference", PROPERTY_HINT_ENUM, "Discrete,Integrated,CPU"));
    TERNI_PROPERTY(compile_rules, PropertyInfo(Variant::BOOL, "compile_rules"));
#undef TERNI_PROPERTY

    ADD_SIGNAL(MethodInfo("started"));
    ADD_SIGNAL(MethodInfo("tick_completed"));
    ADD_SIGNAL(MethodInfo("failed", PropertyInfo(Variant::STRING, "error")));
    ADD_SIGNAL(MethodInfo("device_lost", PropertyInfo(Variant::STRING, "error")));
}

}  // namespace godot
