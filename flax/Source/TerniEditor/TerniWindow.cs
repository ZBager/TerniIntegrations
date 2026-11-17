// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
using System;
using System.Linq;
using System.Text;
using FlaxEditor;
using FlaxEditor.CustomEditors;
using FlaxEditor.CustomEditors.Elements;
using FlaxEngine;
using FlaxEngine.GUI;
using Terni;

namespace TerniEditor
{
    /// <summary>
    /// The Terni window: the devices of this machine, the worlds that are running, and the
    /// values of one field of one of them, refreshed twice a second.
    /// </summary>
    public class TerniWindow : CustomEditorWindow
    {
        private const int MaxRows = 64;

        private LabelElement _devices, _worlds, _values;
        private ComboBoxElement _world, _entity, _field;
        private TerniWorld[] _list = Array.Empty<TerniWorld>();
        private float _nextRefresh;
        private bool _bound;

        /// <inheritdoc />
        public override void Initialize(LayoutElementsContainer layout)
        {
            var devices = layout.Group("Devices");
            _devices = devices.Label(DevicesText());
            _devices.Label.AutoHeight = true;
            devices.Panel.Open(false);

            var worlds = layout.Group("Worlds");
            _worlds = worlds.Label("");
            _worlds.Label.AutoHeight = true;
            var stop = worlds.Button("Stop all", "Stops every world and frees its device.");
            stop.Button.Clicked += TerniWorld.StopAll;
            worlds.Panel.Open(false);

            var values = layout.Group("Field values");
            _world = values.ComboBox("World", "A world that is loaded or running.");
            _entity = values.ComboBox("Entity", "An entity type of that world.");
            _field = values.ComboBox("Field", "A field of that entity type.");
            _world.ComboBox.SelectedIndexChanged += box => FillEntities();
            _entity.ComboBox.SelectedIndexChanged += box => FillFields();
            _values = values.Label("");
            _values.Label.AutoHeight = true;
            values.Panel.Open(false);

            if (!_bound)
            {
                Scripting.Update += OnUpdate;
                _bound = true;
            }
            _nextRefresh = 0;
        }

        private void OnUpdate()
        {
            if (Window == null || Window.IsDisposing)
            {
                Scripting.Update -= OnUpdate;
                _bound = false;
                return;
            }
            if (Time.UnscaledGameTime < _nextRefresh || !Window.Visible)
                return;
            _nextRefresh = Time.UnscaledGameTime + 0.5f;
            try
            {
                RefreshNow();
            }
            catch (Exception e)
            {
                _values.Label.Text = e.Message;
            }
        }

        private static string DevicesText()
        {
            var text = new StringBuilder();
            foreach (var device in TerniWorld.GetDevices())
                text.AppendLine($"{device.Index}: {device.Name} ({device.Type})" + (device.Usable ? "" : " - not usable: " + device.WhyNot));
            return text.Length == 0 ? "No Vulkan device found." : text.ToString().TrimEnd();
        }

        private void RefreshNow()
        {
            var list = TerniWorld.GetWorlds();
            if (!list.SequenceEqual(_list))
            {
                _list = list;
                var box = _world.ComboBox;
                box.Items.Clear();
                box.Items.AddRange(list.Select(world => System.IO.Path.GetFileName(world.GameFolder)));
                box.SelectedIndex = list.Length > 0 ? 0 : -1;
                FillEntities();
            }

            var text = new StringBuilder();
            foreach (var world in list)
            {
                text.AppendLine(world.GameFolder);
                if (!world.IsRunning)
                {
                    text.AppendLine("    loaded, not running");
                    continue;
                }
                var health = world.Health;
                text.AppendLine($"    {world.DeviceName}, tick {world.Ticks}, {world.LastTickGpuMs:0.00} ms on the device per tick" + (health.Slow ? " (the device is busy)" : ""));
                var entities = world.GetEntities();
                for (var i = 0; i < entities.Length; i++)
                    text.AppendLine($"    {entities[i]}: {world.GetCount(i)} of {world.GetCapacity(i)}" + (world.IsDynamic(i) ? $", {world.GetAliveCount(i)} alive" : ""));
            }
            _worlds.Label.Text = text.Length == 0 ? "No world is loaded. Start play mode with a TerniSimulation script in the scene." : text.ToString().TrimEnd();

            _values.Label.Text = ValuesText();
        }

        private TerniWorld Selected()
        {
            var index = _world.ComboBox.SelectedIndex;
            return index >= 0 && index < _list.Length ? _list[index] : null;
        }

        private void FillEntities()
        {
            var box = _entity.ComboBox;
            box.Items.Clear();
            var world = Selected();
            if (world != null)
                box.Items.AddRange(world.GetEntities());
            box.SelectedIndex = box.Items.Count > 0 ? 0 : -1;
            FillFields();
        }

        private void FillFields()
        {
            var box = _field.ComboBox;
            box.Items.Clear();
            var world = Selected();
            if (world != null && _entity.ComboBox.SelectedIndex >= 0)
                box.Items.AddRange(world.GetFields(_entity.ComboBox.SelectedIndex));
            box.SelectedIndex = box.Items.Count > 0 ? 0 : -1;
        }

        private string ValuesText()
        {
            var world = Selected();
            var entity = _entity.ComboBox.SelectedIndex;
            var field = _field.ComboBox.SelectedItem;
            if (world == null || !world.IsRunning || entity < 0 || string.IsNullOrEmpty(field))
                return "";
            // Never while a tick is in flight: reading would wait for it and hold up the frame.
            if (world.TickInFlight)
                return _values.Label.Text;

            var type = world.GetFieldType(entity, field);
            var size = Math.Max(world.GetFieldSize(entity, field), 1);
            var perValue = type == TerniFieldType.Money64 ? 2 : 1;
            var rows = Math.Min(world.GetCount(entity), MaxRows);
            var words = world.ReadField(entity, field, 0, rows);
            var text = new StringBuilder();
            for (var row = 0; row < rows && (row + 1) * size * perValue <= words.Length; row++)
            {
                var id = world.GetRowId(entity, row);
                text.Append(string.IsNullOrEmpty(id) ? row.ToString() : $"{row} {id}").Append(':');
                for (var k = 0; k < size; k++)
                {
                    var at = (row * size + k) * perValue;
                    if (type == TerniFieldType.Money64)
                        text.Append(' ').Append(TerniValue.ToInt64(words[at], words[at + 1]));
                    else if (type == TerniFieldType.Quantity || type == TerniFieldType.Rate)
                        text.Append(' ').Append(TerniValue.ToDouble(words[at], type).ToString("0.####"));
                    else
                        text.Append(' ').Append(TerniValue.ToDouble(words[at], type).ToString("0"));
                }
                text.AppendLine();
            }
            if (world.GetCount(entity) > rows)
                text.AppendLine($"... and {world.GetCount(entity) - rows} more rows");
            return text.ToString().TrimEnd();
        }
    }
}
