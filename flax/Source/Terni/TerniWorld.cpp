// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception-1.0
// Copyright (C) 2026 Rafał Zając
#include "TerniWorld.h"

#include <terni_bridge.h>

#include "Engine/Core/Log.h"
#include "Engine/Engine/Engine.h"
#include "Engine/Platform/CreateProcessSettings.h"
#include "Engine/Platform/FileSystem.h"
#include "Engine/Platform/Platform.h"
#include "Engine/Platform/StringUtils.h"
#include "Engine/Scripting/Scripting.h"

#if PLATFORM_WINDOWS
#include "Engine/Platform/Windows/IncludeWindowsHeaders.h"
#else
#include <dlfcn.h>
#endif

namespace
{
    std::string ToStd(const StringView& s)
    {
        std::string out((size_t)s.Length() * 3 + 1, '\0');
        int32 length = 0;
        if (s.Length() > 0)
            StringUtils::ConvertUTF162UTF8(s.Get(), &out[0], s.Length(), length);
        out.resize((size_t)length);
        return out;
    }

    String ToFlax(const std::string& s)
    {
        String out;
        out.SetUTF8(s.data(), (int32)s.size());
        return out;
    }

    Array<String> ToFlax(const std::vector<std::string>& list)
    {
        Array<String> out;
        for (const std::string& s : list)
            out.Add(ToFlax(s));
        return out;
    }

    terni_bridge::Modifier ToBridge(const TerniModifier& m)
    {
        terni_bridge::Modifier out;
        out.slot = m.Slot;
        out.version = m.Version;
        return out;
    }

    TerniModifier ToFlax(const terni_bridge::Modifier& m)
    {
        TerniModifier out;
        out.Slot = m.slot;
        out.Version = m.version;
        return out;
    }

    Array<TerniWorld*> Worlds;

    // Worlds hold a Vulkan device: let none outlive the scripts that made it, or the engine.
    struct Hooks
    {
        Hooks()
        {
            Scripting::ScriptsReloading.Bind(&TerniWorld::StopAll);
            Engine::RequestingExit.Bind(&TerniWorld::StopAll);
        }
        ~Hooks()
        {
            Scripting::ScriptsReloading.Unbind(&TerniWorld::StopAll);
            Engine::RequestingExit.Unbind(&TerniWorld::StopAll);
        }
    } HooksInstance;

    // The folder this module was loaded from: terni_compile_rules is deployed beside it.
    String ModuleFolder()
    {
#if PLATFORM_WINDOWS
        HMODULE module = nullptr;
        Char path[1024];
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)&Worlds, &module) &&
            GetModuleFileNameW(module, path, ARRAY_COUNT(path)))
            return StringUtils::GetDirectoryName(String(path));
#else
        Dl_info info;
        if (dladdr((void*)&Worlds, &info) && info.dli_fname)
            return StringUtils::GetDirectoryName(ToFlax(info.dli_fname));
#endif
        return String::Empty;
    }

    String CompilerPath()
    {
#if PLATFORM_WINDOWS
        const Char* name = TEXT("terni_compile_rules.exe");
#else
        const Char* name = TEXT("terni_compile_rules");
#endif
        const String path = ModuleFolder() / name;
        return FileSystem::FileExists(path) ? path : String::Empty;
    }
}

TerniWorld::TerniWorld(const SpawnParams& params)
    : ScriptingObject(params)
    , _bridge(new terni_bridge::World())
{
    Worlds.Add(this);
}

TerniWorld::~TerniWorld()
{
    Worlds.Remove(this);
    delete _bridge;
}

bool TerniWorld::Fail(const String& what) const
{
    _lastError = what;
    LOG(Error, "[Terni] {0}", _lastError);
    return false;
}

void TerniWorld::Report() const
{
    if (!_bridge->error().empty())
        Fail(ToFlax(_bridge->error()));
}

Array<TerniDevice> TerniWorld::GetDevices()
{
    Array<TerniDevice> out;
    std::string error;
    for (const terni_bridge::Device& d : terni_bridge::devices(error))
    {
        TerniDevice device;
        device.Index = d.index;
        device.Name = ToFlax(d.name);
        device.Type = ToFlax(d.type);
        device.IsCpu = d.cpu;
        device.Usable = d.usable;
        device.WhyNot = ToFlax(d.why_not);
        out.Add(device);
    }
    if (!error.empty())
        LOG(Error, "[Terni] {0}", ToFlax(error));
    return out;
}

Array<TerniWorld*> TerniWorld::GetWorlds()
{
    Array<TerniWorld*> out;
    for (TerniWorld* world : Worlds)
        if (world->IsLoaded())
            out.Add(world);
    return out;
}

void TerniWorld::StopAll()
{
    for (TerniWorld* world : Worlds)
        world->Stop();
}

bool TerniWorld::CompileRules(const StringView& gameFolder, const StringView& cacheFolder, String& output)
{
    const String compiler = CompilerPath();
    if (compiler.IsEmpty())
    {
        output = TEXT("terni_compile_rules was not found beside the Terni module.");
        return false;
    }
    CreateProcessSettings settings;
    settings.FileName = compiler;
    settings.Arguments = String::Format(TEXT("\"{0}\" \"{1}\""), gameFolder, cacheFolder);
    settings.LogOutput = false;
    settings.SaveOutput = true;
    settings.WaitForEnd = true;
    settings.HiddenWindow = true;
    const int32 code = Platform::CreateProcess(settings);
    output = String(settings.Output.Get(), settings.Output.Count());
    output = output.TrimTrailing();
    return code == 0;
}

bool TerniWorld::Load(const StringView& gameFolder)
{
    _folder = gameFolder;
    _errors.Clear();
    _warnings.Clear();
    if (!FileSystem::DirectoryExists(gameFolder))
    {
        _bridge->stop();
        return Fail(String::Format(TEXT("there is no game folder {0}"), gameFolder));
    }
    std::vector<std::string> errors, warnings;
    const bool ok = _bridge->load(ToStd(gameFolder), errors, warnings);
    _errors = ToFlax(errors);
    _warnings = ToFlax(warnings);
    for (const String& warning : _warnings)
        LOG(Warning, "[Terni] {0}", warning);
    for (const String& error : _errors)
        LOG(Error, "[Terni] {0}", error);
    return Checked(ok);
}

bool TerniWorld::SetCapacity(int32 entity, int32 rows)
{
    return Checked(_bridge->set_capacity(entity, rows));
}

bool TerniWorld::Start(const TerniStartOptions& options)
{
    if (!_bridge->loaded())
        return Fail(TEXT("no game is loaded"));
    _bridge->stop();
    const String cache = options.CacheFolder.HasChars() ? options.CacheFolder : _folder / TEXT("spirv");
    const bool compiler = terni_bridge::can_compile_rules();
    if (options.CompileRules && !compiler && CompilerPath().HasChars())
    {
        String output;
        if (!CompileRules(_folder, cache, output))
            return Fail(String::Format(TEXT("the rules did not compile:\n{0}"), output));
    }
    terni_bridge::StartOptions o;
    o.device = options.Device;
    o.preference = (int)options.Preference;
    o.cache_dir = ToStd(cache);
    o.cache_only = !options.CompileRules;
    o.validation = options.Validation;
    if (!Checked(_bridge->start(o)))
        return false;
    LOG(Info, "[Terni] {0} runs on {1}", _folder, GetDeviceName());
    return true;
}

void TerniWorld::Stop()
{
    _bridge->stop();
}

bool TerniWorld::IsLoaded() const
{
    return _bridge->loaded();
}

bool TerniWorld::IsRunning() const
{
    return _bridge->running();
}

bool TerniWorld::Tick()
{
    return Checked(_bridge->tick());
}

bool TerniWorld::BeginTick()
{
    return Checked(_bridge->begin_tick());
}

bool TerniWorld::TickReady()
{
    return Checked(_bridge->tick_ready());
}

bool TerniWorld::EndTick()
{
    return Checked(_bridge->end_tick());
}

bool TerniWorld::GetTickInFlight() const
{
    return _bridge->tick_in_flight();
}

uint64 TerniWorld::GetTicks() const
{
    return _bridge->ticks();
}

double TerniWorld::GetLastTickMs() const
{
    return _bridge->last_tick_ms();
}

double TerniWorld::GetLastTickGpuMs() const
{
    return _bridge->last_tick_gpu_ms();
}

Array<String> TerniWorld::GetEntities() const
{
    return ToFlax(_bridge->entities());
}

int32 TerniWorld::FindEntity(const StringView& name) const
{
    return _bridge->find_entity(ToStd(name));
}

int32 TerniWorld::GetCapacity(int32 entity) const
{
    return _bridge->capacity(entity);
}

bool TerniWorld::IsDynamic(int32 entity) const
{
    return _bridge->dynamic(entity);
}

Array<String> TerniWorld::GetFields(int32 entity) const
{
    return ToFlax(_bridge->fields(entity));
}

bool TerniWorld::HasField(int32 entity, const StringView& field) const
{
    terni_bridge::FieldInfo info;
    return _bridge->field_info(entity, ToStd(field), info);
}

TerniFieldType TerniWorld::GetFieldType(int32 entity, const StringView& field) const
{
    terni_bridge::FieldInfo info;
    return _bridge->field_info(entity, ToStd(field), info) ? (TerniFieldType)info.type : TerniFieldType::Int;
}

int32 TerniWorld::GetFieldSize(int32 entity, const StringView& field) const
{
    terni_bridge::FieldInfo info;
    return _bridge->field_info(entity, ToStd(field), info) ? info.size : 0;
}

int32 TerniWorld::FindRow(int32 entity, const StringView& id) const
{
    return _bridge->find_row(entity, ToStd(id));
}

String TerniWorld::GetRowId(int32 entity, int32 row) const
{
    return ToFlax(_bridge->row_id(entity, row));
}

bool TerniWorld::GetDefine(const StringView& name, int32& raw) const
{
    return _bridge->define(ToStd(name), raw);
}

Array<String> TerniWorld::GetBundles() const
{
    return ToFlax(_bridge->bundles());
}

int32 TerniWorld::FindBundle(const StringView& name) const
{
    return _bridge->find_bundle(ToStd(name));
}

Array<String> TerniWorld::GetModifierKeys() const
{
    return ToFlax(_bridge->modifier_keys());
}

int32 TerniWorld::FindModifierKey(const StringView& name) const
{
    return _bridge->find_modifier_key(ToStd(name));
}

uint64 TerniWorld::GetChecksum() const
{
    return Checked(_bridge->checksum());
}

Array<int32> TerniWorld::ReadField(int32 entity, const StringView& field, int32 firstRow, int32 rows)
{
    std::vector<int32_t> words;
    Checked(_bridge->read_field(entity, ToStd(field), firstRow, rows, words));
    Array<int32> out;
    out.Set(words.data(), (int32)words.size());
    return out;
}

bool TerniWorld::WriteField(int32 entity, const StringView& field, int32 row, const Span<int32>& words)
{
    return Checked(_bridge->write_field(entity, ToStd(field), row, words.Get(), (size_t)words.Length()));
}

bool TerniWorld::WriteValue(int32 entity, const StringView& field, int32 row, int32 raw, int32 index)
{
    const std::string name = ToStd(field);
    terni_bridge::FieldInfo info;
    if (!_bridge->field_info(entity, name, info))
        return Fail(TEXT("no such field"));
    const int32 words[2] = { raw, raw < 0 ? -1 : 0 }; // money64: low, high
    return Checked(_bridge->write_values(entity, name, row, index, words, (size_t)info.words));
}

bool TerniWorld::WriteValues(int32 entity, const StringView& field, int32 row, int32 index, const Span<int32>& words)
{
    return Checked(_bridge->write_values(entity, ToStd(field), row, index, words.Get(), (size_t)words.Length()));
}

int32 TerniWorld::Subscribe(int32 entity, const StringView& field, int32 firstRow, int32 rows)
{
    return Checked(_bridge->subscribe(entity, ToStd(field), firstRow, rows));
}

Array<int32> TerniWorld::GetSubscribed(int32 id) const
{
    int32 words = 0;
    const int32* data = (const int32*)GetSubscribedData(id, words);
    Array<int32> out;
    if (data)
        out.Set(data, words);
    return out;
}

void* TerniWorld::GetSubscribedData(int32 id, int32& words) const
{
    return (void*)Checked(_bridge->subscribed(id, words));
}

int32 TerniWorld::CreateEntity(int32 entity)
{
    return Checked(_bridge->create_entity(entity));
}

bool TerniWorld::DestroyEntity(int32 entity, int32 row)
{
    return Checked(_bridge->destroy_entity(entity, row));
}

bool TerniWorld::IsAlive(int32 entity, int32 row) const
{
    return Checked(_bridge->alive(entity, row));
}

int32 TerniWorld::GetGeneration(int32 entity, int32 row) const
{
    return Checked(_bridge->generation(entity, row));
}

int32 TerniWorld::GetCount(int32 entity) const
{
    return Checked(_bridge->count(entity));
}

int32 TerniWorld::GetAliveCount(int32 entity) const
{
    return Checked(_bridge->alive_count(entity));
}

TerniModifier TerniWorld::AddModifier(int32 bundle, int32 entity, int32 row, int32 scale, int32 duration)
{
    return ToFlax(Checked(_bridge->add_modifier(bundle, entity, row, scale, duration)));
}

bool TerniWorld::SetModifierScale(const TerniModifier& modifier, int32 scale)
{
    return Checked(_bridge->set_modifier_scale(ToBridge(modifier), scale));
}

bool TerniWorld::RemoveModifier(const TerniModifier& modifier)
{
    return Checked(_bridge->remove_modifier(ToBridge(modifier)));
}

bool TerniWorld::IsModifierAlive(const TerniModifier& modifier) const
{
    return Checked(_bridge->modifier_alive(ToBridge(modifier)));
}

int32 TerniWorld::GetModifierTotal(int32 key, int32 entity, int32 row)
{
    return Checked(_bridge->modifier_total(key, entity, row));
}

Array<TerniContribution> TerniWorld::GetModifierBreakdown(int32 key, int32 entity, int32 row)
{
    Array<TerniContribution> out;
    for (const terni_bridge::Contribution& c : _bridge->modifier_breakdown(key, entity, row))
    {
        TerniContribution item;
        item.Slot = c.modifier.slot;
        item.Version = c.modifier.version;
        item.Bundle = c.bundle;
        item.Entity = c.entity;
        item.Row = c.row;
        item.Value = c.value;
        out.Add(item);
    }
    Report();
    return out;
}

Array<byte> TerniWorld::Save()
{
    std::vector<uint8_t> data;
    Checked(_bridge->save(data));
    Array<byte> out;
    out.Set(data.data(), (int32)data.size());
    return out;
}

bool TerniWorld::LoadSave(const Span<byte>& data)
{
    return Checked(_bridge->load_save(data.Get(), (size_t)data.Length()));
}

uint64 TerniWorld::GetStateHash()
{
    return Checked(_bridge->state_hash());
}

String TerniWorld::GetDeviceName() const
{
    return ToFlax(_bridge->device_name());
}

TerniHealth TerniWorld::GetHealth() const
{
    const terni_bridge::Health h = _bridge->health();
    TerniHealth out;
    out.UsualMs = h.usual_ms;
    out.RecentMs = h.recent_ms;
    out.Ticks = h.ticks;
    out.Slow = h.slow;
    return out;
}

bool TerniWorld::MoveToDevice(int32 device)
{
    if (!Checked(_bridge->move_to_device(device)))
        return false;
    LOG(Info, "[Terni] {0} moved to {1}", _folder, GetDeviceName());
    return true;
}

String TerniWorld::GetReport() const
{
    return ToFlax(_bridge->report());
}
