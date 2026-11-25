// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
//
// Typed C# access to the Terni GDExtension. Godot makes no C# classes for extension
// classes, so these wrap them: TerniWorld wraps a TerniWorld object, TerniSimulation a
// TerniSimulation node. Godot compiles this file into any C# project that has the addon.
using System;
using System.Collections.Generic;
using Godot;
using GDArray = Godot.Collections.Array;
using GDDictionary = Godot.Collections.Dictionary;

namespace Terni
{
    /// <summary>The type of a field, a define or a modifier key.</summary>
    public enum TerniFieldType
    {
        /// <summary>Fixed point with 8 fraction bits (people, goods, money): raw 256 is 1.</summary>
        Quantity,
        /// <summary>Fixed point with 16 fraction bits (ratios, prices): raw 65536 is 1.</summary>
        Rate,
        Agg,
        Int,
        Count,
        Ref,
        /// <summary>A 64-bit whole number in two words: low, then high.</summary>
        Money64,
        /// <summary>Small fields (1 or 2 bytes on the device), one word per value here too. In a ref, -1 is no row.</summary>
        Int8, Int16, Count8, Count16, Ref8, Ref16,
    }

    /// <summary>Which device to choose when none is named.</summary>
    public enum TerniDevicePreference { Discrete, Integrated, Cpu }

    /// <summary>What a rule that reads or writes past the end of a field does on a device.</summary>
    public enum TerniBufferRobustness
    {
        /// <summary>Undefined: the device may crash or corrupt other memory.</summary>
        None,
        /// <summary>The access stays inside the field's buffer, in a way each driver chooses.</summary>
        Bounded,
        /// <summary>Reads give 0 and writes are dropped, the same on every device.</summary>
        Defined,
    }

    /// <summary>A Vulkan device the simulation can run on.</summary>
    public readonly record struct TerniDevice(int Index, string Name, string Type, string Driver, bool IsCpu, bool Usable, string WhyNot,
                                              TerniBufferRobustness Robustness);

    /// <summary>A hash of one field ("planet.pop") or of one entity type's fields ("planet"), computed on the device.</summary>
    public readonly record struct TerniHash(string Name, ulong Hash);

    /// <summary>GPU time of one stage: in the last tick (if it ran) and over its runs since the start.</summary>
    public readonly record struct TerniStageTime(string Name, bool Ran, double LastMs, long Runs, double TotalMs)
    {
        public double AverageMs => Runs > 0 ? TotalMs / Runs : 0;
    }

    /// <summary>
    /// A stage whose result depends on the order its rows are handled in: Field came out different
    /// forwards and backwards, or (Parallel) in parallel and one row at a time. With Misfit, no order
    /// problem: the stage wrote Rows values that do not fit a small field, and the nearest were stored.
    /// </summary>
    public readonly record struct TerniOrderIssue(string Stage, string Field, bool Parallel, long FirstRow, long Rows, bool Misfit = false);

    /// <summary>Where the rules' SPIR-V came from at the last start.</summary>
    public readonly record struct TerniCompileStats(int Compiled, int Cached, double Ms);

    /// <summary>C# events over a Godot object's signals, removable again.</summary>
    internal sealed class SignalEvents
    {
        private readonly GodotObject _object;
        // The callables connected for each handler: removing needs the same one.
        private readonly Dictionary<(string, Delegate), Callable> _connected = new();

        public SignalEvents(GodotObject obj) => _object = obj;

        public void Connect(string signal, Delegate handler, Callable callable)
        {
            if (handler == null || _connected.ContainsKey((signal, handler)))
                return;
            _connected[(signal, handler)] = callable;
            _object.Connect(signal, callable);
        }

        public void Disconnect(string signal, Delegate handler)
        {
            if (handler != null && _connected.Remove((signal, handler), out var callable) && _object.IsConnected(signal, callable))
                _object.Disconnect(signal, callable);
        }
    }

    /// <summary>A modifier instance. Old handles are harmless once it is gone.</summary>
    public readonly record struct TerniModifier(int Slot, int Version)
    {
        internal Vector2I ToGodot() => new(Slot, Version);
        internal static TerniModifier From(Vector2I v) => new(v.X, v.Y);
    }

    /// <summary>The share of one modifier instance in a key's total.</summary>
    public readonly record struct TerniContribution(TerniModifier Modifier, int Bundle, int Entity, int Row, int Value);

    /// <summary>How the simulation's device is doing.</summary>
    public readonly record struct TerniHealth(double UsualMs, double RecentMs, long Ticks, bool Slow);

    /// <summary>How TerniWorld.Start runs a loaded game. The defaults choose the device
    /// (the renderer's GPU, else by Preference) and compile rules missing from the cache.</summary>
    public sealed class TerniStartOptions
    {
        /// <summary>A device index from TerniWorld.GetDevices(), or -1 to choose.</summary>
        public int Device = -1;
        /// <summary>When choosing: do not prefer the GPU the engine renders with.</summary>
        public bool IgnoreRenderer;
        public TerniDevicePreference Preference = TerniDevicePreference.Discrete;
        /// <summary>Folder of the compiled rules. Empty: "spirv" in the game folder.</summary>
        public string CacheFolder = "";
        /// <summary>Never compile rules: every rule must be in the cache.</summary>
        public bool CacheOnly;
        public bool Validation;
        /// <summary>Waits for a tick spin for up to this many microseconds before they sleep: worth it
        /// only for ticks well under a millisecond, and it keeps a CPU core busy. 0: they sleep at once.</summary>
        public int SpinMicroseconds;

        internal GDDictionary ToGodot()
        {
            var d = new GDDictionary
            {
                ["ignore_renderer"] = IgnoreRenderer, ["preference"] = (int)Preference,
                ["cache_folder"] = CacheFolder, ["cache_only"] = CacheOnly, ["validation"] = Validation,
                ["spin_us"] = SpinMicroseconds,
            };
            if (Device >= 0)
                d["device"] = Device;
            return d;
        }
    }

    /// <summary>Raw simulation values as numbers to show, and back.</summary>
    public static class TerniValue
    {
        public const int QuantityOne = 1 << 8;
        public const int RateOne = 1 << 16;

        public static double ToDouble(int raw, TerniFieldType type) => type switch
        {
            TerniFieldType.Quantity => raw / (double)QuantityOne,
            TerniFieldType.Rate => raw / (double)RateOne,
            TerniFieldType.Count or TerniFieldType.Ref or TerniFieldType.Ref8 or TerniFieldType.Ref16 => (uint)raw,
            _ => raw,
        };

        /// <summary>Rounded to the nearest step. Send raw values to other players, not doubles.</summary>
        public static int FromDouble(double value, TerniFieldType type) => type switch
        {
            TerniFieldType.Quantity => (int)Math.Round(value * QuantityOne),
            TerniFieldType.Rate => (int)Math.Round(value * RateOne),
            _ => (int)Math.Round(value),
        };

        public static long ToInt64(int low, int high) => ((long)high << 32) | (uint)low;
    }

    /// <summary>
    /// A Terni simulation: a game folder run as compute shaders on a Vulkan device of its
    /// own, with the same results on every device. Calls that return bool return true when
    /// they worked; after false, LastError says why. Call everything from one thread.
    /// </summary>
    public sealed class TerniWorld
    {
        private static readonly StringName ClassName = "TerniWorld";

        /// <summary>The TerniWorld object this wraps (for GDScript or Godot calls).</summary>
        public GodotObject Object { get; }

        /// <summary>A new world, not loaded yet.</summary>
        public TerniWorld() : this(ClassDB.Instantiate(ClassName).AsGodotObject()) { }

        /// <summary>Wraps an existing TerniWorld object, e.g. from TerniSimulation.</summary>
        public TerniWorld(GodotObject world)
        {
            Object = world ?? throw new ArgumentNullException(nameof(world));
            _events = new SignalEvents(world);
        }

        private readonly SignalEvents _events;

        /// <summary>
        /// The device was lost (a rule ran too long and the driver reset the GPU, or the GPU
        /// went away). The world has stopped: Start() it again, on this device or another,
        /// and load the last save. The text says what failed.
        /// </summary>
        public event Action<string> DeviceLost
        {
            add => _events.Connect("device_lost", value, Callable.From(value));
            remove => _events.Disconnect("device_lost", value);
        }

        private Variant C(string method, params Variant[] args) => Object.Call(method, args);
        private static Variant S(string method, params Variant[] args) => ClassDB.ClassCallStatic(ClassName, method, args);

        // ---- devices and tools ----

        public static TerniDevice[] GetDevices()
        {
            var list = S("get_devices").AsGodotArray();
            var result = new TerniDevice[list.Count];
            for (var i = 0; i < list.Count; i++)
            {
                var d = list[i].AsGodotDictionary();
                result[i] = new TerniDevice(d["index"].AsInt32(), d["name"].AsString(), d["type"].AsString(), d["driver"].AsString(),
                                            d["is_cpu"].AsBool(), d["usable"].AsBool(), d["why_not"].AsString(),
                                            (TerniBufferRobustness)d["robustness"].AsInt32());
            }
            return result;
        }

        /// <summary>The device the engine renders with, or -1.</summary>
        public static int FindRendererDevice() => S("find_renderer_device").AsInt32();
        public static void StopAll() => S("stop_all");
        /// <summary>
        /// The software Vulkan driver shipped with the game, if it was added: on a machine whose
        /// own drivers have no usable software device. Empty otherwise.
        /// </summary>
        public static string SoftwareDriver => S("get_software_driver").AsString();

        /// <summary>
        /// Compiles a game folder's rules, with mod folders on top if given, into the cache
        /// folder. Output: errors as file:line: message.
        /// </summary>
        public static bool CompileRules(string gameFolder, string cacheFolder, out string output, string[] mods = null)
        {
            var r = S("compile_rules", gameFolder, cacheFolder, mods ?? Array.Empty<string>()).AsGodotDictionary();
            output = r["output"].AsString();
            return r["ok"].AsBool();
        }

        /// <summary>The checksum of the game a save was made with; 0 if the data is not an intact save.</summary>
        public static ulong SaveChecksum(byte[] data) => unchecked((ulong)S("save_checksum", data).AsInt64());

        // ---- loading and running ----

        /// <summary>
        /// Reads a game folder (res://, user:// or a path on disk), with mod folders on top in
        /// order: a file in a mod replaces the game's file of the same name, a new one adds to it.
        /// </summary>
        public bool Load(string gameFolder, params string[] mods) => C("load", gameFolder, mods ?? Array.Empty<string>()).AsBool();
        public bool SetCapacity(int entity, int rows) => C("set_capacity", entity, rows).AsBool();
        /// <summary>Room for this many modifier instances at once. Before the start; part of Checksum.</summary>
        public bool SetModifierCapacity(int instances) => C("set_modifier_capacity", instances).AsBool();
        /// <summary>Rows in use at the start; new ones get the fields' defaults. Before the start.</summary>
        public bool SetStartCount(int entity, int rows) => C("set_start_count", entity, rows).AsBool();
        /// <summary>The starting value of whole rows of a field, from a row on, as raw words. Before the start.</summary>
        public bool WriteStart(int entity, string field, int row, int[] words) => C("write_start", entity, field, row, words).AsBool();
        /// <summary>The starting value of rows of a field, as raw words; all of them if rows is -1.</summary>
        public int[] ReadStart(int entity, string field, int firstRow = 0, int rows = -1) =>
            C("read_start", entity, field, firstRow, rows).AsInt32Array();
        /// <summary>A modifier present at the start. Before the start.</summary>
        public bool AddStartModifier(int bundle, int entity, int row, int scale = TerniValue.RateOne, int duration = 0) =>
            C("add_start_modifier", bundle, entity, row, scale, duration).AsBool();
        public bool Start(TerniStartOptions options = null) => C("start", (options ?? new TerniStartOptions()).ToGodot()).AsBool();
        /// <summary>Starts on a worker thread; poll StartReady(). Other calls meanwhile wait for the start.</summary>
        public bool BeginStart(TerniStartOptions options = null) => C("begin_start", (options ?? new TerniStartOptions()).ToGodot()).AsBool();
        public bool StartReady() => C("start_ready").AsBool();
        public bool EndStart() => C("end_start").AsBool();
        public bool IsStarting => C("is_starting").AsBool();
        public void Stop() => C("stop");
        public bool IsLoaded => C("is_loaded").AsBool();
        public bool IsRunning => C("is_running").AsBool();
        public string GameFolder => C("get_game_folder").AsString();
        public string[] ModFolders => C("get_mod_folders").AsStringArray();
        public string LastError => C("get_last_error").AsString();
        public string[] Errors => C("get_errors").AsStringArray();
        public string[] Warnings => C("get_warnings").AsStringArray();

        // ---- ticks ----

        public bool Tick() => C("tick").AsBool();
        public bool BeginTick() => C("begin_tick").AsBool();
        public bool TickReady() => C("tick_ready").AsBool();
        public bool EndTick() => C("end_tick").AsBool();
        public bool TickInFlight => C("is_tick_in_flight").AsBool();
        /// <summary>
        /// A tick like Tick() that also finds stages whose result depends on the order of their rows;
        /// OrderIssues lists them, and values that do not fit a small field. Much slower: for tests, on small worlds.
        /// </summary>
        public bool TickChecked() => C("tick_checked").AsBool();
        /// <summary>What the last TickChecked() found.</summary>
        public TerniOrderIssue[] OrderIssues
        {
            get
            {
                var list = C("get_order_issues").AsGodotArray();
                var result = new TerniOrderIssue[list.Count];
                for (var i = 0; i < list.Count; i++)
                {
                    var d = list[i].AsGodotDictionary();
                    result[i] = new TerniOrderIssue(d["stage"].AsString(), d["field"].AsString(), d["parallel"].AsBool(),
                                                    d["first_row"].AsInt64(), d["rows"].AsInt64(), d["misfit"].AsBool());
                }
                return result;
            }
        }
        public long Ticks => C("get_ticks").AsInt64();
        public double LastTickMs => C("get_last_tick_ms").AsDouble();
        public double LastTickGpuMs => C("get_last_tick_gpu_ms").AsDouble();

        // ---- what the game is made of ----

        public string[] GetEntities() => C("get_entities").AsStringArray();
        public int FindEntity(string name) => C("find_entity", name).AsInt32();
        /// <summary>The index of an entity type; throws if there is none of that name.</summary>
        public int Entity(string name)
        {
            var index = FindEntity(name);
            return index >= 0 ? index : throw new ArgumentException($"The game has no entity type '{name}'.");
        }
        public int GetCapacity(int entity) => C("get_capacity", entity).AsInt32();
        public bool IsDynamic(int entity) => C("is_dynamic", entity).AsBool();
        public string[] GetFields(int entity) => C("get_fields", entity).AsStringArray();
        public bool HasField(int entity, string field) => C("has_field", entity, field).AsBool();
        public TerniFieldType GetFieldType(int entity, string field) => (TerniFieldType)C("get_field_type", entity, field).AsInt32();
        public int GetFieldSize(int entity, string field) => C("get_field_size", entity, field).AsInt32();
        /// <summary>The entity type a ref field refers to; -1 for other fields.</summary>
        public int GetFieldRef(int entity, string field) => C("get_field_ref", entity, field).AsInt32();
        public int FindRow(int entity, string id) => C("find_row", entity, id).AsInt32();
        public string GetRowId(int entity, int row) => C("get_row_id", entity, row).AsString();
        public string[] GetDefines() => C("get_defines").AsStringArray();
        public bool GetDefine(string name, out int raw)
        {
            var v = C("get_define", name);
            raw = v.VariantType == Variant.Type.Nil ? 0 : v.AsInt32();
            return v.VariantType != Variant.Type.Nil;
        }
        public string[] GetBundles() => C("get_bundles").AsStringArray();
        public int FindBundle(string name) => C("find_bundle", name).AsInt32();
        public string[] GetModifierKeys() => C("get_modifier_keys").AsStringArray();
        public int FindModifierKey(string name) => C("find_modifier_key", name).AsInt32();
        public ulong Checksum => unchecked((ulong)C("get_checksum").AsInt64());

        // ---- fields ----

        /// <summary>Reads rows of a field from the device (waits for it: use Subscribe for every tick).</summary>
        public int[] ReadField(int entity, string field, int firstRow = 0, int rows = -1) =>
            C("read_field", entity, field, firstRow, rows).AsInt32Array();
        public bool WriteField(int entity, string field, int row, int[] words) => C("write_field", entity, field, row, words).AsBool();
        /// <summary>One value; for money64 the whole 64-bit value.</summary>
        public bool WriteValue(int entity, string field, int row, long raw, int index = 0) =>
            C("write_value", entity, field, row, raw, index).AsBool();
        public bool WriteValues(int entity, string field, int row, int index, int[] words) =>
            C("write_values", entity, field, row, index, words).AsBool();
        /// <summary>A field copied back with every tick; returns the id for GetSubscribed, or -1.</summary>
        public int Subscribe(int entity, string field, int firstRow = 0, int rows = -1) =>
            C("subscribe", entity, field, firstRow, rows).AsInt32();
        public int[] GetSubscribed(int id) => C("get_subscribed", id).AsInt32Array();

        // ---- entities that come and go ----

        public int CreateEntity(int entity) => C("create_entity", entity).AsInt32();
        public bool DestroyEntity(int entity, int row) => C("destroy_entity", entity, row).AsBool();
        public bool IsAlive(int entity, int row) => C("is_alive", entity, row).AsBool();
        public int GetGeneration(int entity, int row) => C("get_generation", entity, row).AsInt32();
        public int GetCount(int entity) => C("get_count", entity).AsInt32();
        public int GetAliveCount(int entity) => C("get_alive_count", entity).AsInt32();
        /// <summary>A dynamic entity type's rows: row r is alive if bit r % 32 of word r / 32 is set.</summary>
        public int[] GetAliveBits(int entity) => C("get_alive_bits", entity).AsInt32Array();
        /// <summary>Rows CreateEntity() would find now; -1 if the call failed.</summary>
        public int GetFreeCount(int entity) => C("get_free_count", entity).AsInt32();

        // ---- modifiers ----

        public TerniModifier AddModifier(int bundle, int entity, int row, int scale = TerniValue.RateOne, int duration = 0) =>
            TerniModifier.From(C("add_modifier", bundle, entity, row, scale, duration).AsVector2I());
        /// <summary>The same bundle on many rows, all or none: one handle per row, or an empty array if it failed.</summary>
        public TerniModifier[] AddModifiers(int bundle, int entity, int[] rows, int scale = TerniValue.RateOne, int duration = 0)
        {
            var list = C("add_modifiers", bundle, entity, rows, scale, duration).AsGodotArray();
            var result = new TerniModifier[list.Count];
            for (var i = 0; i < list.Count; i++)
                result[i] = TerniModifier.From(list[i].AsVector2I());
            return result;
        }
        /// <summary>
        /// Removes a modifier if it is still alive and adds a new one in its place: returns the new
        /// handle, or the old one if the call failed.
        /// </summary>
        public TerniModifier ReplaceModifier(TerniModifier modifier, int bundle, int entity, int row, int scale = TerniValue.RateOne,
                                             int duration = 0) =>
            TerniModifier.From(C("replace_modifier", modifier.ToGodot(), bundle, entity, row, scale, duration).AsVector2I());
        public bool SetModifierScale(TerniModifier modifier, int scale) => C("set_modifier_scale", modifier.ToGodot(), scale).AsBool();
        public bool RemoveModifier(TerniModifier modifier) => C("remove_modifier", modifier.ToGodot()).AsBool();
        public bool IsModifierAlive(TerniModifier modifier) => C("is_modifier_alive", modifier.ToGodot()).AsBool();
        /// <summary>Modifier instances alive now.</summary>
        public int ModifierCount => C("get_modifier_count").AsInt32();
        /// <summary>The raw total of a key on a row without what it inherits.</summary>
        public int GetModifierOwnTotal(int key, int entity, int row) => C("get_modifier_own_total", key, entity, row).AsInt32();
        public int GetModifierTotal(int key, int entity, int row) => C("get_modifier_total", key, entity, row).AsInt32();
        public TerniContribution[] GetModifierBreakdown(int key, int entity, int row)
        {
            var list = C("get_modifier_breakdown", key, entity, row).AsGodotArray();
            var result = new TerniContribution[list.Count];
            for (var i = 0; i < list.Count; i++)
            {
                var d = list[i].AsGodotDictionary();
                result[i] = new TerniContribution(TerniModifier.From(d["modifier"].AsVector2I()), d["bundle"].AsInt32(),
                                                  d["entity"].AsInt32(), d["row"].AsInt32(), d["value"].AsInt32());
            }
            return result;
        }

        // ---- saves and state ----

        public byte[] Save() => C("save").AsByteArray();
        /// <summary>
        /// Restores a save. byName: a save of another version of the game (or other mods) is
        /// fitted to this one by name; FittedLines says what was fitted.
        /// </summary>
        public bool LoadSave(byte[] data, bool byName = false) => C("load_save", data, byName).AsBool();
        /// <summary>What the last LoadSave() fitted, one line each.</summary>
        public string[] FittedLines => C("get_fitted_lines").AsStringArray();
        /// <summary>A hash of the whole state, equal on every device that ran the same ticks.</summary>
        public ulong GetStateHash() => unchecked((ulong)C("get_state_hash").AsInt64());
        /// <summary>One hash per field, computed on the device: cheap enough to compare every few ticks.</summary>
        public TerniHash[] GetFieldHashes() => Hashes(C("get_field_hashes").AsGodotDictionary());
        /// <summary>One hash per entity type ("terni": the tick and counts), computed on the device.</summary>
        public TerniHash[] GetEntityHashes() => Hashes(C("get_entity_hashes").AsGodotDictionary());
        /// <summary>GPU time of each stage, in the order they run (Terni's own are named terni_...).</summary>
        public TerniStageTime[] GetStageTimes()
        {
            var list = C("get_stage_times").AsGodotArray();
            var result = new TerniStageTime[list.Count];
            for (var i = 0; i < list.Count; i++)
            {
                var d = list[i].AsGodotDictionary();
                result[i] = new TerniStageTime(d["name"].AsString(), d["ran"].AsBool(), d["last_ms"].AsDouble(),
                                               d["runs"].AsInt64(), d["total_ms"].AsDouble());
            }
            return result;
        }

        /// <summary>Where the rules' SPIR-V came from at the last start.</summary>
        public TerniCompileStats CompileStats
        {
            get
            {
                var d = C("get_compile_stats").AsGodotDictionary();
                return new TerniCompileStats(d["compiled"].AsInt32(), d["cached"].AsInt32(), d["ms"].AsDouble());
            }
        }
        /// <summary>Reads so far that waited for the device (ReadField, Save, GetStateHash): keep them out of every tick.</summary>
        public long SyncReads => C("get_sync_reads").AsInt64();

        private static TerniHash[] Hashes(GDDictionary d)
        {
            var result = new List<TerniHash>(d.Count);
            foreach (var (name, hash) in d)
                result.Add(new TerniHash(name.AsString(), unchecked((ulong)hash.AsInt64())));
            return result.ToArray();
        }

        // ---- the device ----

        public string DeviceName => C("get_device_name").AsString();
        public TerniHealth Health
        {
            get
            {
                var d = C("get_health").AsGodotDictionary();
                return new TerniHealth(d["usual_ms"].AsDouble(), d["recent_ms"].AsDouble(), d["ticks"].AsInt64(), d["slow"].AsBool());
            }
        }
        public bool MoveToDevice(int device) => C("move_to_device", device).AsBool();
        public string GetReport() => C("get_report").AsString();
        /// <summary>The device was lost; see DeviceLost.</summary>
        public bool IsDeviceLost => C("is_device_lost").AsBool();
        /// <summary>Makes the next call that uses the device find it lost, to test how the game recovers.</summary>
        public void SimulateDeviceLost() => C("simulate_device_lost");
    }

    /// <summary>
    /// Wraps a TerniSimulation node, the node that runs a game folder from a scene, with
    /// C# events for its signals.
    /// </summary>
    public sealed class TerniSimulation
    {
        /// <summary>The TerniSimulation node.</summary>
        public Node Node { get; }
        private TerniWorld _world;

        public TerniSimulation(Node node)
        {
            if (node == null || node.GetClass() != "TerniSimulation")
                throw new ArgumentException("Not a TerniSimulation node.", nameof(node));
            Node = node;
            _events = new SignalEvents(node);
        }

        private readonly SignalEvents _events;

        /// <summary>A world started and its starting state is on the device.</summary>
        public event Action Started
        {
            add => _events.Connect("started", value, Callable.From(value));
            remove => _events.Disconnect("started", value);
        }
        /// <summary>A tick was collected: subscribed fields hold its results.</summary>
        public event Action TickCompleted
        {
            add => _events.Connect("tick_completed", value, Callable.From(value));
            remove => _events.Disconnect("tick_completed", value);
        }
        /// <summary>Loading, starting or a tick failed (a lost device is DeviceLost instead).</summary>
        public event Action<string> Failed
        {
            add => _events.Connect("failed", value, Callable.From(value));
            remove => _events.Disconnect("failed", value);
        }
        /// <summary>
        /// The device was lost and the world has stopped: StartWorld() again and load the
        /// last save into World.
        /// </summary>
        public event Action<string> DeviceLost
        {
            add => _events.Connect("device_lost", value, Callable.From(value));
            remove => _events.Disconnect("device_lost", value);
        }

        /// <summary>The world, or null before a start and after StopWorld().</summary>
        public TerniWorld World
        {
            get
            {
                var obj = Node.Call("get_world").AsGodotObject();
                if (obj == null)
                    return _world = null;
                if (_world == null || _world.Object != obj)
                    _world = new TerniWorld(obj);
                return _world;
            }
        }

        public bool IsRunning => Node.Call("is_running").AsBool();
        public bool IsStarting => Node.Call("is_starting").AsBool();
        public long Ticks => Node.Call("get_ticks").AsInt64();
        public string LastError => Node.Call("get_last_error").AsString();
        public bool IsDeviceLost => Node.Call("is_device_lost").AsBool();
        public bool StartWorld() => Node.Call("start_world").AsBool();
        public void StopWorld() => Node.Call("stop_world");
        public bool Step() => Node.Call("step").AsBool();

        public bool Paused
        {
            get => Node.Get("paused").AsBool();
            set => Node.Set("paused", value);
        }
        public double TicksPerSecond
        {
            get => Node.Get("ticks_per_second").AsDouble();
            set => Node.Set("ticks_per_second", value);
        }
        public string GameFolder
        {
            get => Node.Get("game_folder").AsString();
            set => Node.Set("game_folder", value);
        }
        /// <summary>Mod folders loaded on top of the game folder, in order.</summary>
        public string[] ModFolders
        {
            get => Node.Get("mod_folders").AsStringArray();
            set => Node.Set("mod_folders", value);
        }
    }
}
