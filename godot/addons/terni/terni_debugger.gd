# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Adds a Terni tab to the editor's Debugger panel. The game runs in its own process:
# its TerniSimulation nodes send their state over the debugger connection twice a second
# (see TerniSimulation::report_to_debugger), and the tab asks for the values of one field.
@tool
extends EditorDebuggerPlugin

const TerniPanel := preload("terni_panel.gd")

var _panels := {}  # session id -> panel


func _has_capture(prefix: String) -> bool:
	return prefix == "terni"


func _capture(message: String, data: Array, session_id: int) -> bool:
	if message == "terni:state" and _panels.has(session_id) and data.size() > 0:
		_panels[session_id].show_state(data[0])
		return true
	return false


func _setup_session(session_id: int) -> void:
	var panel := TerniPanel.new()
	panel.name = "Terni"
	var session := get_session(session_id)
	panel.watch_requested.connect(func(node: String, entity: String, field: String):
		session.send_message("terni:watch", [node, entity, field]))
	session.started.connect(panel.clear_state)
	session.stopped.connect(panel.mark_stopped)
	session.add_session_tab(panel)
	_panels[session_id] = panel
