# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
@tool
extends EditorPlugin

var _export: EditorExportPlugin
var _debugger: EditorDebuggerPlugin


func _enter_tree() -> void:
	_export = preload("terni_export.gd").new()
	add_export_plugin(_export)
	_debugger = preload("terni_debugger.gd").new()
	add_debugger_plugin(_debugger)


func _exit_tree() -> void:
	remove_export_plugin(_export)
	remove_debugger_plugin(_debugger)
	_export = null
	_debugger = null
	TerniWorld.stop_all()
