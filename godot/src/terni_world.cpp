// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
#include "terni_world.h"

#include <terni_bridge.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace godot {

namespace {

std::string to_std(const String& s) {
    const CharString utf8 = s.utf8();
    return std::string(utf8.get_data(), static_cast<size_t>(utf8.length()));
}

String to_godot(const std::string& s) { return String::utf8(s.data(), static_cast<int64_t>(s.size())); }

PackedStringArray to_godot(const std::vector<std::string>& list) {
    PackedStringArray out;
    for (const std::string& s : list) out.push_back(to_godot(s));
    return out;
}

PackedInt32Array to_packed(const int32_t* words, size_t count) {
    PackedInt32Array out;
    out.resize(static_cast<int64_t>(count));
    if (count) std::copy(words, words + count, out.ptrw());
    return out;
}

terni_bridge::Modifier to_bridge(const Vector2i& m) {
    terni_bridge::Modifier out;
    out.slot = static_cast<uint32_t>(m.x);
    out.version = static_cast<uint32_t>(m.y);
    return out;
}

Vector2i to_godot(const terni_bridge::Modifier& m) { return Vector2i(static_cast<int32_t>(m.slot), static_cast<int32_t>(m.version)); }

int32_t to_int(int64_t v) { return static_cast<int32_t>(std::clamp<int64_t>(v, INT32_MIN, INT32_MAX)); }

// res:// and user:// paths as paths on disk; others unchanged. In an exported game, res://
// is the exported package, which does not hold game folders: the export plugin puts them
// beside the program, at the same relative path.
String globalize(const String& path) {
    if (path.begins_with("res://") && OS::get_singleton()->has_feature("template"))
        return OS::get_singleton()->get_executable_path().get_base_dir().path_join(path.substr(6));
    if (path.begins_with("res://") || path.begins_with("user://"))
        return ProjectSettings::get_singleton()->globalize_path(path);
    return path;
}

std::vector<TerniWorld*>& worlds() {
    static std::vector<TerniWorld*> list;
    return list;
}

// The folder this library was loaded from: terni_compile_rules is beside it.
String library_folder() {
#ifdef _WIN32
    HMODULE module = nullptr;
    wchar_t path[1024];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&worlds), &module) &&
        GetModuleFileNameW(module, path, 1024))
        return String(path).get_base_dir();
#else
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&worlds), &info) && info.dli_fname)
        return String::utf8(info.dli_fname).get_base_dir();
#endif
    return String();
}

String compiler_path() {
#ifdef _WIN32
    const String path = library_folder().path_join("terni_compile_rules.exe");
#else
    const String path = library_folder().path_join("terni_compile_rules");
#endif
    return FileAccess::file_exists(path) ? path : String();
}

// What the renderer says about the GPU it renders with. Main thread.
terni_bridge::RendererGpu renderer_gpu() {
    terni_bridge::RendererGpu gpu;
    RenderingServer* server = RenderingServer::get_singleton();
    RenderingDevice* device = server ? server->get_rendering_device() : nullptr;
    if (!device) return gpu;  // the Compatibility renderer, or a headless run
    const String driver = server->get_current_rendering_driver_name();
    const uint64_t physical = device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_PHYSICAL_DEVICE, RID(), 0);
    if (driver == "vulkan") {
        const uint64_t instance = device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TOPMOST_OBJECT, RID(), 0);
        terni_bridge::vulkan_renderer_gpu(reinterpret_cast<void*>(instance), nullptr, reinterpret_cast<void*>(physical), gpu);
    } else if (driver == "d3d12") {
        terni_bridge::dxgi_adapter_gpu(reinterpret_cast<void*>(physical), gpu);  // an IDXGIAdapter
    }
    return gpu;
}

}  // namespace

// A start: what prepare_start() worked out on the main thread, then the work, which needs
// nothing of the TerniWorld but its bridge and may run on any thread.
struct TerniWorld::StartJob {
    terni_bridge::World* bridge = nullptr;
    terni_bridge::StartOptions options;
    bool cache_first = false;  // try the cache alone before running the compiler
    bool match_renderer = false;
    terni_bridge::RendererGpu gpu;
    int renderer = -1;
    String folder, cache;
    String failure;  // empty: started
    std::atomic<bool> done{false};
    std::thread thread;

    void run() {
        if (match_renderer) {
            std::string error;
            renderer = terni_bridge::find_device(gpu, error);
            if (renderer >= 0) options.device = renderer;  // otherwise the preference decides
        }
        if (cache_first) {
            // From the cache first: a shipped game has every rule there and needs no compiler.
            terni_bridge::StartOptions o = options;
            o.cache_only = true;
            if (bridge->start(o)) {
                done = true;
                return;
            }
            if (!compiler_path().is_empty()) {
                const Dictionary result = TerniWorld::compile_rules(folder, cache);
                if (!bool(result["ok"])) {
                    failure = "the rules did not compile:\n" + String(result["output"]);
                    done = true;
                    return;
                }
            }
        }
        if (!bridge->start(options)) failure = to_godot(bridge->error());
        done = true;
    }
};

TerniWorld::TerniWorld() : bridge_(new terni_bridge::World()) { worlds().push_back(this); }

TerniWorld::~TerniWorld() {
    auto& list = worlds();
    list.erase(std::remove(list.begin(), list.end(), this), list.end());
    bridge();
    delete bridge_;
}

terni_bridge::World* TerniWorld::bridge() const {
    if (start_) finish_start();
    return bridge_;
}

bool TerniWorld::fail(const String& what) const {
    last_error_ = what;
    UtilityFunctions::push_error("[Terni] " + what);
    return false;
}

void TerniWorld::report() const {
    if (!bridge_->error().empty()) fail(to_godot(bridge_->error()));
}

// ---- devices and tools ----

Array TerniWorld::get_devices() {
    Array out;
    std::string error;
    for (const terni_bridge::Device& d : terni_bridge::devices(error)) {
        Dictionary device;
        device["index"] = d.index;
        device["name"] = to_godot(d.name);
        device["type"] = to_godot(d.type);
        device["is_cpu"] = d.cpu;
        device["usable"] = d.usable;
        device["why_not"] = to_godot(d.why_not);
        out.push_back(device);
    }
    if (!error.empty()) UtilityFunctions::push_error("[Terni] " + to_godot(error));
    return out;
}

int64_t TerniWorld::find_renderer_device() {
    std::string error;
    const int index = terni_bridge::find_device(renderer_gpu(), error);
    if (!error.empty()) UtilityFunctions::push_error("[Terni] " + to_godot(error));
    return index;
}

void TerniWorld::stop_all() {
    for (TerniWorld* world : worlds()) world->stop();
}

void TerniWorld::unload_all() {
    for (TerniWorld* world : worlds()) {
        world->bridge()->unload();
        world->errors_.clear();
        world->warnings_.clear();
    }
}

Dictionary TerniWorld::compile_rules(const String& game_folder, const String& cache_folder) {
    Dictionary result;
    const String compiler = compiler_path();
    if (compiler.is_empty()) {
        result["ok"] = false;
        result["output"] = "terni_compile_rules was not found beside the Terni library.";
        return result;
    }
    PackedStringArray args;
    args.push_back(globalize(game_folder));
    args.push_back(globalize(cache_folder));
    Array output;
    const int64_t code = OS::get_singleton()->execute(compiler, args, output, true);
    String text;
    for (int64_t i = 0; i < output.size(); ++i) text += String(output[i]);
    result["ok"] = code == 0;
    result["output"] = text.strip_edges();
    return result;
}

double TerniWorld::to_float(int64_t raw, int64_t type) {
    switch (type) {
        case FIELD_QUANTITY: return static_cast<double>(static_cast<int32_t>(raw)) / QUANTITY_ONE;
        case FIELD_RATE: return static_cast<double>(static_cast<int32_t>(raw)) / RATE_ONE;
        case FIELD_COUNT:
        case FIELD_REF: return static_cast<double>(static_cast<uint32_t>(raw));
        default: return static_cast<double>(raw);
    }
}

int64_t TerniWorld::from_float(double value, int64_t type) {
    switch (type) {
        case FIELD_QUANTITY: return static_cast<int64_t>(std::llround(value * QUANTITY_ONE));
        case FIELD_RATE: return static_cast<int64_t>(std::llround(value * RATE_ONE));
        default: return static_cast<int64_t>(std::llround(value));
    }
}

int64_t TerniWorld::to_int64(int64_t low, int64_t high) {
    return static_cast<int64_t>((static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) | static_cast<uint32_t>(low));
}

// ---- loading and running ----

bool TerniWorld::load(const String& game_folder) {
    folder_ = globalize(game_folder);
    errors_.clear();
    warnings_.clear();
    if (!DirAccess::dir_exists_absolute(folder_)) {
        bridge()->stop();
        return fail("there is no game folder " + folder_);
    }
    std::vector<std::string> errors, warnings;
    const bool ok = bridge()->load(to_std(folder_), errors, warnings);
    errors_ = to_godot(errors);
    warnings_ = to_godot(warnings);
    for (int64_t i = 0; i < warnings_.size(); ++i) UtilityFunctions::push_warning("[Terni] " + warnings_[i]);
    for (int64_t i = 0; i < errors_.size(); ++i) UtilityFunctions::push_error("[Terni] " + errors_[i]);
    report();
    return ok;
}

bool TerniWorld::set_capacity(int64_t entity, int64_t rows) {
    const bool ok = bridge()->set_capacity(to_int(entity), to_int(rows));
    report();
    return ok;
}

// Options (all optional): device (int: run on this device; otherwise choose), ignore_renderer
// (bool: do not prefer the renderer's GPU), preference (DevicePreference), cache_folder
// (String; default: "spirv" in the game folder), cache_only (bool: never compile rules),
// validation (bool).
TerniWorld::StartJob* TerniWorld::prepare_start(const Dictionary& options) {
    if (!bridge()->loaded()) {
        fail("no game is loaded");
        return nullptr;
    }
    bridge()->stop();
    StartJob* job = new StartJob();
    job->bridge = bridge_;
    job->folder = folder_;
    const String cache = options.get("cache_folder", String());
    job->cache = cache.is_empty() ? folder_.path_join("spirv") : globalize(cache);
    const bool use_device = options.has("device") && int64_t(options["device"]) >= 0;
    job->options.device = use_device ? to_int(options["device"]) : -1;
    job->options.preference = to_int(options.get("preference", int64_t(PREFER_DISCRETE)));
    job->options.cache_dir = to_std(job->cache);
    job->options.cache_only = bool(options.get("cache_only", false));
    job->options.validation = bool(options.get("validation", false));
    job->cache_first = !job->options.cache_only && !terni_bridge::can_compile_rules();
    if (!use_device && !bool(options.get("ignore_renderer", false))) {
        job->match_renderer = true;
        job->gpu = renderer_gpu();
    }
    return job;
}

bool TerniWorld::finish_start() const {
    StartJob* job = start_;
    start_ = nullptr;
    if (job->thread.joinable()) job->thread.join();
    const bool ok = job->failure.is_empty();
    if (ok)
        UtilityFunctions::print("[Terni] ", folder_, " runs on ", to_godot(bridge_->device_name()),
                                job->renderer >= 0 ? ", the GPU the engine renders with" : "");
    else
        fail(job->failure);
    delete job;
    return ok;
}

bool TerniWorld::start(const Dictionary& options) {
    StartJob* job = prepare_start(options);
    if (!job) return false;
    job->run();
    start_ = job;
    return finish_start();
}

bool TerniWorld::begin_start(const Dictionary& options) {
    StartJob* job = prepare_start(options);
    if (!job) return false;
    start_ = job;
    job->thread = std::thread([job] { job->run(); });
    return true;
}

bool TerniWorld::start_ready() {
    if (start_ && !start_->done) return false;
    if (start_) finish_start();
    return true;
}

bool TerniWorld::end_start() {
    if (start_) finish_start();
    return is_running();
}

bool TerniWorld::is_starting() const { return start_ != nullptr; }
void TerniWorld::stop() { bridge()->stop(); }
bool TerniWorld::is_loaded() const { return bridge_->loaded(); }  // a start does not change it
bool TerniWorld::is_running() const { return !start_ && bridge_->running(); }

// ---- ticks ----

bool TerniWorld::tick() {
    const bool ok = bridge()->tick();
    report();
    return ok;
}
bool TerniWorld::begin_tick() {
    const bool ok = bridge()->begin_tick();
    report();
    return ok;
}
bool TerniWorld::tick_ready() {
    const bool ok = bridge()->tick_ready();
    report();
    return ok;
}
bool TerniWorld::end_tick() {
    const bool ok = bridge()->end_tick();
    report();
    return ok;
}
bool TerniWorld::is_tick_in_flight() const { return bridge()->tick_in_flight(); }
int64_t TerniWorld::get_ticks() const { return static_cast<int64_t>(bridge()->ticks()); }
double TerniWorld::get_last_tick_ms() const { return bridge()->last_tick_ms(); }
double TerniWorld::get_last_tick_gpu_ms() const { return bridge()->last_tick_gpu_ms(); }

// ---- what the game is made of ----

PackedStringArray TerniWorld::get_entities() const { return to_godot(bridge()->entities()); }
int64_t TerniWorld::find_entity(const String& name) const { return bridge()->find_entity(to_std(name)); }
int64_t TerniWorld::entity(const String& name) const {
    const int64_t index = find_entity(name);
    if (index < 0) fail("the game has no entity type " + name);
    return index;
}
int64_t TerniWorld::get_capacity(int64_t entity) const { return bridge()->capacity(to_int(entity)); }
bool TerniWorld::is_dynamic(int64_t entity) const { return bridge()->dynamic(to_int(entity)); }
PackedStringArray TerniWorld::get_fields(int64_t entity) const { return to_godot(bridge()->fields(to_int(entity))); }
bool TerniWorld::has_field(int64_t entity, const String& field) const {
    terni_bridge::FieldInfo info;
    return bridge()->field_info(to_int(entity), to_std(field), info);
}
int64_t TerniWorld::get_field_type(int64_t entity, const String& field) const {
    terni_bridge::FieldInfo info;
    return bridge()->field_info(to_int(entity), to_std(field), info) ? info.type : -1;
}
int64_t TerniWorld::get_field_size(int64_t entity, const String& field) const {
    terni_bridge::FieldInfo info;
    return bridge()->field_info(to_int(entity), to_std(field), info) ? info.size : 0;
}
int64_t TerniWorld::find_row(int64_t entity, const String& id) const { return bridge()->find_row(to_int(entity), to_std(id)); }
String TerniWorld::get_row_id(int64_t entity, int64_t row) const { return to_godot(bridge()->row_id(to_int(entity), to_int(row))); }
Variant TerniWorld::get_define(const String& name) const {
    int32_t raw = 0;
    return bridge()->define(to_std(name), raw) ? Variant(int64_t(raw)) : Variant();
}
PackedStringArray TerniWorld::get_bundles() const { return to_godot(bridge()->bundles()); }
int64_t TerniWorld::find_bundle(const String& name) const { return bridge()->find_bundle(to_std(name)); }
PackedStringArray TerniWorld::get_modifier_keys() const { return to_godot(bridge()->modifier_keys()); }
int64_t TerniWorld::find_modifier_key(const String& name) const { return bridge()->find_modifier_key(to_std(name)); }
int64_t TerniWorld::get_checksum() const {
    const uint64_t sum = bridge()->checksum();
    report();
    return static_cast<int64_t>(sum);
}

// ---- fields ----

PackedInt32Array TerniWorld::read_field(int64_t entity, const String& field, int64_t first_row, int64_t rows) {
    std::vector<int32_t> words;
    bridge()->read_field(to_int(entity), to_std(field), to_int(first_row), to_int(rows), words);
    report();
    return to_packed(words.data(), words.size());
}

bool TerniWorld::write_field(int64_t entity, const String& field, int64_t row, const PackedInt32Array& words) {
    const bool ok = bridge()->write_field(to_int(entity), to_std(field), to_int(row), words.ptr(), static_cast<size_t>(words.size()));
    report();
    return ok;
}

bool TerniWorld::write_value(int64_t entity, const String& field, int64_t row, int64_t raw, int64_t index) {
    const std::string name = to_std(field);
    terni_bridge::FieldInfo info;
    if (!bridge()->field_info(to_int(entity), name, info)) return fail("no such field: " + field);
    // money64 takes the whole 64-bit value; every other type one 32-bit word
    const int32_t words[2] = {static_cast<int32_t>(raw), static_cast<int32_t>(raw >> 32)};
    const bool ok = bridge()->write_values(to_int(entity), name, to_int(row), to_int(index), words, static_cast<size_t>(info.words));
    report();
    return ok;
}

bool TerniWorld::write_values(int64_t entity, const String& field, int64_t row, int64_t index, const PackedInt32Array& words) {
    const bool ok = bridge()->write_values(to_int(entity), to_std(field), to_int(row), to_int(index), words.ptr(),
                                           static_cast<size_t>(words.size()));
    report();
    return ok;
}

int64_t TerniWorld::subscribe(int64_t entity, const String& field, int64_t first_row, int64_t rows) {
    const int id = bridge()->subscribe(to_int(entity), to_std(field), to_int(first_row), to_int(rows));
    report();
    return id;
}

PackedInt32Array TerniWorld::get_subscribed(int64_t id) const {
    int words = 0;
    const int32_t* data = bridge()->subscribed(to_int(id), words);
    report();
    return to_packed(data, data ? static_cast<size_t>(words) : 0);
}

// ---- entities that come and go ----

int64_t TerniWorld::create_entity(int64_t entity) {
    const int row = bridge()->create_entity(to_int(entity));
    report();
    return row;
}
bool TerniWorld::destroy_entity(int64_t entity, int64_t row) {
    const bool ok = bridge()->destroy_entity(to_int(entity), to_int(row));
    report();
    return ok;
}
bool TerniWorld::is_alive(int64_t entity, int64_t row) const { return bridge()->alive(to_int(entity), to_int(row)); }
int64_t TerniWorld::get_generation(int64_t entity, int64_t row) const { return bridge()->generation(to_int(entity), to_int(row)); }
int64_t TerniWorld::get_count(int64_t entity) const { return bridge()->count(to_int(entity)); }
int64_t TerniWorld::get_alive_count(int64_t entity) const { return bridge()->alive_count(to_int(entity)); }

// ---- modifiers ----

Vector2i TerniWorld::add_modifier(int64_t bundle, int64_t entity, int64_t row, int64_t scale, int64_t duration) {
    const terni_bridge::Modifier m =
        bridge()->add_modifier(to_int(bundle), to_int(entity), to_int(row), to_int(scale), to_int(duration));
    report();
    return to_godot(m);
}
bool TerniWorld::set_modifier_scale(const Vector2i& modifier, int64_t scale) {
    const bool ok = bridge()->set_modifier_scale(to_bridge(modifier), to_int(scale));
    report();
    return ok;
}
bool TerniWorld::remove_modifier(const Vector2i& modifier) {
    const bool ok = bridge()->remove_modifier(to_bridge(modifier));
    report();
    return ok;
}
bool TerniWorld::is_modifier_alive(const Vector2i& modifier) const { return bridge()->modifier_alive(to_bridge(modifier)); }
int64_t TerniWorld::get_modifier_total(int64_t key, int64_t entity, int64_t row) {
    const int32_t total = bridge()->modifier_total(to_int(key), to_int(entity), to_int(row));
    report();
    return total;
}
Array TerniWorld::get_modifier_breakdown(int64_t key, int64_t entity, int64_t row) {
    Array out;
    for (const terni_bridge::Contribution& c : bridge()->modifier_breakdown(to_int(key), to_int(entity), to_int(row))) {
        Dictionary item;
        item["modifier"] = to_godot(c.modifier);
        item["bundle"] = c.bundle;
        item["entity"] = c.entity;
        item["row"] = c.row;
        item["value"] = c.value;
        out.push_back(item);
    }
    report();
    return out;
}

// ---- saves and state ----

PackedByteArray TerniWorld::save() {
    std::vector<uint8_t> data;
    bridge()->save(data);
    report();
    PackedByteArray out;
    out.resize(static_cast<int64_t>(data.size()));
    if (!data.empty()) std::copy(data.begin(), data.end(), out.ptrw());
    return out;
}

bool TerniWorld::load_save(const PackedByteArray& data) {
    const bool ok = bridge()->load_save(data.ptr(), static_cast<size_t>(data.size()));
    report();
    return ok;
}

int64_t TerniWorld::get_state_hash() {
    const uint64_t hash = bridge()->state_hash();
    report();
    return static_cast<int64_t>(hash);
}

// ---- the device ----

String TerniWorld::get_device_name() const { return to_godot(bridge()->device_name()); }

Dictionary TerniWorld::get_health() const {
    const terni_bridge::Health h = bridge()->health();
    Dictionary out;
    out["usual_ms"] = h.usual_ms;
    out["recent_ms"] = h.recent_ms;
    out["ticks"] = static_cast<int64_t>(h.ticks);
    out["slow"] = h.slow;
    return out;
}

bool TerniWorld::move_to_device(int64_t device) {
    const bool ok = bridge()->move_to_device(to_int(device));
    report();
    if (ok) UtilityFunctions::print("[Terni] ", folder_, " moved to ", get_device_name());
    return ok;
}

String TerniWorld::get_report() const { return to_godot(bridge()->report()); }

// ---- binding ----

void TerniWorld::_bind_methods() {
    ClassDB::bind_static_method("TerniWorld", D_METHOD("get_devices"), &TerniWorld::get_devices);
    ClassDB::bind_static_method("TerniWorld", D_METHOD("find_renderer_device"), &TerniWorld::find_renderer_device);
    ClassDB::bind_static_method("TerniWorld", D_METHOD("stop_all"), &TerniWorld::stop_all);
    ClassDB::bind_static_method("TerniWorld", D_METHOD("compile_rules", "game_folder", "cache_folder"), &TerniWorld::compile_rules);
    ClassDB::bind_static_method("TerniWorld", D_METHOD("to_float", "raw", "type"), &TerniWorld::to_float);
    ClassDB::bind_static_method("TerniWorld", D_METHOD("from_float", "value", "type"), &TerniWorld::from_float);
    ClassDB::bind_static_method("TerniWorld", D_METHOD("to_int64", "low", "high"), &TerniWorld::to_int64);

    ClassDB::bind_method(D_METHOD("load", "game_folder"), &TerniWorld::load);
    ClassDB::bind_method(D_METHOD("set_capacity", "entity", "rows"), &TerniWorld::set_capacity);
    ClassDB::bind_method(D_METHOD("start", "options"), &TerniWorld::start, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("begin_start", "options"), &TerniWorld::begin_start, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("start_ready"), &TerniWorld::start_ready);
    ClassDB::bind_method(D_METHOD("end_start"), &TerniWorld::end_start);
    ClassDB::bind_method(D_METHOD("is_starting"), &TerniWorld::is_starting);
    ClassDB::bind_method(D_METHOD("stop"), &TerniWorld::stop);
    ClassDB::bind_method(D_METHOD("is_loaded"), &TerniWorld::is_loaded);
    ClassDB::bind_method(D_METHOD("is_running"), &TerniWorld::is_running);
    ClassDB::bind_method(D_METHOD("get_game_folder"), &TerniWorld::get_game_folder);
    ClassDB::bind_method(D_METHOD("get_last_error"), &TerniWorld::get_last_error);
    ClassDB::bind_method(D_METHOD("get_errors"), &TerniWorld::get_errors);
    ClassDB::bind_method(D_METHOD("get_warnings"), &TerniWorld::get_warnings);

    ClassDB::bind_method(D_METHOD("tick"), &TerniWorld::tick);
    ClassDB::bind_method(D_METHOD("begin_tick"), &TerniWorld::begin_tick);
    ClassDB::bind_method(D_METHOD("tick_ready"), &TerniWorld::tick_ready);
    ClassDB::bind_method(D_METHOD("end_tick"), &TerniWorld::end_tick);
    ClassDB::bind_method(D_METHOD("is_tick_in_flight"), &TerniWorld::is_tick_in_flight);
    ClassDB::bind_method(D_METHOD("get_ticks"), &TerniWorld::get_ticks);
    ClassDB::bind_method(D_METHOD("get_last_tick_ms"), &TerniWorld::get_last_tick_ms);
    ClassDB::bind_method(D_METHOD("get_last_tick_gpu_ms"), &TerniWorld::get_last_tick_gpu_ms);

    ClassDB::bind_method(D_METHOD("get_entities"), &TerniWorld::get_entities);
    ClassDB::bind_method(D_METHOD("find_entity", "name"), &TerniWorld::find_entity);
    ClassDB::bind_method(D_METHOD("entity", "name"), &TerniWorld::entity);
    ClassDB::bind_method(D_METHOD("get_capacity", "entity"), &TerniWorld::get_capacity);
    ClassDB::bind_method(D_METHOD("is_dynamic", "entity"), &TerniWorld::is_dynamic);
    ClassDB::bind_method(D_METHOD("get_fields", "entity"), &TerniWorld::get_fields);
    ClassDB::bind_method(D_METHOD("has_field", "entity", "field"), &TerniWorld::has_field);
    ClassDB::bind_method(D_METHOD("get_field_type", "entity", "field"), &TerniWorld::get_field_type);
    ClassDB::bind_method(D_METHOD("get_field_size", "entity", "field"), &TerniWorld::get_field_size);
    ClassDB::bind_method(D_METHOD("find_row", "entity", "id"), &TerniWorld::find_row);
    ClassDB::bind_method(D_METHOD("get_row_id", "entity", "row"), &TerniWorld::get_row_id);
    ClassDB::bind_method(D_METHOD("get_define", "name"), &TerniWorld::get_define);
    ClassDB::bind_method(D_METHOD("get_bundles"), &TerniWorld::get_bundles);
    ClassDB::bind_method(D_METHOD("find_bundle", "name"), &TerniWorld::find_bundle);
    ClassDB::bind_method(D_METHOD("get_modifier_keys"), &TerniWorld::get_modifier_keys);
    ClassDB::bind_method(D_METHOD("find_modifier_key", "name"), &TerniWorld::find_modifier_key);
    ClassDB::bind_method(D_METHOD("get_checksum"), &TerniWorld::get_checksum);

    ClassDB::bind_method(D_METHOD("read_field", "entity", "field", "first_row", "rows"), &TerniWorld::read_field, DEFVAL(0), DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("write_field", "entity", "field", "row", "words"), &TerniWorld::write_field);
    ClassDB::bind_method(D_METHOD("write_value", "entity", "field", "row", "raw", "index"), &TerniWorld::write_value, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("write_values", "entity", "field", "row", "index", "words"), &TerniWorld::write_values);
    ClassDB::bind_method(D_METHOD("subscribe", "entity", "field", "first_row", "rows"), &TerniWorld::subscribe, DEFVAL(0), DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("get_subscribed", "id"), &TerniWorld::get_subscribed);

    ClassDB::bind_method(D_METHOD("create_entity", "entity"), &TerniWorld::create_entity);
    ClassDB::bind_method(D_METHOD("destroy_entity", "entity", "row"), &TerniWorld::destroy_entity);
    ClassDB::bind_method(D_METHOD("is_alive", "entity", "row"), &TerniWorld::is_alive);
    ClassDB::bind_method(D_METHOD("get_generation", "entity", "row"), &TerniWorld::get_generation);
    ClassDB::bind_method(D_METHOD("get_count", "entity"), &TerniWorld::get_count);
    ClassDB::bind_method(D_METHOD("get_alive_count", "entity"), &TerniWorld::get_alive_count);

    ClassDB::bind_method(D_METHOD("add_modifier", "bundle", "entity", "row", "scale", "duration"), &TerniWorld::add_modifier,
                         DEFVAL(RATE_ONE), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("set_modifier_scale", "modifier", "scale"), &TerniWorld::set_modifier_scale);
    ClassDB::bind_method(D_METHOD("remove_modifier", "modifier"), &TerniWorld::remove_modifier);
    ClassDB::bind_method(D_METHOD("is_modifier_alive", "modifier"), &TerniWorld::is_modifier_alive);
    ClassDB::bind_method(D_METHOD("get_modifier_total", "key", "entity", "row"), &TerniWorld::get_modifier_total);
    ClassDB::bind_method(D_METHOD("get_modifier_breakdown", "key", "entity", "row"), &TerniWorld::get_modifier_breakdown);

    ClassDB::bind_method(D_METHOD("save"), &TerniWorld::save);
    ClassDB::bind_method(D_METHOD("load_save", "data"), &TerniWorld::load_save);
    ClassDB::bind_method(D_METHOD("get_state_hash"), &TerniWorld::get_state_hash);

    ClassDB::bind_method(D_METHOD("get_device_name"), &TerniWorld::get_device_name);
    ClassDB::bind_method(D_METHOD("get_health"), &TerniWorld::get_health);
    ClassDB::bind_method(D_METHOD("move_to_device", "device"), &TerniWorld::move_to_device);
    ClassDB::bind_method(D_METHOD("get_report"), &TerniWorld::get_report);

    BIND_ENUM_CONSTANT(FIELD_QUANTITY);
    BIND_ENUM_CONSTANT(FIELD_RATE);
    BIND_ENUM_CONSTANT(FIELD_AGG);
    BIND_ENUM_CONSTANT(FIELD_INT);
    BIND_ENUM_CONSTANT(FIELD_COUNT);
    BIND_ENUM_CONSTANT(FIELD_REF);
    BIND_ENUM_CONSTANT(FIELD_MONEY64);
    BIND_ENUM_CONSTANT(PREFER_DISCRETE);
    BIND_ENUM_CONSTANT(PREFER_INTEGRATED);
    BIND_ENUM_CONSTANT(PREFER_CPU);
    BIND_CONSTANT(QUANTITY_ONE);
    BIND_CONSTANT(RATE_ONE);
}

}  // namespace godot
