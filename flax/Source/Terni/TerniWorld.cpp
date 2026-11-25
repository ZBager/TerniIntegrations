// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
#include "TerniWorld.h"

#include <terni_bridge.h>

#include "Engine/Core/Log.h"
#include "Engine/Engine/Engine.h"
#include "Engine/Graphics/GPUAdapter.h"
#include "Engine/Graphics/GPUDevice.h"
#include "Engine/Platform/CreateProcessSettings.h"
#include "Engine/Platform/FileSystem.h"
#include "Engine/Platform/Platform.h"
#include "Engine/Platform/StringUtils.h"
#include "Engine/Scripting/Scripting.h"

#include <atomic>
#include <mutex>
#include <thread>

#if PLATFORM_WINDOWS
#include "Engine/Platform/Win32/IncludeWindowsHeaders.h"
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

    Array<TerniHash> ToFlax(const std::vector<terni_bridge::Hash>& hashes)
    {
        Array<TerniHash> out;
        for (const terni_bridge::Hash& h : hashes)
        {
            TerniHash item;
            item.Name = ToFlax(h.name);
            item.Hash = h.hash;
            out.Add(item);
        }
        return out;
    }

    Array<TerniWorld*> Worlds;



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

    // The software Vulkan driver deployed beside this module (lavapipe), added once per
    // process before devices are listed; see terni_bridge::add_shipped_driver. Returns its
    // path if it was added.
    const String& AddShippedDriver()
    {
        static std::once_flag once;
        static String added;
        std::call_once(once, []
        {
            std::string path, error;
            switch (terni_bridge::add_shipped_driver(ToStd(ModuleFolder()), path, error))
            {
            case terni_bridge::DriverResult::added:
                added = ToFlax(path);
                LOG(Info, "[Terni] added the software Vulkan driver {0}", added);
                break;
            case terni_bridge::DriverResult::failed:
                LOG(Warning, "[Terni] the software Vulkan driver {0} could not be added: {1}", ToFlax(path), ToFlax(error));
                break;
            default:
                break;
            }
        });
        return added;
    }

    // The ID of the GPU the engine renders with, from the renderer's own objects. Main thread.
    void RendererGpu(terni_bridge::RendererGpu& gpu)
    {
        GPUDevice* device = GPUDevice::Instance;
        GPUAdapter* adapter = device ? device->GetAdapter() : nullptr;
        if (!adapter)
            return;
        switch (device->GetRendererType())
        {
        case RendererType::Vulkan:
        {
            // VkInstance, VkDevice, vkGetInstanceProcAddr, vkGetDeviceProcAddr
            void** native = (void**)device->GetNativePtr();
            if (native)
                terni_bridge::vulkan_renderer_gpu(native[0], native[2], adapter->GetNativePtr(), gpu);
            break;
        }
        case RendererType::DirectX10:
        case RendererType::DirectX10_1:
        case RendererType::DirectX11:
        case RendererType::DirectX12:
            // The adapter's index in DXGI's list of adapters.
            terni_bridge::dxgi_renderer_gpu((unsigned)(intptr)adapter->GetNativePtr(), gpu);
            break;
        default:
            break;
        }
    }

    // One rule compiler at a time in this process: worlds that start together would
    // otherwise compile the same rules into the same cache folder at once.
    std::mutex& CompileMutex()
    {
        static std::mutex mutex;
        return mutex;
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

// Worlds hold a Vulkan device: let none outlive the scripts that made it, or the engine.
struct TerniHooks
{
    TerniHooks()
    {
        Scripting::ScriptsReloading.Bind(&TerniWorld::UnloadAll);
        Engine::RequestingExit.Bind(&TerniWorld::StopAll);
    }
    ~TerniHooks()
    {
        Scripting::ScriptsReloading.Unbind(&TerniWorld::UnloadAll);
        Engine::RequestingExit.Unbind(&TerniWorld::StopAll);
    }
};
static TerniHooks HooksInstance;

void TerniWorld::UnloadAll()
{
    int32 count = 0;
    for (TerniWorld* world : Worlds)
    {
        if (!world->IsLoaded())
            continue;
        world->Bridge()->unload();
        world->_errors.Clear();
        world->_warnings.Clear();
        count++;
    }
    if (count > 0)
        LOG(Info, "[Terni] scripts reload: unloaded {0} world(s)", count);
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
    Bridge();
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
    if (!Bridge()->error().empty())
        Fail(ToFlax(Bridge()->error()));
    if (_bridge->device_lost() && !_lostReported)
    {
        _lostReported = true;
        TerniWorld* self = const_cast<TerniWorld*>(this);
        self->DeviceLost(self);
    }
}

Array<TerniDevice> TerniWorld::GetDevices()
{
    AddShippedDriver();
    Array<TerniDevice> out;
    std::string error;
    for (const terni_bridge::Device& d : terni_bridge::devices(error))
    {
        TerniDevice device;
        device.Index = d.index;
        device.Name = ToFlax(d.name);
        device.Type = ToFlax(d.type);
        device.Driver = ToFlax(d.driver);
        device.IsCpu = d.cpu;
        device.Usable = d.usable;
        device.WhyNot = ToFlax(d.why_not);
        device.Robustness = (TerniBufferRobustness)d.robustness;
        device.SmallFields = d.small_fields;
        out.Add(device);
    }
    if (!error.empty())
        LOG(Error, "[Terni] {0}", ToFlax(error));
    return out;
}

int32 TerniWorld::FindRendererDevice()
{
    AddShippedDriver();
    terni_bridge::RendererGpu gpu;
    RendererGpu(gpu);
    std::string error;
    const int index = terni_bridge::find_device(gpu, error);
    if (!error.empty())
        LOG(Error, "[Terni] {0}", ToFlax(error));
    return index;
}

String TerniWorld::GetSoftwareDriver()
{
    return AddShippedDriver();
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
    return CompileRulesWithMods(gameFolder, Array<String>(), cacheFolder, output);
}

bool TerniWorld::CompileRulesWithMods(const StringView& gameFolder, const Array<String>& mods, const StringView& cacheFolder, String& output)
{
    const String compiler = CompilerPath();
    if (compiler.IsEmpty())
    {
        output = TEXT("terni_compile_rules was not found beside the Terni module.");
        return false;
    }
    CreateProcessSettings settings;
    settings.FileName = compiler;
    settings.Arguments = String::Format(TEXT("\"{0}\""), gameFolder);
    for (const String& mod : mods)
        settings.Arguments += String::Format(TEXT(" \"{0}\""), mod);
    settings.Arguments += String::Format(TEXT(" \"{0}\""), cacheFolder);
    settings.LogOutput = false;
    settings.SaveOutput = true;
    settings.WaitForEnd = true;
    settings.HiddenWindow = true;
    const int32 code = Platform::CreateProcess(settings);
    output = String(settings.Output.Get(), settings.Output.Count());
    output = output.TrimTrailing();
    return code == 0;
}

uint64 TerniWorld::GetSaveChecksum(const Span<byte>& data)
{
    uint64_t checksum = 0;
    std::string error;
    if (!terni_bridge::save_checksum(data.Get(), (size_t)data.Length(), checksum, error))
        LOG(Error, "[Terni] {0}", ToFlax(error));
    return checksum;
}

bool TerniWorld::Load(const StringView& gameFolder)
{
    return LoadWithMods(gameFolder, Array<String>());
}

bool TerniWorld::LoadWithMods(const StringView& gameFolder, const Array<String>& mods)
{
    _folder = gameFolder;
    _mods = mods;
    _errors.Clear();
    _warnings.Clear();
    _lostReported = false;
    std::vector<std::string> folders { ToStd(gameFolder) };
    for (const String& mod : mods)
    {
        if (!FileSystem::DirectoryExists(mod))
        {
            Bridge()->stop();
            return Fail(String::Format(TEXT("there is no mod folder {0}"), mod));
        }
        folders.push_back(ToStd(mod));
    }
    if (!FileSystem::DirectoryExists(gameFolder))
    {
        Bridge()->stop();
        return Fail(String::Format(TEXT("there is no game folder {0}"), gameFolder));
    }
    std::vector<std::string> errors, warnings;
    const bool ok = Bridge()->load(folders, errors, warnings);
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
    return Checked(Bridge()->set_capacity(entity, rows));
}

bool TerniWorld::SetModifierCapacity(int32 instances)
{
    return Checked(Bridge()->set_modifier_capacity(instances));
}

bool TerniWorld::SetStartCount(int32 entity, int32 rows)
{
    return Checked(Bridge()->set_start_count(entity, rows));
}

bool TerniWorld::WriteStart(int32 entity, const StringView& field, int32 row, const Span<int32>& words)
{
    return Checked(Bridge()->write_start(entity, ToStd(field), row, words.Get(), (size_t)words.Length()));
}

Array<int32> TerniWorld::ReadStart(int32 entity, const StringView& field, int32 firstRow, int32 rows) const
{
    std::vector<int32_t> words;
    Checked(Bridge()->read_start(entity, ToStd(field), firstRow, rows, words));
    Array<int32> out;
    out.Set(words.data(), (int32)words.size());
    return out;
}

bool TerniWorld::AddStartModifier(int32 bundle, int32 entity, int32 row, int32 scale, int32 duration)
{
    return Checked(Bridge()->add_start_modifier(bundle, entity, row, scale, duration));
}

// A start: what PrepareStart() worked out on the main thread, then the work, which needs
// nothing of the TerniWorld but its bridge and may run on any thread.
struct TerniWorld::StartJob
{
    terni_bridge::World* bridge = nullptr;
    terni_bridge::StartOptions options;
    bool cacheFirst = false; // try the cache alone before running the compiler
    String folder, cache;
    Array<String> mods;
    String failure; // empty: started
    bool matchRenderer = false;
    terni_bridge::RendererGpu gpu;
    int renderer = -1; // the renderer's device, if it is to be used and can run the simulation
    std::atomic<bool> done { false };
    std::thread thread;

    void Run()
    {
        if (matchRenderer)
        {
            std::string error;
            renderer = terni_bridge::find_device(gpu, error);
            if (renderer >= 0)
                options.device = renderer; // otherwise the preference decides
        }
        if (cacheFirst)
        {
            // From the cache first: a shipped game has every rule there and needs no compiler.
            // Only when that fails (a rule is new or has changed) is the compiler run.
            terni_bridge::StartOptions o = options;
            o.cache_only = true;
            if (bridge->start(o))
            {
                done = true;
                return;
            }
            std::lock_guard<std::mutex> lock(CompileMutex());
            if (bridge->start(o)) // a world that started at the same time compiled them
            {
                done = true;
                return;
            }
            const String fromCache = ToFlax(bridge->error());
            if (CompilerPath().HasChars())
            {
                String output;
                if (!CompileRulesWithMods(folder, mods, cache, output))
                {
                    failure = String::Format(TEXT("the rules did not compile:\n{0}\n(starting from the cache had failed: {1})"), output, fromCache);
                    done = true;
                    return;
                }
                terni_bridge::parse_compile_output(ToStd(output), options.compiler);
            }
        }
        if (!bridge->start(options))
            failure = ToFlax(bridge->error());
        done = true;
    }
};

terni_bridge::World* TerniWorld::Bridge() const
{
    if (_start)
        FinishStart();
    return _bridge;
}

TerniWorld::StartJob* TerniWorld::PrepareStart(const TerniStartOptions& options)
{
    AddShippedDriver();
    if (!Bridge()->loaded())
    {
        Fail(TEXT("no game is loaded"));
        return nullptr;
    }
    Bridge()->stop();
    StartJob* job = new StartJob();
    job->bridge = _bridge;
    job->folder = _folder;
    job->mods = _mods;
    job->cache = options.CacheFolder.HasChars() ? options.CacheFolder : _folder / TEXT("spirv");
    job->options.device = options.UseDevice ? Math::Max(options.Device, 0) : -1;
    job->options.preference = (int)options.Preference;
    job->options.cache_dir = ToStd(job->cache);
    job->options.cache_only = options.CacheOnly;
    job->options.validation = options.Validation;
    job->options.spin_us = (uint32_t)Math::Max(options.SpinMicroseconds, 0);
    job->options.widen_small_fields = options.WidenSmallFields;
    job->cacheFirst = !options.CacheOnly && !terni_bridge::can_compile_rules();
    if (!options.UseDevice && !options.IgnoreRenderer)
    {
        job->matchRenderer = true;
        RendererGpu(job->gpu);
    }
    return job;
}

bool TerniWorld::FinishStart() const
{
    StartJob* job = _start;
    _start = nullptr;
    if (job->thread.joinable())
        job->thread.join();
    const bool ok = job->failure.IsEmpty();
    if (ok)
        _lostReported = false;
    if (ok && job->renderer >= 0)
        LOG(Info, "[Terni] {0} runs on {1}, the GPU the engine renders with", _folder, ToFlax(_bridge->device_name()));
    else if (ok)
        LOG(Info, "[Terni] {0} runs on {1}", _folder, ToFlax(_bridge->device_name()));
    else
        Fail(job->failure);
    delete job;
    return ok;
}

bool TerniWorld::Start(const TerniStartOptions& options)
{
    StartJob* job = PrepareStart(options);
    if (!job)
        return false;
    job->Run();
    _start = job;
    return FinishStart();
}

bool TerniWorld::BeginStart(const TerniStartOptions& options)
{
    StartJob* job = PrepareStart(options);
    if (!job)
        return false;
    _start = job;
    job->thread = std::thread([job] { job->Run(); });
    return true;
}

bool TerniWorld::StartReady()
{
    if (_start && !_start->done)
        return false;
    if (_start)
        FinishStart();
    return true;
}

bool TerniWorld::EndStart()
{
    if (_start)
        FinishStart();
    return IsRunning();
}

bool TerniWorld::GetIsStarting() const
{
    return _start != nullptr;
}

void TerniWorld::Stop()
{
    Bridge()->stop();
}

bool TerniWorld::IsLoaded() const
{
    return _bridge->loaded(); // a start does not change it: no need to wait
}

bool TerniWorld::IsRunning() const
{
    return !_start && _bridge->running();
}

bool TerniWorld::Tick()
{
    return Checked(Bridge()->tick());
}

bool TerniWorld::BeginTick()
{
    return Checked(Bridge()->begin_tick());
}

bool TerniWorld::TickReady()
{
    return Checked(Bridge()->tick_ready());
}

bool TerniWorld::EndTick()
{
    return Checked(Bridge()->end_tick());
}

bool TerniWorld::TickChecked()
{
    std::vector<terni_bridge::OrderIssue> issues;
    const bool ok = Bridge()->tick_checked(issues);
    _orderIssues.Clear();
    for (const terni_bridge::OrderIssue& i : issues)
    {
        TerniOrderIssue item;
        item.Stage = ToFlax(i.stage);
        item.Field = ToFlax(i.field);
        item.Parallel = i.parallel;
        item.Misfit = i.misfit;
        item.FirstRow = i.first_row;
        item.Rows = i.rows;
        _orderIssues.Add(item);
    }
    return Checked(ok);
}

bool TerniWorld::GetTickInFlight() const
{
    return Bridge()->tick_in_flight();
}

uint64 TerniWorld::GetTicks() const
{
    return Bridge()->ticks();
}

double TerniWorld::GetLastTickMs() const
{
    return Bridge()->last_tick_ms();
}

double TerniWorld::GetLastTickGpuMs() const
{
    return Bridge()->last_tick_gpu_ms();
}

Array<String> TerniWorld::GetEntities() const
{
    return ToFlax(Bridge()->entities());
}

int32 TerniWorld::FindEntity(const StringView& name) const
{
    return Bridge()->find_entity(ToStd(name));
}

int32 TerniWorld::GetCapacity(int32 entity) const
{
    return Bridge()->capacity(entity);
}

bool TerniWorld::IsDynamic(int32 entity) const
{
    return Bridge()->dynamic(entity);
}

Array<String> TerniWorld::GetFields(int32 entity) const
{
    return ToFlax(Bridge()->fields(entity));
}

bool TerniWorld::HasField(int32 entity, const StringView& field) const
{
    terni_bridge::FieldInfo info;
    return Bridge()->field_info(entity, ToStd(field), info);
}

TerniFieldType TerniWorld::GetFieldType(int32 entity, const StringView& field) const
{
    terni_bridge::FieldInfo info;
    return Bridge()->field_info(entity, ToStd(field), info) ? (TerniFieldType)info.type : TerniFieldType::Int;
}

int32 TerniWorld::GetFieldSize(int32 entity, const StringView& field) const
{
    terni_bridge::FieldInfo info;
    return Bridge()->field_info(entity, ToStd(field), info) ? info.size : 0;
}

int32 TerniWorld::GetFieldRef(int32 entity, const StringView& field) const
{
    terni_bridge::FieldInfo info;
    return Bridge()->field_info(entity, ToStd(field), info) ? info.ref : -1;
}

int32 TerniWorld::FindRow(int32 entity, const StringView& id) const
{
    return Bridge()->find_row(entity, ToStd(id));
}

String TerniWorld::GetRowId(int32 entity, int32 row) const
{
    return ToFlax(Bridge()->row_id(entity, row));
}

Array<String> TerniWorld::GetDefines() const
{
    return ToFlax(Bridge()->defines());
}

bool TerniWorld::GetDefine(const StringView& name, int32& raw) const
{
    return Bridge()->define(ToStd(name), raw);
}

Array<String> TerniWorld::GetBundles() const
{
    return ToFlax(Bridge()->bundles());
}

int32 TerniWorld::FindBundle(const StringView& name) const
{
    return Bridge()->find_bundle(ToStd(name));
}

Array<String> TerniWorld::GetModifierKeys() const
{
    return ToFlax(Bridge()->modifier_keys());
}

int32 TerniWorld::FindModifierKey(const StringView& name) const
{
    return Bridge()->find_modifier_key(ToStd(name));
}

bool TerniWorld::GetHasSmallFields() const
{
    return Bridge()->small_fields();
}

uint64 TerniWorld::GetChecksum() const
{
    return Checked(Bridge()->checksum());
}

Array<int32> TerniWorld::ReadField(int32 entity, const StringView& field, int32 firstRow, int32 rows)
{
    std::vector<int32_t> words;
    Checked(Bridge()->read_field(entity, ToStd(field), firstRow, rows, words));
    Array<int32> out;
    out.Set(words.data(), (int32)words.size());
    return out;
}

bool TerniWorld::WriteField(int32 entity, const StringView& field, int32 row, const Span<int32>& words)
{
    return Checked(Bridge()->write_field(entity, ToStd(field), row, words.Get(), (size_t)words.Length()));
}

bool TerniWorld::WriteValue(int32 entity, const StringView& field, int32 row, int32 raw, int32 index)
{
    const std::string name = ToStd(field);
    terni_bridge::FieldInfo info;
    if (!Bridge()->field_info(entity, name, info))
        return Fail(TEXT("no such field"));
    const int32 words[2] = { raw, raw < 0 ? -1 : 0 }; // money64: low, high
    return Checked(Bridge()->write_values(entity, name, row, index, words, (size_t)info.words));
}

bool TerniWorld::WriteValues(int32 entity, const StringView& field, int32 row, int32 index, const Span<int32>& words)
{
    return Checked(Bridge()->write_values(entity, ToStd(field), row, index, words.Get(), (size_t)words.Length()));
}

int32 TerniWorld::Subscribe(int32 entity, const StringView& field, int32 firstRow, int32 rows)
{
    return Checked(Bridge()->subscribe(entity, ToStd(field), firstRow, rows));
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
    return (void*)Checked(Bridge()->subscribed(id, words));
}

Array<byte> TerniWorld::GetSubscribedBytes(int32 id) const
{
    int32 size = 0;
    const byte* data = (const byte*)GetSubscribedBytesData(id, size);
    Array<byte> out;
    if (data)
        out.Set(data, size);
    return out;
}

void* TerniWorld::GetSubscribedBytesData(int32 id, int32& size) const
{
    return (void*)Checked(Bridge()->subscribed_bytes(id, size));
}

int32 TerniWorld::CreateEntity(int32 entity)
{
    return Checked(Bridge()->create_entity(entity));
}

bool TerniWorld::DestroyEntity(int32 entity, int32 row)
{
    return Checked(Bridge()->destroy_entity(entity, row));
}

bool TerniWorld::IsAlive(int32 entity, int32 row) const
{
    return Checked(Bridge()->alive(entity, row));
}

int32 TerniWorld::GetGeneration(int32 entity, int32 row) const
{
    return Checked(Bridge()->generation(entity, row));
}

int32 TerniWorld::GetCount(int32 entity) const
{
    return Checked(Bridge()->count(entity));
}

int32 TerniWorld::GetAliveCount(int32 entity) const
{
    return Checked(Bridge()->alive_count(entity));
}

Array<uint32> TerniWorld::GetAliveBits(int32 entity) const
{
    int32 words = 0;
    const uint32_t* bits = Checked(Bridge()->alive_bits(entity, words));
    Array<uint32> out;
    if (bits)
        out.Set(bits, words);
    return out;
}

int32 TerniWorld::GetFreeCount(int32 entity)
{
    return Checked(Bridge()->free_count(entity));
}

TerniModifier TerniWorld::AddModifier(int32 bundle, int32 entity, int32 row, int32 scale, int32 duration)
{
    return ToFlax(Checked(Bridge()->add_modifier(bundle, entity, row, scale, duration)));
}

Array<TerniModifier> TerniWorld::AddModifiers(int32 bundle, int32 entity, const Span<int32>& rows, int32 scale, int32 duration)
{
    std::vector<terni_bridge::Modifier> added;
    Checked(Bridge()->add_modifiers(bundle, entity, rows.Get(), (size_t)rows.Length(), scale, duration, added));
    Array<TerniModifier> out;
    for (const terni_bridge::Modifier& m : added)
        out.Add(ToFlax(m));
    return out;
}

TerniModifier TerniWorld::ReplaceModifier(const TerniModifier& modifier, int32 bundle, int32 entity, int32 row, int32 scale, int32 duration)
{
    terni_bridge::Modifier m = ToBridge(modifier);
    Checked(Bridge()->replace_modifier(m, bundle, entity, row, scale, duration));
    return ToFlax(m);
}

bool TerniWorld::SetModifierScale(const TerniModifier& modifier, int32 scale)
{
    return Checked(Bridge()->set_modifier_scale(ToBridge(modifier), scale));
}

bool TerniWorld::RemoveModifier(const TerniModifier& modifier)
{
    return Checked(Bridge()->remove_modifier(ToBridge(modifier)));
}

bool TerniWorld::IsModifierAlive(const TerniModifier& modifier) const
{
    return Checked(Bridge()->modifier_alive(ToBridge(modifier)));
}

int32 TerniWorld::GetModifierCount() const
{
    return Bridge()->modifier_count();
}

int32 TerniWorld::GetModifierOwnTotal(int32 key, int32 entity, int32 row) const
{
    return Checked(Bridge()->modifier_own_total(key, entity, row));
}

int32 TerniWorld::GetModifierTotal(int32 key, int32 entity, int32 row)
{
    return Checked(Bridge()->modifier_total(key, entity, row));
}

Array<TerniContribution> TerniWorld::GetModifierBreakdown(int32 key, int32 entity, int32 row)
{
    Array<TerniContribution> out;
    for (const terni_bridge::Contribution& c : Bridge()->modifier_breakdown(key, entity, row))
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
    Checked(Bridge()->save(data));
    Array<byte> out;
    out.Set(data.data(), (int32)data.size());
    return out;
}

bool TerniWorld::LoadSave(const Span<byte>& data, bool byName)
{
    std::vector<std::string> fitted;
    const bool ok = Bridge()->load_save(data.Get(), (size_t)data.Length(), byName, fitted);
    _fitted = ToFlax(fitted);
    for (const String& line : _fitted)
        LOG(Info, "[Terni] save: {0}", line);
    return Checked(ok);
}

uint64 TerniWorld::GetStateHash()
{
    return Checked(Bridge()->state_hash());
}

Array<TerniHash> TerniWorld::GetFieldHashes()
{
    std::vector<terni_bridge::Hash> hashes;
    Checked(Bridge()->field_hashes(hashes));
    return ToFlax(hashes);
}

Array<TerniHash> TerniWorld::GetEntityHashes()
{
    std::vector<terni_bridge::Hash> hashes;
    Checked(Bridge()->entity_hashes(hashes));
    return ToFlax(hashes);
}

Array<TerniStageTime> TerniWorld::GetStageTimes() const
{
    Array<TerniStageTime> out;
    for (const terni_bridge::StageTime& t : Bridge()->stage_times())
    {
        TerniStageTime item;
        item.Name = ToFlax(t.name);
        item.Ran = t.ran;
        item.LastMs = t.last_ms;
        item.Runs = t.runs;
        item.TotalMs = t.total_ms;
        out.Add(item);
    }
    Report();
    return out;
}

TerniCompileStats TerniWorld::GetCompileStats() const
{
    const terni_bridge::CompileStats s = Bridge()->compile_stats();
    TerniCompileStats out;
    out.Compiled = (int32)s.compiled;
    out.Cached = (int32)s.cached;
    out.Ms = s.ms;
    return out;
}

uint64 TerniWorld::GetSyncReads() const
{
    return Bridge()->sync_reads();
}

String TerniWorld::GetDeviceName() const
{
    return ToFlax(Bridge()->device_name());
}

TerniHealth TerniWorld::GetHealth() const
{
    const terni_bridge::Health h = Bridge()->health();
    TerniHealth out;
    out.UsualMs = h.usual_ms;
    out.RecentMs = h.recent_ms;
    out.Ticks = h.ticks;
    out.Slow = h.slow;
    return out;
}

bool TerniWorld::GetIsWidened() const
{
    return Bridge()->widened();
}

bool TerniWorld::MoveToDevice(int32 device)
{
    if (!Checked(Bridge()->move_to_device(device)))
        return false;
    LOG(Info, "[Terni] {0} moved to {1}", _folder, GetDeviceName());
    return true;
}

String TerniWorld::GetReport() const
{
    return ToFlax(Bridge()->report());
}

bool TerniWorld::GetIsDeviceLost() const
{
    return Bridge()->device_lost();
}

void TerniWorld::SimulateDeviceLost()
{
    Bridge()->simulate_device_lost();
}
