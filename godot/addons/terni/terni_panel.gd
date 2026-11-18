# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# The Terni tab of the Debugger panel: this machine's devices, the simulations of the
# running game (with the GPU time of each stage and a state hash per entity type, computed
# on the device), and the values of one field, refreshed twice a second.
@tool
extends VBoxContainer

signal watch_requested(node: String, entity: String, field: String)

var _devices := Label.new()
var _simulations := Label.new()
var _node := OptionButton.new()
var _entity := OptionButton.new()
var _field := OptionButton.new()
var _values := Label.new()
var _states := {}  # node path -> last state
var _hashes := {}  # node path -> last entity hashes (not sent while a tick is in flight)


func _init() -> void:
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(scroll)
	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(box)
	box.add_child(_heading("Devices"))
	box.add_child(_devices)
	box.add_child(_heading("Simulations"))
	box.add_child(_simulations)
	box.add_child(_heading("Field values"))
	var row := HBoxContainer.new()
	for option in [_node, _entity, _field]:
		option.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(option)
	box.add_child(row)
	box.add_child(_values)
	_node.item_selected.connect(func(_i): _fill_entities())
	_entity.item_selected.connect(func(_i): _fill_fields())
	_field.item_selected.connect(func(_i): _request_watch())
	_show_devices()
	clear_state()


func _heading(text: String) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", 16)
	return label


func _show_devices() -> void:
	var renderer := TerniWorld.find_renderer_device()
	var lines := PackedStringArray()
	for d in TerniWorld.get_devices():
		var line := "%d: %s (%s)" % [d.index, d.name, d.type]
		if d.index == renderer:
			line += ", the editor renders with it"
		if not d.usable:
			line += " - not usable: " + d.why_not
		elif d.robustness == TerniWorld.ROBUSTNESS_BOUNDED:
			line += ", reads and writes past a field's end are up to the driver"
		elif d.robustness == TerniWorld.ROBUSTNESS_NONE:
			line += ", reads and writes past a field's end are undefined"
		lines.append(line)
	_devices.text = "\n".join(lines) if not lines.is_empty() else "No Vulkan device found."


## A 64-bit hash as 16 hex digits; Godot ints are signed.
static func hex(value: int) -> String:
	return "%08x%08x" % [(value >> 32) & 0xffffffff, value & 0xffffffff]


func clear_state() -> void:
	_states.clear()
	_hashes.clear()
	_simulations.text = "Run the game: its TerniSimulation nodes show here."
	_values.text = ""
	_node.clear()
	_entity.clear()
	_field.clear()


func mark_stopped() -> void:
	if not _states.is_empty():
		_simulations.text += "\n(the game has stopped)"


func show_state(state: Dictionary) -> void:
	var known := _states.has(state.node)
	_states[state.node] = state
	if state.has("hashes"):
		_hashes[state.node] = state.hashes
	var lines := PackedStringArray()
	for node in _states:
		var s: Dictionary = _states[node]
		var hashes: Dictionary = _hashes.get(node, {})
		lines.append("%s - %s" % [node, s.folder])
		if s.running:
			lines.append("    %s, tick %d, %.3f ms on the device per tick%s" % [s.device, s.ticks, s.gpu_ms, " (the device is busy)" if s.slow else ""])
			for e in s.entities:
				var alive: String = ", %d alive" % e.alive if e.dynamic else ""
				var hash: String = ", state %s" % hex(hashes[e.name]) if hashes.has(e.name) else ""
				lines.append("    %s: %d of %d%s%s" % [e.name, e.count, e.capacity, alive, hash])
			for t in s.get("stages", []):
				var average: float = t.total_ms / t.runs if t.runs > 0 else 0.0
				lines.append("    stage %s: %.3f ms, %.3f ms on average%s" % [t.name, t.last_ms, average, "" if t.ran else " (did not run in the last tick)"])
		elif s.starting:
			lines.append("    starting")
		elif s.get("device_lost", false):
			lines.append("    the device was lost: " + s.error)
		else:
			lines.append("    not running" + (": " + s.error if not s.error.is_empty() else ""))
	_simulations.text = "\n".join(lines)
	if not known:
		_node.add_item(state.node)
		if _node.item_count == 1:
			_node.select(0)
			_fill_entities()
	if _entity.item_count == 0 and _selected_node() == state.node:
		_fill_entities()
	if state.has("watched"):
		_show_values(state.watched)


func _selected_node() -> String:
	return _node.get_item_text(_node.selected) if _node.selected >= 0 else ""


func _fill_entities() -> void:
	_entity.clear()
	var state: Dictionary = _states.get(_selected_node(), {})
	for e in state.get("entities", []):
		_entity.add_item(e.name)
	if _entity.item_count > 0:
		_entity.select(0)
	_fill_fields()


func _fill_fields() -> void:
	_field.clear()
	var state: Dictionary = _states.get(_selected_node(), {})
	for e in state.get("entities", []):
		if _entity.selected >= 0 and e.name == _entity.get_item_text(_entity.selected):
			for f in e.fields:
				_field.add_item(f)
	if _field.item_count > 0:
		_field.select(0)
	_request_watch()


func _request_watch() -> void:
	if _node.selected >= 0 and _entity.selected >= 0 and _field.selected >= 0:
		watch_requested.emit(_selected_node(), _entity.get_item_text(_entity.selected), _field.get_item_text(_field.selected))


func _show_values(w: Dictionary) -> void:
	var per_value := 2 if w.type == TerniWorld.FIELD_MONEY64 else 1
	var size := maxi(int(w.size), 1)
	var words: PackedInt32Array = w.words
	var lines := PackedStringArray()
	for row in w.ids.size():
		var line: String = "%d %s:" % [row, w.ids[row]] if not w.ids[row].is_empty() else "%d:" % row
		for k in size:
			var at: int = (row * size + k) * per_value
			if at + per_value > words.size():
				break
			if w.type == TerniWorld.FIELD_MONEY64:
				line += " %d" % TerniWorld.to_int64(words[at], words[at + 1])
			elif w.type == TerniWorld.FIELD_QUANTITY or w.type == TerniWorld.FIELD_RATE:
				line += " %s" % str(snappedf(TerniWorld.to_float(words[at], w.type), 0.0001))
			else:
				line += " %d" % int(TerniWorld.to_float(words[at], w.type))
		lines.append(line)
	if w.rows > w.ids.size():
		lines.append("... and %d more rows" % (w.rows - w.ids.size()))
	_values.text = "%s.%s\n%s" % [w.entity, w.field, "\n".join(lines)]
