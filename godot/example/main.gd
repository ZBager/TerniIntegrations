# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# With the environment variable TERNI_EXAMPLE_TEST set to a result file, runs the
# self-test and quits.
extends Control

func _ready() -> void:
	var result := OS.get_environment("TERNI_EXAMPLE_TEST")
	if result.is_empty():
		return
	($Simulation as TerniSimulation).paused = true
	var passed := SelfTest.run("res://Galaxy", result, self)
	# Quit a little later: let the first frames finish.
	await get_tree().create_timer(0.5).timeout
	get_tree().quit(0 if passed else 1)
