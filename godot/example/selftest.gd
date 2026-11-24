# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Checks the extension against what Terni gives on its own: the state hash of the galaxy
# example after 200 ticks, on every device, with blocking and background ticks, and a
# save that continues to the same state. Then hashes on the device, stage times, a lost
# device and a mod. Writes the result to a file.
class_name SelfTest

## State hash of the galaxy example after 200 ticks, as terni_run prints it.
const EXPECTED := "ad93f051a2965dfb"
## A mod for the galaxy example: other growth, a new bundle and a new entity type.
const MOD := "res://Mods/FasterGrowth"

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


## `parent`: a node to add a TerniSimulation to, for the checks of the node.
static func run(folder: String, result_file: String, parent: Node) -> bool:
	_log.clear()
	_ok = true
	var devices := TerniWorld.get_devices()
	var usable := devices.filter(func(d): return d.usable)
	check(usable.size() > 0, "%d devices, at least one usable" % devices.size())
	check(usable.all(func(d): return d.robustness >= TerniWorld.ROBUSTNESS_NONE and d.robustness <= TerniWorld.ROBUSTNESS_DEFINED),
		"buffer robustness per device: %s" % ", ".join(usable.map(func(d): return ["none", "bounded", "defined"][d.robustness])))
	if OS.get_environment("TERNI_EXPECT_SOFTWARE_DRIVER") == "1":
		check(not TerniWorld.get_software_driver().is_empty() and devices.all(func(d): return d.is_cpu),
			"no GPU: the software driver shipped with the game is added (%s)" % TerniWorld.get_software_driver())
	var first_hashes := {}
	var first_device := ""

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

		# Hashes computed on the device: the same on every device.
		var hashes := world.get_field_hashes()
		var entity_hashes := world.get_entity_hashes()
		check(hashes.has("planet.pop") and entity_hashes.has("planet") and entity_hashes.has("terni"),
			"[%s] hashes on the device: %d fields, %d entity types" % [name, hashes.size(), entity_hashes.size()])
		if first_device.is_empty():
			first_hashes = hashes
			first_device = name
		else:
			check(hashes == first_hashes, "[%s] the same field hashes as on %s" % [name, first_device])
		var stages := world.get_stage_times()
		check(stages.any(func(t): return t.name == "planet_growth" and t.ran and t.runs == 200),
			"[%s] GPU time of %d stages (planet_growth: %.4f ms)" % [name, stages.size(),
			stages.filter(func(t): return t.name == "planet_growth").map(func(t): return t.last_ms).front() if not stages.is_empty() else 0.0])

		var read := world.read_field(planet, "pop")
		var subscribed := world.get_subscribed(pop)
		check(subscribed.size() == world.get_count(planet) and subscribed == read.slice(0, subscribed.size()),
			"[%s] subscribed pop equals read pop (%d rows)" % [name, subscribed.size()])

		check(world.load_save(save) and world.get_ticks() == 100 and world.get_state_hash() == middle, "[%s] save restores tick 100" % name)
		for i in 100:
			world.tick()
		check(hex(world.get_state_hash()) == EXPECTED and world.get_field_hashes() == hashes, "[%s] the save continues to the same hash" % name)

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

	# Waits that spin change how long a tick's results take to arrive, never the results.
	var spinning := TerniWorld.new()
	check(spinning.load(folder) and spinning.start({"spin_us": 200}), "a world starts with spin_us")
	for t in 200:
		spinning.tick()
	check(hex(spinning.get_state_hash()) == EXPECTED, "waits that spin: the same state hash")
	spinning.stop()
	var spinning_node := TerniSimulation.new()
	spinning_node.spin_us = 200
	check(spinning_node.spin_us == 200, "the node has spin_us")
	spinning_node.free()

	var missing := TerniWorld.new()
	check(not missing.load(folder + "_missing") and not missing.get_last_error().is_empty(), "a missing folder is an error")
	check(not missing.load(folder, [MOD + "_missing"]) and not missing.get_last_error().is_empty(), "a missing mod folder is an error")

	_check_device_lost(folder, parent)
	_check_mod(folder)
	_check_start_state(folder)
	_check_order_and_cache(folder)

	_log.append("PASSED" if _ok else "FAILED")
	var file := FileAccess.open(result_file, FileAccess.WRITE)
	file.store_string("\n".join(_log) + "\n")
	file.close()
	print("Terni self-test:\n" + "\n".join(_log))
	return _ok


# A lost device, simulated: a real one would reset the GPU.
static func _check_device_lost(folder: String, parent: Node) -> void:
	var world := TerniWorld.new()
	var signalled := []
	world.device_lost.connect(func(error: String): signalled.append(error))
	if not world.load(folder) or not world.start():
		check(false, "lost device: the world starts: " + world.get_last_error())
		return
	for i in 100:
		world.tick()
	var autosave := world.save()
	world.simulate_device_lost()
	check(not world.tick() and world.is_device_lost() and not world.is_running() and signalled.size() == 1,
		"a lost device stops the world and is signalled: %s" % world.get_last_error())
	check(not world.tick() and signalled.size() == 1, "calls after it fail, without another signal")
	check(world.start() and not world.is_device_lost() and world.load_save(autosave), "the world starts again and loads the last save")
	for i in 100:
		world.tick()
	check(hex(world.get_state_hash()) == EXPECTED, "and goes on to the same state")
	# Found while a tick is in flight.
	world.begin_tick()
	world.simulate_device_lost()
	while not world.tick_ready():
		pass
	check(world.is_device_lost() and not world.is_tick_in_flight() and signalled.size() == 2, "a device lost during a background tick is found")
	world.stop()

	# TerniSimulation: device_lost, not failed.
	var node := TerniSimulation.new()
	node.game_folder = folder
	node.start_in_background = false
	node.paused = true
	var events := []
	node.device_lost.connect(func(_error: String): events.append("device_lost"))
	node.failed.connect(func(_error: String): events.append("failed"))
	parent.add_child(node)
	check(node.is_running(), "a TerniSimulation node starts: " + node.get_last_error())
	node.get_world().simulate_device_lost()
	check(not node.step() and node.is_device_lost() and events == ["device_lost"] and not node.get_last_error().is_empty(),
		"TerniSimulation emits device_lost, not failed (%s)" % ", ".join(events))
	node.queue_free()


# The game's folder with a mod on top: its rules compiled, saves of the game alone loaded
# by name, and the mod's entity type, whose rows come and go.
static func _check_mod(folder: String) -> void:
	var game := TerniWorld.new()
	var modded := TerniWorld.new()
	if not game.load(folder) or not game.start():
		check(false, "mod: the game starts: " + game.get_last_error())
		return
	for i in 100:
		game.tick()
	var save := game.save()
	check(modded.load(folder, [MOD]) and modded.get_mod_folders().size() == 1 and modded.get_checksum() != game.get_checksum()
		and modded.find_bundle("festival") >= 0 and modded.find_entity("fleet") >= 0,
		"a mod loads on top of the game: its bundle, its entity type, another checksum")
	check(modded.start(), "the game with the mod starts, the mod's rules compiled: " + modded.get_last_error())
	if not modded.is_running():
		return
	check(TerniWorld.save_checksum(save) == game.get_checksum() and TerniWorld.save_checksum(PackedByteArray([1, 2, 3])) == 0,
		"save_checksum names the game a save was made with")
	check(not modded.load_save(save), "a save of the game alone is refused as it is")
	check(modded.load_save(save, true) and modded.get_ticks() == 100,
		"and loads by name: %s" % "; ".join(modded.get_fitted_lines()))
	for i in 100:
		game.tick()
		modded.tick()
	check(hex(game.get_state_hash()) == EXPECTED and modded.get_state_hash() != game.get_state_hash(), "the mod's rules run")

	var fleet := modded.entity("fleet")
	var free := modded.get_free_count(fleet)
	var row := modded.create_entity(fleet)
	var bits := modded.get_alive_bits(fleet)
	check(free == 64 and row == 0 and modded.get_free_count(fleet) == 63 and bits.size() > 0 and bits[0] & 1 == 1,
		"free rows and alive bits of a dynamic entity type (%d free, row %d)" % [free, row])
	modded.destroy_entity(fleet, row)
	modded.tick()
	check(modded.get_alive_bits(fleet)[0] & 1 == 0 and modded.get_free_count(fleet) == 64, "a destroyed row is free again a tick later")
	check(modded.get_alive_bits(modded.entity("planet")).is_empty() and not modded.get_last_error().is_empty(),
		"alive bits of a type that is not dynamic are an error, not a crash")
	game.stop()
	modded.stop()


# A starting state changed before the start, and the modifier calls on many rows at once.
static func _check_start_state(folder: String) -> void:
	var world := TerniWorld.new()
	check(world.load(folder), "start state: the game loads")
	var planet := world.entity("planet")
	var empire := world.entity("empire")
	var harvest := world.find_bundle("good_harvest")
	var key := world.find_modifier_key("pop_growth")
	check(world.get_defines().has("growth_rate") and world.get_field_ref(planet, "owner") == empire and world.get_field_ref(planet, "pop") == -1,
		"defines by name, and what a ref field refers to")
	var checksum := world.get_checksum()
	check(world.set_modifier_capacity(2048) and world.get_checksum() != checksum and not world.set_modifier_capacity(0),
		"the modifier store's capacity is set, and is part of the checksum")
	var row := world.get_count(planet)
	var pop := TerniWorld.from_float(100, TerniWorld.FIELD_QUANTITY)
	check(not world.add_start_modifier(harvest, planet, row), "a start modifier on a row not in use is an error")
	check(world.set_start_count(planet, row + 2) and world.get_count(planet) == row + 2 and not world.set_start_count(planet, 1 << 30),
		"two more planets at the start")
	var instances := world.get_modifier_count()
	check(instances == 0, "no instances before the start")
	check(world.write_start(planet, "owner", row, PackedInt32Array([0, 0])) and world.write_start(planet, "pop", row, PackedInt32Array([pop, pop]))
		and world.read_start(planet, "pop", row, 2) == PackedInt32Array([pop, pop]) and world.read_start(planet, "pop").size() == world.get_capacity(planet),
		"their starting values are written and read")
	check(not world.write_start(empire, "stock", 0, PackedInt32Array([1, 2])) and not world.write_start(planet, "no_such_field", 0, PackedInt32Array([1])),
		"words that are not whole rows, and a bad field, are errors")
	check(world.add_start_modifier(harvest, planet, row), "a modifier at the start")
	check(world.start(), "the changed world starts: " + world.get_last_error())
	if not world.is_running():
		return
	check(not world.write_start(planet, "pop", row, PackedInt32Array([pop])), "a running world refuses a starting state")
	var reads := world.get_sync_reads()
	check(world.read_field(planet, "pop", row, 2) == PackedInt32Array([pop, pop]) and world.get_sync_reads() > reads,
		"the starting state is on the device, and the read was counted as one that waits")
	instances = world.get_modifier_count()
	var own := world.get_modifier_own_total(key, planet, row)
	check(instances >= 1 and own > 0 and world.get_modifier_own_total(key, planet, row + 1) == 0, "the start modifier is in its row's own total")

	var added := world.add_modifiers(harvest, planet, PackedInt32Array([0, row, row + 1]))
	check(added.size() == 3 and world.get_modifier_count() == instances + 3 and world.get_modifier_own_total(key, planet, row) == 2 * own
		and world.get_modifier_own_total(key, planet, row + 1) == own, "one bundle on three rows")
	check(world.add_modifiers(harvest, planet, PackedInt32Array([0, 1 << 30])).is_empty() and world.get_modifier_count() == instances + 3,
		"a bad row among many adds none of them")
	var replaced := world.replace_modifier(added[2], harvest, planet, row + 1, 2 * TerniWorld.RATE_ONE)
	check(replaced != added[2] and not world.is_modifier_alive(added[2]) and world.is_modifier_alive(replaced)
		and world.get_modifier_count() == instances + 3 and world.get_modifier_own_total(key, planet, row + 1) == 2 * own, "a modifier replaced by another")
	check(world.replace_modifier(replaced, -1, planet, row + 1) == replaced and world.is_modifier_alive(replaced), "a failed replacement keeps the old one")
	world.add_modifier(world.find_bundle("war_economy"), empire, 0)
	check(world.get_modifier_total(key, planet, row + 1) < world.get_modifier_own_total(key, planet, row + 1), "the total inherits, the own total does not")

	var ticks := world.get_ticks()
	check(world.tick_checked() and world.get_ticks() == ticks + 1 and world.get_order_issues().is_empty(), "a checked tick finds no order issues")
	world.stop()
	check(world.set_start_count(planet, row) and world.get_count(planet) == row, "rows can be given up again")


# A rule whose result depends on the order of its rows, and where its SPIR-V came from.
static func _check_order_and_cache(folder: String) -> void:
	var mod := "user://order_test_%d" % Time.get_ticks_usec()
	var rule := mod.path_join("rules/planet_growth.glsl")
	var cache := mod.path_join("spirv")
	DirAccess.make_dir_recursive_absolute(rule.get_base_dir())
	var file := FileAccess.open(rule, FileAccess.WRITE)
	file.store_string("void main() {\n    FOR_EACH_PLANET(p) planet_pop[p] += planet_pop[(p + 1u) % terni.planet_count] + int(p);\n}\n")
	file.close()
	var world := TerniWorld.new()
	check(world.load(folder, [mod]) and not world.get_warnings().is_empty(), "a rule that depends on order is a warning when it loads")
	check(world.start({"cache_folder": cache}), "it starts with an empty cache: " + world.get_last_error())
	var stats := world.get_compile_stats()
	check(stats.compiled >= 3 and stats.cached == 0 and stats.ms > 0, "its rules were compiled (%s)" % stats)
	check(world.tick_checked() and world.get_order_issues().any(func(i): return i.stage == "planet_growth" and i.field == "planet.pop" and i.rows > 0),
		"a checked tick names the stage and field: %s" % [world.get_order_issues()])
	check(world.start({"cache_folder": cache}) and world.get_compile_stats().compiled == 0 and world.get_compile_stats().cached >= 3,
		"the second start takes them from the cache (%s)" % world.get_compile_stats())
	world.stop()
	check(not world.tick_checked() and world.get_compile_stats().cached == 0, "a stopped world cannot be checked")
	for name in DirAccess.get_files_at(cache):
		DirAccess.remove_absolute(cache.path_join(name))
	DirAccess.remove_absolute(rule)
	for path in [cache, rule.get_base_dir(), mod]:
		DirAccess.remove_absolute(path)
