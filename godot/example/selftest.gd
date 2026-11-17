# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Checks the extension against what Terni gives on its own: the state hash of the galaxy
# example after 200 ticks, on every device, with blocking and background ticks, and a
# save that continues to the same state. Writes the result to a file.
class_name SelfTest

## State hash of the galaxy example after 200 ticks, as terni_run prints it.
const EXPECTED := "ad93f051a2965dfb"

static var _log := PackedStringArray()
static var _ok := true


static func check(condition: bool, what: String) -> void:
	_log.append(("ok    " if condition else "FAIL  ") + what)
	_ok = _ok and condition


static func hex(value: int) -> String:
	# A 64-bit hash as 16 hex digits; Godot ints are signed.
	var high := (value >> 32) & 0xffffffff
	var low := value & 0xffffffff
	return "%08x%08x" % [high, low]


static func run(folder: String, result_file: String) -> bool:
	_log.clear()
	_ok = true
	var devices := TerniWorld.get_devices()
	var usable := devices.filter(func(d): return d.usable)
	check(usable.size() > 0, "%d devices, at least one usable" % devices.size())

	for device in usable:
		var name: String = device.name
		var world := TerniWorld.new()
		check(world.load(folder), "[%s] load" % name)
		check(world.begin_start({"device": device.index}), "[%s] begin start" % name)
		var polls := 0
		while not world.start_ready():
			polls += 1
		check(world.is_running() and not world.is_starting(), "[%s] started in the background (%d polls): %s" % [name, polls, world.get_last_error()])
		if not world.is_running():
			continue  # the rest would only repeat that
		var planet := world.entity("planet")
		var pop := world.subscribe(planet, "pop")

		# 100 blocking ticks, a save, then 100 background ticks.
		for i in 100:
			world.tick()
		var save := world.save()
		var middle := world.get_state_hash()
		for i in 100:
			world.begin_tick()
			while not world.tick_ready():
				pass
			world.end_tick()
		var end := world.get_state_hash()
		check(world.get_ticks() == 200, "[%s] 200 ticks" % name)
		check(hex(end) == EXPECTED, "[%s] state hash %s, expected %s" % [name, hex(end), EXPECTED])

		var read := world.read_field(planet, "pop")
		var subscribed := world.get_subscribed(pop)
		check(subscribed.size() == world.get_count(planet) and subscribed == read.slice(0, subscribed.size()),
			"[%s] subscribed pop equals read pop (%d rows)" % [name, subscribed.size()])

		check(world.load_save(save) and world.get_ticks() == 100 and world.get_state_hash() == middle, "[%s] save restores tick 100" % name)
		for i in 100:
			world.tick()
		check(hex(world.get_state_hash()) == EXPECTED, "[%s] the save continues to the same hash" % name)

		var sicilia := world.find_row(planet, "sicilia")
		check(sicilia >= 0 and world.get_row_id(planet, sicilia) == "sicilia", "[%s] rows by id" % name)
		check(world.write_value(planet, "pop", sicilia, TerniWorld.from_float(1234.5, TerniWorld.FIELD_QUANTITY)), "[%s] write" % name)
		var key := world.find_modifier_key("pop_growth")
		var before := world.get_modifier_total(key, planet, sicilia)
		var modifier := world.add_modifier(world.find_bundle("good_harvest"), planet, sicilia)
		world.tick()
		check(world.read_field(planet, "pop", sicilia, 1)[0] != 0 and world.is_modifier_alive(modifier)
			and world.get_modifier_total(key, planet, sicilia) != before
			and world.get_modifier_breakdown(key, planet, sicilia).size() > 0, "[%s] modifier added and counted" % name)
		check(not world.write_value(planet, "no_such_field", 0, 1) and not world.get_last_error().is_empty(), "[%s] a bad field is an error, not a crash" % name)
		check(world.read_field(-1, "pop").is_empty(), "[%s] a bad entity is an error, not a crash" % name)
		world.stop()
		check(not world.is_running() and not world.tick(), "[%s] stopped" % name)

	# The GPU the engine renders with.
	var renderer := TerniWorld.find_renderer_device()
	var driver := RenderingServer.get_current_rendering_driver_name()
	var rd := RenderingServer.get_rendering_device()
	_log.append("      the engine renders with %s on %s: Terni device %d" % [driver, rd.get_device_name() if rd else "no rendering device", renderer])
	if rd and usable.any(func(d): return d.name == rd.get_device_name()):
		check(renderer >= 0, "the renderer's GPU is found among Terni's devices")
	if renderer >= 0:
		var renderer_name: String = devices[renderer].name
		var world := TerniWorld.new()
		var preference := TerniWorld.PREFER_DISCRETE if devices[renderer].is_cpu else TerniWorld.PREFER_CPU
		check(world.load(folder) and world.start({"preference": preference}) and world.get_device_name() == renderer_name,
			"by default the simulation runs on the renderer's GPU (%s)" % world.get_device_name())
		var cpu = usable.filter(func(d): return d.is_cpu and d.index != renderer)
		if not cpu.is_empty():
			check(world.start({"ignore_renderer": true, "preference": TerniWorld.PREFER_CPU}) and world.get_device_name() == cpu[0].name,
				"ignore_renderer lets the preference decide (%s)" % world.get_device_name())
		var other = usable.filter(func(d): return d.index != renderer)
		if not other.is_empty():
			check(world.start({"device": other[0].index}) and world.get_device_name() == other[0].name,
				"a device given wins over the renderer's GPU (%s)" % world.get_device_name())
		world.stop()

	# A call made while a start is in flight waits for it.
	var eager := TerniWorld.new()
	check(eager.load(folder) and eager.begin_start() and eager.tick() and eager.get_ticks() == 1, "a tick right after begin_start waits for the start")
	eager.stop()

	var missing := TerniWorld.new()
	check(not missing.load(folder + "_missing") and not missing.get_last_error().is_empty(), "a missing folder is an error")

	_log.append("PASSED" if _ok else "FAILED")
	var file := FileAccess.open(result_file, FileAccess.WRITE)
	file.store_string("\n".join(_log) + "\n")
	file.close()
	print("Terni self-test:\n" + "\n".join(_log))
	return _ok
