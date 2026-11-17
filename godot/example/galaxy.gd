# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Follows the galaxy simulation that the Simulation node runs: subscribes to the planets'
# population, shows it on screen and in the output, and applies a modifier from the host.
extends Label

@onready var simulation: TerniSimulation = $"../Simulation"
var planet := -1
var pop := -1
var subscribed: TerniWorld  # the world pop belongs to


func _ready() -> void:
	if not OS.get_environment("TERNI_EXAMPLE_TEST").is_empty():
		return  # main.gd runs the self-test instead
	# Every time a world starts: subscriptions belong to one world.
	simulation.started.connect(setup)
	simulation.tick_completed.connect(on_tick)
	simulation.failed.connect(func(error: String): text = "The simulation stopped:\n" + error)
	if simulation.is_running():
		setup()


func setup() -> void:
	var world := simulation.get_world()
	subscribed = world
	planet = world.entity("planet")
	pop = world.subscribe(planet, "pop")  # comes back with every tick
	# A host action: ten years of good harvests on Sicilia.
	var sicilia := world.find_row(planet, "sicilia")
	world.add_modifier(world.find_bundle("good_harvest"), planet, sicilia, TerniWorld.RATE_ONE, 120)
	print("Galaxy runs on %s: %d planets." % [world.get_device_name(), world.get_count(planet)])


func on_tick() -> void:
	var world := simulation.get_world()
	if world != subscribed:
		setup()
	var values := world.get_subscribed(pop)
	var lines := PackedStringArray()
	lines.append("Terni galaxy on %s" % world.get_device_name())
	lines.append("Tick %d, %.3f ms on the device per tick" % [world.get_ticks(), world.get_last_tick_gpu_ms()])
	lines.append("")
	for row in values.size():
		lines.append("%s: %.1f" % [world.get_row_id(planet, row), TerniWorld.to_float(values[row], TerniWorld.FIELD_QUANTITY)])
	text = "\n".join(lines)
	if world.get_ticks() % 20 == 0:
		print("tick %d: %s" % [world.get_ticks(), " ".join(lines.slice(3))])
