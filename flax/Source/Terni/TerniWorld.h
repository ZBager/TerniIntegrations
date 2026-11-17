// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
#pragma once

#include "Engine/Core/Collections/Array.h"
#include "Engine/Core/Types/Span.h"
#include "Engine/Core/Types/String.h"
#include "Engine/Core/Types/StringView.h"
#include "Engine/Scripting/ScriptingObject.h"

namespace terni_bridge
{
    class World;
}

/// <summary>
/// The type of a field, a define or a modifier key. Every value is one 32-bit word,
/// except Money64, which is two (low, high).
/// </summary>
API_ENUM() enum class TerniFieldType
{
    /// <summary>Fixed point with 8 fraction bits (people, goods, money): raw 256 is 1.</summary>
    Quantity,
    /// <summary>Fixed point with 16 fraction bits (ratios, prices): raw 65536 is 1.</summary>
    Rate,
    /// <summary>A whole-number sum.</summary>
    Agg,
    /// <summary>A whole number.</summary>
    Int,
    /// <summary>A whole number that is never negative.</summary>
    Count,
    /// <summary>The row of another entity.</summary>
    Ref,
    /// <summary>A 64-bit whole number in two words: low, then high.</summary>
    Money64,
};

/// <summary>
/// Which device to choose when none is named.
/// </summary>
API_ENUM() enum class TerniDevicePreference
{
    /// <summary>A dedicated GPU, then an integrated one, then a software driver.</summary>
    Discrete,
    /// <summary>An integrated GPU first, to leave a dedicated one to the renderer.</summary>
    Integrated,
    /// <summary>A software Vulkan driver first: the rules run on the CPU.</summary>
    Cpu,
};

/// <summary>
/// A Vulkan device the simulation can run on.
/// </summary>
API_STRUCT() struct TERNI_API TerniDevice
{
    DECLARE_SCRIPTING_TYPE_MINIMAL(TerniDevice);

    /// <summary>The index to pass in TerniStartOptions.Device.</summary>
    API_FIELD() int32 Index = 0;
    API_FIELD() String Name;
    /// <summary>"discrete", "integrated", "virtual", "cpu (software)" or "other".</summary>
    API_FIELD() String Type;
    /// <summary>A software driver: the rules run on the CPU.</summary>
    API_FIELD() bool IsCpu = false;
    /// <summary>The device has everything Terni needs.</summary>
    API_FIELD() bool Usable = false;
    /// <summary>If not usable: what is missing.</summary>
    API_FIELD() String WhyNot;
};

/// <summary>
/// How TerniWorld.Start runs a loaded game.
/// </summary>
API_STRUCT() struct TERNI_API TerniStartOptions
{
    DECLARE_SCRIPTING_TYPE_MINIMAL(TerniStartOptions);

    /// <summary>Device index from TerniWorld.GetDevices(), or -1 to choose by Preference.</summary>
    API_FIELD() int32 Device = -1;
    API_FIELD() TerniDevicePreference Preference = TerniDevicePreference::Discrete;
    /// <summary>
    /// Folder of the compiled rules (SPIR-V). Empty: the folder "spirv" inside the game folder.
    /// Ship it with the game: players then need no compiler.
    /// </summary>
    API_FIELD() String CacheFolder;
    /// <summary>
    /// Compile rules that are not in the cache yet, with the terni_compile_rules program
    /// that comes with the plugin. Without it, a rule missing from the cache is an error.
    /// </summary>
    API_FIELD() bool CompileRules = true;
    /// <summary>Run Terni's own Vulkan device with the validation layers (slow; for debugging).</summary>
    API_FIELD() bool Validation = false;
};

/// <summary>
/// One modifier instance: a bundle applied to one row. Old handles are harmless: once the
/// instance is removed or has expired, calls with its handle do nothing.
/// </summary>
API_STRUCT() struct TERNI_API TerniModifier
{
    DECLARE_SCRIPTING_TYPE_MINIMAL(TerniModifier);

    API_FIELD() uint32 Slot = 0xFFFFFFFFu;
    API_FIELD() uint32 Version = 0;
};

template<>
struct TIsPODType<TerniModifier>
{
    enum { Value = true };
};

/// <summary>
/// The share of one modifier instance in a key's total, for tooltips.
/// </summary>
API_STRUCT() struct TERNI_API TerniContribution
{
    DECLARE_SCRIPTING_TYPE_MINIMAL(TerniContribution);

    /// <summary>The instance's handle (C#: the Modifier property).</summary>
    API_FIELD() uint32 Slot = 0xFFFFFFFFu;
    API_FIELD() uint32 Version = 0;
    /// <summary>The bundle's index (TerniWorld.GetBundles()).</summary>
    API_FIELD() int32 Bundle = 0;
    /// <summary>Where the instance sits: the row asked about, or one it inherits from.</summary>
    API_FIELD() int32 Entity = 0;
    API_FIELD() int32 Row = 0;
    /// <summary>Raw value added to the key, already scaled.</summary>
    API_FIELD() int32 Value = 0;
};

template<>
struct TIsPODType<TerniContribution>
{
    enum { Value = true };
};

/// <summary>
/// How the simulation's device is doing, from the time its ticks take.
/// </summary>
API_STRUCT() struct TERNI_API TerniHealth
{
    DECLARE_SCRIPTING_TYPE_MINIMAL(TerniHealth);

    /// <summary>A tick when the device was at its best, in milliseconds.</summary>
    API_FIELD() double UsualMs = 0;
    /// <summary>The median of the last 32 ticks (0 until there are 32).</summary>
    API_FIELD() double RecentMs = 0;
    API_FIELD() uint64 Ticks = 0;
    /// <summary>Ticks take over four times the usual: something else keeps the device busy.</summary>
    API_FIELD() bool Slow = false;
};

template<>
struct TIsPODType<TerniHealth>
{
    enum { Value = true };
};

/// <summary>
/// A Terni simulation: a game folder (schema, rules, starting state) run as compute
/// shaders on a Vulkan device of its own, with the same results on every device.
/// Load() reads the folder, Start() runs it, Tick() advances it by one tick.
/// Functions that return bool return true when they worked; after false, LastError says
/// why (it is also written to the log). Call everything from one thread.
/// </summary>
API_CLASS() class TERNI_API TerniWorld : public ScriptingObject
{
    DECLARE_SCRIPTING_TYPE(TerniWorld);

public:
    ~TerniWorld();

    // ---- devices and tools ----

    /// <summary>The Vulkan devices of this machine.</summary>
    API_FUNCTION() static Array<TerniDevice> GetDevices();

    /// <summary>The worlds that are loaded or running, for editor tools.</summary>
    API_FUNCTION() static Array<TerniWorld*> GetWorlds();

    /// <summary>Stops every world. Called when scripts reload and when the engine exits.</summary>
    API_FUNCTION() static void StopAll();

    /// <summary>
    /// Checks a game folder and compiles its rules into the cache folder with the
    /// terni_compile_rules program. Needs no GPU. Use it at build time to ship the cache.
    /// </summary>
    /// <param name="gameFolder">The game folder.</param>
    /// <param name="cacheFolder">The SPIR-V cache folder.</param>
    /// <param name="output">What the program printed: errors and warnings as file:line: message.</param>
    /// <returns>True if every rule is in the cache now.</returns>
    API_FUNCTION() static bool CompileRules(const StringView& gameFolder, const StringView& cacheFolder, API_PARAM(Out) String& output);

    // ---- loading and running ----

    /// <summary>
    /// Reads a game folder. Stops the world first if it was running. Problems in the data
    /// are listed in Errors (and Warnings), each as file:line: message.
    /// </summary>
    API_FUNCTION() bool Load(const StringView& gameFolder);

    /// <summary>
    /// Room for this many rows of an entity type, e.g. for the map size a match picks.
    /// Between Load() and Start(). Peers of a multiplayer game must use the same numbers.
    /// </summary>
    API_FUNCTION() bool SetCapacity(int32 entity, int32 rows);

    /// <summary>
    /// Starts the loaded game on a device: compiles the rules if needed, then uploads the
    /// starting state. Takes from a fraction of a second to a few seconds; BeginStart() does
    /// the same without holding up the frame.
    /// </summary>
    API_FUNCTION() bool Start(const TerniStartOptions& options);
    /// <summary>
    /// Starts like Start(), on a worker thread, and returns at once. Check StartReady()
    /// each frame; once it is true, IsRunning says whether the world started and LastError
    /// why not. Other calls made before that wait for the start to finish.
    /// </summary>
    /// <returns>False if no game is loaded.</returns>
    API_FUNCTION() bool BeginStart(const TerniStartOptions& options);
    /// <summary>The start begun by BeginStart() is over (or none was begun). Never waits.</summary>
    API_FUNCTION() bool StartReady();
    /// <summary>Waits for the start begun by BeginStart(). Returns whether the world runs.</summary>
    API_FUNCTION() bool EndStart();
    /// <summary>BeginStart() was called and StartReady() or EndStart() has not seen it end yet.</summary>
    API_PROPERTY() bool GetIsStarting() const;

    /// <summary>Stops the simulation and frees its device. The game stays loaded.</summary>
    API_FUNCTION() void Stop();

    API_PROPERTY() bool IsLoaded() const;
    API_PROPERTY() bool IsRunning() const;
    /// <summary>The game folder given to Load().</summary>
    API_PROPERTY() const String& GetGameFolder() const { return _folder; }
    /// <summary>Why the last call that failed did.</summary>
    API_PROPERTY() const String& GetLastError() const { return _lastError; }
    /// <summary>Errors in the game data found by Load(): file:line: message.</summary>
    API_PROPERTY() const Array<String>& GetErrors() const { return _errors; }
    /// <summary>Warnings found by Load(), such as stages whose result can depend on thread order.</summary>
    API_PROPERTY() const Array<String>& GetWarnings() const { return _warnings; }

    // ---- ticks ----

    /// <summary>Runs one tick and waits for it.</summary>
    API_FUNCTION() bool Tick();
    /// <summary>
    /// Starts the next tick and returns at once. Collect it with EndTick(), e.g. when
    /// TickReady() says it is done. Writes made in between are for the tick after it.
    /// </summary>
    API_FUNCTION() bool BeginTick();
    /// <summary>The tick in flight is done (or none is). Never waits.</summary>
    API_FUNCTION() bool TickReady();
    /// <summary>Waits for the tick in flight and collects its results.</summary>
    API_FUNCTION() bool EndTick();
    API_PROPERTY() bool GetTickInFlight() const;
    /// <summary>Completed ticks.</summary>
    API_PROPERTY() uint64 GetTicks() const;
    /// <summary>Wall time of the last tick in milliseconds, uploads and readbacks included.</summary>
    API_PROPERTY() double GetLastTickMs() const;
    /// <summary>Time the device spent on the last tick's stages, in milliseconds.</summary>
    API_PROPERTY() double GetLastTickGpuMs() const;

    // ---- what the game is made of ----

    API_FUNCTION() Array<String> GetEntities() const;
    /// <summary>The index of an entity type, or -1.</summary>
    API_FUNCTION() int32 FindEntity(const StringView& name) const;
    API_FUNCTION() int32 GetCapacity(int32 entity) const;
    API_FUNCTION() bool IsDynamic(int32 entity) const;
    API_FUNCTION() Array<String> GetFields(int32 entity) const;
    API_FUNCTION() bool HasField(int32 entity, const StringView& field) const;
    API_FUNCTION() TerniFieldType GetFieldType(int32 entity, const StringView& field) const;
    /// <summary>Values per row of a field (1 unless the schema gives it a size).</summary>
    API_FUNCTION() int32 GetFieldSize(int32 entity, const StringView& field) const;
    /// <summary>The row of a starting entity by the id its history file gave it, or -1.</summary>
    API_FUNCTION() int32 FindRow(int32 entity, const StringView& id) const;
    /// <summary>The id a starting entity has in its history file, or an empty string.</summary>
    API_FUNCTION() String GetRowId(int32 entity, int32 row) const;
    /// <summary>The raw value of a define (a rate of 0.02 is 1310). False if there is none of that name.</summary>
    API_FUNCTION() bool GetDefine(const StringView& name, API_PARAM(Out) int32& raw) const;
    API_FUNCTION() Array<String> GetBundles() const;
    API_FUNCTION() int32 FindBundle(const StringView& name) const;
    API_FUNCTION() Array<String> GetModifierKeys() const;
    API_FUNCTION() int32 FindModifierKey(const StringView& name) const;
    /// <summary>A hash of everything that defines how the game behaves. Multiplayer peers compare it.</summary>
    API_PROPERTY() uint64 GetChecksum() const;

    // ---- fields ----

    /// <summary>
    /// Reads rows of a field from the device, as raw words. Waits for the device: for values
    /// needed every tick use Subscribe() instead.
    /// </summary>
    API_FUNCTION() Array<int32> ReadField(int32 entity, const StringView& field, int32 firstRow = 0, int32 rows = -1);
    /// <summary>Writes whole rows of a field, starting at a row. Takes effect at the next tick.</summary>
    API_FUNCTION() bool WriteField(int32 entity, const StringView& field, int32 row, const Span<int32>& words);
    /// <summary>Writes one value (two words for money64) of a row. Takes effect at the next tick.</summary>
    API_FUNCTION() bool WriteValue(int32 entity, const StringView& field, int32 row, int32 raw, int32 index = 0);
    /// <summary>Writes values of one row of a sized field, starting at an index; the rest of the row is kept.</summary>
    API_FUNCTION() bool WriteValues(int32 entity, const StringView& field, int32 row, int32 index, const Span<int32>& words);

    /// <summary>
    /// Asks for a field to come back at the end of every tick, in the same device
    /// submission as the tick: no extra wait. Returns an id for GetSubscribed(), or -1.
    /// Covers the rows in use, or exactly `rows` rows if given.
    /// </summary>
    API_FUNCTION() int32 Subscribe(int32 entity, const StringView& field, int32 firstRow = 0, int32 rows = -1);
    /// <summary>A copy of a subscribed field as of the last collected tick.</summary>
    API_FUNCTION() Array<int32> GetSubscribed(int32 id) const;
    /// <summary>
    /// The same without a copy: a pointer to `words` 32-bit words, valid until the tick
    /// after the next one begins. C# code uses TerniWorld.Subscribed(id), a span over it.
    /// </summary>
    API_FUNCTION() void* GetSubscribedData(int32 id, API_PARAM(Out) int32& words) const;

    // ---- entities that come and go (entity types marked dynamic) ----

    /// <summary>A new row, alive from the next tick, or -1 if the entity type is full.</summary>
    API_FUNCTION() int32 CreateEntity(int32 entity);
    API_FUNCTION() bool DestroyEntity(int32 entity, int32 row);
    API_FUNCTION() bool IsAlive(int32 entity, int32 row) const;
    /// <summary>How many times the row has been created. With the row it names one entity for good.</summary>
    API_FUNCTION() int32 GetGeneration(int32 entity, int32 row) const;
    /// <summary>Rows in use: one past the highest row ever alive.</summary>
    API_FUNCTION() int32 GetCount(int32 entity) const;
    API_FUNCTION() int32 GetAliveCount(int32 entity) const;

    // ---- modifiers ----

    /// <summary>
    /// Applies a bundle to a row. `scale` is a rate (65536 = once); `duration` is in ticks,
    /// 0 for until removed.
    /// </summary>
    API_FUNCTION() TerniModifier AddModifier(int32 bundle, int32 entity, int32 row, int32 scale = 65536, int32 duration = 0);
    API_FUNCTION() bool SetModifierScale(const TerniModifier& modifier, int32 scale);
    API_FUNCTION() bool RemoveModifier(const TerniModifier& modifier);
    API_FUNCTION() bool IsModifierAlive(const TerniModifier& modifier) const;
    /// <summary>The raw total of a key on a row, with what it inherits.</summary>
    API_FUNCTION() int32 GetModifierTotal(int32 key, int32 entity, int32 row);
    /// <summary>The instances behind that total, nearest first.</summary>
    API_FUNCTION() Array<TerniContribution> GetModifierBreakdown(int32 key, int32 entity, int32 row);

    // ---- saves and state ----

    /// <summary>The complete state. Loads on any device and continues bit for bit.</summary>
    API_FUNCTION() Array<byte> Save();
    /// <summary>Restores a save made by the same game (same Checksum). On failure nothing changes.</summary>
    API_FUNCTION() bool LoadSave(const Span<byte>& data);
    /// <summary>
    /// A hash of the whole state, read back from the device (slow). Equal on every device
    /// that ran the same ticks: compare it between multiplayer peers to find a desync.
    /// </summary>
    API_FUNCTION() uint64 GetStateHash();

    // ---- the device ----

    API_PROPERTY() String GetDeviceName() const;
    API_PROPERTY() TerniHealth GetHealth() const;
    /// <summary>Continues on another device, bit for bit, e.g. when this one has become slow.</summary>
    API_FUNCTION() bool MoveToDevice(int32 device);
    /// <summary>A text report: device, stages and their times.</summary>
    API_FUNCTION() String GetReport() const;

private:
    struct StartJob;
    terni_bridge::World* _bridge;
    mutable StartJob* _start = nullptr;
    String _folder;
    mutable String _lastError;
    Array<String> _errors;
    Array<String> _warnings;

    bool Fail(const String& what) const;
    // The bridge, once a start in flight has finished (waits for it).
    terni_bridge::World* Bridge() const;
    StartJob* PrepareStart(const TerniStartOptions& options);
    bool FinishStart() const;
    // Scripts are reloading: frees everything every world holds. The world objects
    // themselves may outlive the reload (a script can still reference one).
    static void UnloadAll();
    friend struct TerniHooks;
    // Passes a result of the bridge on; if the call failed, records and logs why.
    template<typename T>
    T Checked(T&& value) const
    {
        Report();
        return value;
    }
    void Report() const;
};
