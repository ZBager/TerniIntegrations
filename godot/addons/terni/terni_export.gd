# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Puts Terni game folders beside an exported game. A game folder is a folder of the
# project with a "schema" and a "rules" folder in it (and a .gdignore, so that Godot does
# not import the rules). Godot leaves such folders out of the exported package, so each is
# copied, with its rules compiled into its "spirv" folder, to the same path relative to
# the exported program: the res:// path a TerniSimulation has works there unchanged. The
# rule compiler, the libraries it needs and Terni-NOTICES.txt go beside the program too.
@tool
extends EditorExportPlugin

const SKIPPED := ["addons", ".godot"]
const MAX_DEPTH := 4


func _get_name() -> String:
	return "Terni"


func _export_begin(_features: PackedStringArray, _is_debug: bool, path: String, _flags: int) -> void:
	var target := path if path.is_absolute_path() else ProjectSettings.globalize_path("res://" + path)
	target = target.get_base_dir()
	var root := ProjectSettings.globalize_path("res://")
	for folder in find_game_folders():
		var relative := folder.trim_prefix(root)
		var result := TerniWorld.compile_rules(folder, folder.path_join("spirv"))
		if not result.ok:
			push_error("[Terni] %s is not in the exported game, its rules did not compile:\n%s" % [relative, result.output])
			continue
		var into := target.path_join(relative)
		if DirAccess.dir_exists_absolute(into):
			_remove(into)
		_copy(folder, into)
		print("[Terni] %s is in the exported game (%s)" % [relative, result.output])
	# What ships beside the extension library (Godot exports the library itself).
	var bin := ProjectSettings.globalize_path("res://addons/terni/bin")
	for file in DirAccess.get_files_at(bin):
		if file.begins_with("libterni.") or file.ends_with(".import") or file.ends_with(".uid"):
			continue
		DirAccess.copy_absolute(bin.path_join(file), target.path_join(file))
		if file.begins_with("terni_compile_rules") and not file.ends_with(".exe"):
			FileAccess.set_unix_permissions(target.path_join(file),
				FileAccess.UNIX_READ_OWNER | FileAccess.UNIX_WRITE_OWNER | FileAccess.UNIX_EXECUTE_OWNER
				| FileAccess.UNIX_READ_GROUP | FileAccess.UNIX_EXECUTE_GROUP | FileAccess.UNIX_READ_OTHER | FileAccess.UNIX_EXECUTE_OTHER)


## The game folders of the project, as paths on disk.
static func find_game_folders() -> PackedStringArray:
	var found := PackedStringArray()
	var root := ProjectSettings.globalize_path("res://")
	for name in DirAccess.get_directories_at(root):
		if not name.begins_with(".") and not SKIPPED.has(name):
			_find(root.path_join(name), 1, found)
	found.sort()
	return found


static func _find(folder: String, depth: int, found: PackedStringArray) -> void:
	if DirAccess.dir_exists_absolute(folder.path_join("schema")) and DirAccess.dir_exists_absolute(folder.path_join("rules")):
		found.append(folder)
		return
	if depth >= MAX_DEPTH:
		return
	for name in DirAccess.get_directories_at(folder):
		if not name.begins_with("."):
			_find(folder.path_join(name), depth + 1, found)


static func _copy(from: String, to: String) -> void:
	DirAccess.make_dir_recursive_absolute(to)
	for file in DirAccess.get_files_at(from):
		DirAccess.copy_absolute(from.path_join(file), to.path_join(file))
	for name in DirAccess.get_directories_at(from):
		_copy(from.path_join(name), to.path_join(name))


static func _remove(folder: String) -> void:
	for file in DirAccess.get_files_at(folder):
		DirAccess.remove_absolute(folder.path_join(file))
	for name in DirAccess.get_directories_at(folder):
		_remove(folder.path_join(name))
	DirAccess.remove_absolute(folder)
