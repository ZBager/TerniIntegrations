# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
@tool
extends EditorPlugin


func _exit_tree() -> void:
	TerniWorld.stop_all()
