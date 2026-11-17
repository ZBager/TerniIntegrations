// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
#include "terni_bridge.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>

#include <terni/game.hpp>

namespace terni_bridge {

namespace {
constexpr uint32_t kAllRows = 0xFFFFFFFFu;
}

std::vector<Device> devices(std::string& error) {
    std::vector<Device> out;
    error.clear();
    try {
        for (const terni::DeviceInfo& d : terni::VulkanContext::list_devices()) {
            Device device;
            device.index = static_cast<int>(d.index);
            device.name = d.name;
            device.type = d.type;
            device.why_not = d.why_not;
            device.cpu = d.cpu();
            device.usable = d.usable;
            out.push_back(std::move(device));
        }
    } catch (const std::exception& e) {
        error = e.what();
    }
    return out;
}

bool can_compile_rules() { return terni::can_compile_rules(); }

struct World::Impl {
    std::unique_ptr<terni::Game> game;
    std::unique_ptr<terni::GameSim> sim;
    int subscriptions = 0;

    const terni::EntityDef* entity(int index) const {
        return game && index >= 0 && index < static_cast<int>(game->entities.size()) ? &game->entities[static_cast<size_t>(index)]
                                                                                    : nullptr;
    }
    bool row_ok(int entity_index, int row) const {
        const terni::EntityDef* e = entity(entity_index);
        return e && row >= 0 && static_cast<uint32_t>(row) < e->capacity;
    }
    // Throwing checks, for use inside guard().
    void need_game() const {
        if (!game) throw std::runtime_error("no game is loaded");
    }
    void need_sim() const {
        if (!sim) throw std::runtime_error("the world is not running");
    }
    void need_entity(int index) const {
        if (!entity(index)) throw std::runtime_error("no such entity type");
    }
    void need_row(int entity_index, int row) const {
        need_entity(entity_index);
        if (!row_ok(entity_index, row)) throw std::runtime_error("the row is outside the entity type's capacity");
    }
};

namespace {
// Runs `body`; an exception becomes `error` and the fallback value.
template <class T, class Body>
T guard(std::string& error, T fallback, Body&& body) {
    error.clear();
    try {
        return body();
    } catch (const std::exception& e) {
        error = e.what();
    } catch (...) {
        error = "unknown error";
    }
    return fallback;
}
}  // namespace

World::World() : impl_(new Impl()) {}

World::~World() {
    stop();
    delete impl_;
}

bool World::load(const std::string& folder, std::vector<std::string>& errors, std::vector<std::string>& warnings) {
    stop();
    impl_->game.reset();
    errors.clear();
    warnings.clear();
    return guard(error_, false, [&] {
        std::unique_ptr<terni::Game> game = terni::load_game(folder);
        for (const terni::GameDiagnostic& w : game->warnings) warnings.push_back(w.text());
        for (const terni::GameDiagnostic& e : game->errors) errors.push_back(e.text());
        if (!game->ok())
            throw std::runtime_error(errors.front() +
                                     (errors.size() > 1 ? " (and " + std::to_string(errors.size() - 1) + " more)" : ""));
        impl_->game = std::move(game);
        return true;
    });
}

bool World::set_capacity(int entity, int rows) {
    return guard(error_, false, [&] {
        impl_->need_game();
        if (impl_->sim) throw std::runtime_error("capacities are set before the world starts");
        impl_->need_entity(entity);
        impl_->game->set_capacity(static_cast<uint32_t>(entity), static_cast<uint32_t>(std::max(rows, 0)));
        return true;
    });
}

bool World::start(const StartOptions& options) {
    stop();
    return guard(error_, false, [&] {
        impl_->need_game();
        terni::GameSimOptions o;
        o.device = options.device;
        o.preference = static_cast<terni::DevicePreference>(options.preference);
        o.spirv_cache_dir = options.cache_dir;
        o.cache_only = options.cache_only || !terni::can_compile_rules();
        o.validation = options.validation;
        impl_->sim = std::make_unique<terni::GameSim>(*impl_->game, o);
        impl_->subscriptions = 0;
        return true;
    });
}

void World::stop() {
    try {
        impl_->sim.reset();
    } catch (...) {
    }
    impl_->subscriptions = 0;
}

void World::unload() {
    stop();
    impl_->game.reset();
}

bool World::loaded() const { return impl_->game != nullptr; }
bool World::running() const { return impl_->sim != nullptr; }

bool World::tick() {
    return guard(error_, false, [&] {
        impl_->need_sim();
        impl_->sim->tick();
        return true;
    });
}

bool World::begin_tick() {
    return guard(error_, false, [&] {
        impl_->need_sim();
        impl_->sim->begin_tick();
        return true;
    });
}

bool World::tick_ready() {
    return guard(error_, true, [&] { return !impl_->sim || impl_->sim->tick_ready(); });
}

bool World::end_tick() {
    return guard(error_, false, [&] {
        impl_->need_sim();
        impl_->sim->end_tick();
        return true;
    });
}

bool World::tick_in_flight() const { return impl_->sim && impl_->sim->tick_in_flight(); }
uint64_t World::ticks() const { return impl_->sim ? impl_->sim->ticks() : 0; }
double World::last_tick_ms() const { return impl_->sim ? impl_->sim->last_tick_ms() : 0; }
double World::last_tick_gpu_ms() const { return impl_->sim ? impl_->sim->last_tick_gpu_ms() : 0; }

std::vector<std::string> World::entities() const {
    std::vector<std::string> out;
    if (impl_->game)
        for (const terni::EntityDef& e : impl_->game->entities) out.push_back(e.name);
    return out;
}

int World::find_entity(const std::string& name) const { return impl_->game ? impl_->game->entity_index(name) : -1; }

int World::capacity(int entity) const {
    const terni::EntityDef* e = impl_->entity(entity);
    return e ? static_cast<int>(e->capacity) : 0;
}

bool World::dynamic(int entity) const {
    const terni::EntityDef* e = impl_->entity(entity);
    return e && e->dynamic;
}

std::vector<std::string> World::fields(int entity) const {
    std::vector<std::string> out;
    if (const terni::EntityDef* e = impl_->entity(entity))
        for (const terni::FieldDef& f : e->fields) out.push_back(f.name);
    return out;
}

bool World::field_info(int entity, const std::string& field, FieldInfo& info) const {
    const terni::EntityDef* e = impl_->entity(entity);
    const terni::FieldDef* f = e ? e->field(field) : nullptr;
    if (!f) return false;
    info.type = static_cast<int>(f->type);
    info.size = static_cast<int>(f->size);
    info.words = static_cast<int>(f->words());
    return true;
}

int World::find_row(int entity, const std::string& id) const {
    if (!impl_->entity(entity)) return -1;
    const auto& ids = impl_->game->tables[static_cast<size_t>(entity)].ids;
    const auto it = ids.find(id);
    return it == ids.end() ? -1 : static_cast<int>(it->second);
}

std::string World::row_id(int entity, int row) const {
    if (impl_->entity(entity))
        for (const auto& [id, r] : impl_->game->tables[static_cast<size_t>(entity)].ids)
            if (static_cast<int>(r) == row) return id;
    return {};
}

bool World::define(const std::string& name, int32_t& raw) const {
    raw = 0;
    if (!impl_->game) return false;
    const int index = impl_->game->define_index(name);
    if (index < 0) return false;
    raw = impl_->game->define_values[static_cast<size_t>(index)];
    return true;
}

std::vector<std::string> World::bundles() const {
    std::vector<std::string> out;
    if (impl_->game)
        for (const terni::BundleDef& b : impl_->game->bundles) out.push_back(b.name);
    return out;
}

int World::find_bundle(const std::string& name) const { return impl_->game ? impl_->game->bundle_index(name) : -1; }

std::vector<std::string> World::modifier_keys() const {
    std::vector<std::string> out;
    if (impl_->game)
        for (const terni::ModifierKeyDef& k : impl_->game->modifier_keys) out.push_back(k.name);
    return out;
}

int World::find_modifier_key(const std::string& name) const {
    return impl_->game ? impl_->game->modifier_key_index(name) : -1;
}

uint64_t World::checksum() const {
    return guard(error_, uint64_t{0}, [&] { return impl_->game ? terni::game_checksum(*impl_->game) : uint64_t{0}; });
}

bool World::read_field(int entity, const std::string& field, int first_row, int rows, std::vector<int32_t>& words) {
    words.clear();
    return guard(error_, false, [&] {
        impl_->need_sim();
        impl_->need_entity(entity);
        words = impl_->sim->read_field(static_cast<uint32_t>(entity), field, static_cast<uint32_t>(std::max(first_row, 0)),
                                       rows < 0 ? kAllRows : static_cast<uint32_t>(rows));
        return true;
    });
}

bool World::write_field(int entity, const std::string& field, int row, const int32_t* words, size_t count) {
    return guard(error_, false, [&] {
        impl_->need_sim();
        impl_->need_row(entity, row);
        impl_->sim->write_field(static_cast<uint32_t>(entity), field, static_cast<uint32_t>(row),
                                std::span<const int32_t>(words, count));
        return true;
    });
}

bool World::write_values(int entity, const std::string& field, int row, int index, const int32_t* words, size_t count) {
    return guard(error_, false, [&] {
        impl_->need_sim();
        impl_->need_row(entity, row);
        if (index < 0) throw std::runtime_error("the index is negative");
        impl_->sim->write_field(static_cast<uint32_t>(entity), field, static_cast<uint32_t>(row), static_cast<uint32_t>(index),
                                std::span<const int32_t>(words, count));
        return true;
    });
}

int World::subscribe(int entity, const std::string& field, int first_row, int rows) {
    return guard(error_, -1, [&] {
        impl_->need_sim();
        impl_->need_entity(entity);
        const uint32_t id =
            impl_->sim->read_each_tick(static_cast<uint32_t>(entity), field, static_cast<uint32_t>(std::max(first_row, 0)),
                                       rows < 0 ? kAllRows : static_cast<uint32_t>(rows));
        impl_->subscriptions = std::max(impl_->subscriptions, static_cast<int>(id) + 1);
        return static_cast<int>(id);
    });
}

const int32_t* World::subscribed(int id, int& words) const {
    words = 0;
    return guard(error_, static_cast<const int32_t*>(nullptr), [&] {
        impl_->need_sim();
        if (id < 0 || id >= impl_->subscriptions)
            throw std::runtime_error("no such subscription: id " + std::to_string(id) + ", this world has " +
                                     std::to_string(impl_->subscriptions) +
                                     " (subscriptions end when a world stops or starts again)");
        const std::span<const int32_t> result = impl_->sim->tick_result(static_cast<uint32_t>(id));
        words = static_cast<int>(result.size());
        return result.data();
    });
}

int World::create_entity(int entity) {
    return guard(error_, -1, [&] {
        impl_->need_sim();
        impl_->need_entity(entity);
        const uint32_t row = impl_->sim->create_entity(static_cast<uint32_t>(entity));
        return row == terni::NO_ENTITY ? -1 : static_cast<int>(row);
    });
}

bool World::destroy_entity(int entity, int row) {
    return guard(error_, false, [&] {
        impl_->need_sim();
        impl_->need_row(entity, row);
        return impl_->sim->destroy_entity(static_cast<uint32_t>(entity), static_cast<uint32_t>(row));
    });
}

bool World::alive(int entity, int row) const {
    return guard(error_, false, [&] {
        return impl_->sim && impl_->row_ok(entity, row) &&
               impl_->sim->entity_alive(static_cast<uint32_t>(entity), static_cast<uint32_t>(row));
    });
}

int World::generation(int entity, int row) const {
    return guard(error_, 0, [&] {
        return impl_->sim && impl_->row_ok(entity, row)
                   ? static_cast<int>(impl_->sim->entity_generation(static_cast<uint32_t>(entity), static_cast<uint32_t>(row)))
                   : 0;
    });
}

int World::count(int entity) const {
    return guard(error_, 0, [&] {
        if (!impl_->entity(entity)) return 0;
        return impl_->sim ? static_cast<int>(impl_->sim->entity_count(static_cast<uint32_t>(entity)))
                          : static_cast<int>(impl_->game->tables[static_cast<size_t>(entity)].count);
    });
}

int World::alive_count(int entity) const {
    return guard(error_, 0, [&] {
        return impl_->sim && impl_->entity(entity) ? static_cast<int>(impl_->sim->entities_alive(static_cast<uint32_t>(entity)))
                                                   : 0;
    });
}

Modifier World::add_modifier(int bundle, int entity, int row, int32_t scale, int duration) {
    return guard(error_, Modifier{}, [&] {
        impl_->need_sim();
        impl_->need_row(entity, row);
        if (bundle < 0 || bundle >= static_cast<int>(impl_->game->bundles.size())) throw std::runtime_error("no such bundle");
        const terni::ModifierHandle h =
            impl_->sim->add_modifier(static_cast<uint32_t>(bundle), static_cast<uint32_t>(entity), static_cast<uint32_t>(row),
                                     scale, static_cast<uint32_t>(std::max(duration, 0)));
        return Modifier{h.slot, h.version};
    });
}

bool World::set_modifier_scale(Modifier m, int32_t scale) {
    return guard(error_, false, [&] {
        impl_->need_sim();
        return impl_->sim->set_modifier_scale(terni::ModifierHandle{m.slot, m.version}, scale);
    });
}

bool World::remove_modifier(Modifier m) {
    return guard(error_, false, [&] {
        impl_->need_sim();
        return impl_->sim->remove_modifier(terni::ModifierHandle{m.slot, m.version});
    });
}

bool World::modifier_alive(Modifier m) const {
    return guard(error_, false,
                 [&] { return impl_->sim && impl_->sim->modifier_alive(terni::ModifierHandle{m.slot, m.version}); });
}

int32_t World::modifier_total(int key, int entity, int row) {
    return guard(error_, int32_t{0}, [&] {
        impl_->need_sim();
        impl_->need_row(entity, row);
        if (key < 0 || key >= static_cast<int>(impl_->game->modifier_keys.size()))
            throw std::runtime_error("no such modifier key");
        return impl_->sim->modifier_total(static_cast<uint32_t>(key), static_cast<uint32_t>(entity), static_cast<uint32_t>(row));
    });
}

std::vector<Contribution> World::modifier_breakdown(int key, int entity, int row) {
    return guard(error_, std::vector<Contribution>{}, [&] {
        impl_->need_sim();
        impl_->need_row(entity, row);
        if (key < 0 || key >= static_cast<int>(impl_->game->modifier_keys.size()))
            throw std::runtime_error("no such modifier key");
        std::vector<Contribution> out;
        for (const terni::ModifierContribution& c : impl_->sim->modifier_breakdown(
                 static_cast<uint32_t>(key), static_cast<uint32_t>(entity), static_cast<uint32_t>(row))) {
            Contribution item;
            item.modifier = Modifier{c.handle.slot, c.handle.version};
            item.bundle = static_cast<int>(c.bundle);
            item.entity = static_cast<int>(c.entity);
            item.row = static_cast<int>(c.row);
            item.value = c.value;
            out.push_back(item);
        }
        return out;
    });
}

bool World::save(std::vector<uint8_t>& data) {
    data.clear();
    return guard(error_, false, [&] {
        impl_->need_sim();
        data = impl_->sim->save();
        return true;
    });
}

bool World::load_save(const uint8_t* data, size_t size) {
    return guard(error_, false, [&] {
        impl_->need_sim();
        impl_->sim->load(std::vector<uint8_t>(data, data + size));
        return true;
    });
}

uint64_t World::state_hash() {
    return guard(error_, uint64_t{0}, [&] {
        impl_->need_sim();
        return terni::combine(terni::hash_fields(impl_->sim->snapshot()));
    });
}

std::string World::device_name() const {
    return guard(error_, std::string(), [&] { return impl_->sim ? impl_->sim->device_name() : std::string(); });
}

Health World::health() const {
    return guard(error_, Health{}, [&] {
        Health out;
        if (impl_->sim) {
            const terni::DeviceHealth h = impl_->sim->device_health();
            out.usual_ms = h.usual_ms;
            out.recent_ms = h.recent_ms;
            out.ticks = h.ticks;
            out.slow = h.slow;
        }
        return out;
    });
}

bool World::move_to_device(int device) {
    return guard(error_, false, [&] {
        impl_->need_sim();
        if (device < 0) throw std::runtime_error("no such device");
        impl_->sim->move_to_device(static_cast<uint32_t>(device));
        return true;
    });
}

std::string World::report() const {
    return guard(error_, std::string(), [&] { return impl_->sim ? impl_->sim->report() : std::string(); });
}

}  // namespace terni_bridge
