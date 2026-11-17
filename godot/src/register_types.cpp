// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "terni_simulation.h"
#include "terni_world.h"

using namespace godot;

namespace {

void initialize_terni(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(TerniWorld);
    GDREGISTER_CLASS(TerniSimulation);
}

void uninitialize_terni(ModuleInitializationLevel level) {
    // Worlds hold a Vulkan device: none may outlive the extension.
    if (level == MODULE_INITIALIZATION_LEVEL_SCENE) TerniWorld::unload_all();
}

}  // namespace

extern "C" GDExtensionBool GDE_EXPORT terni_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                                         GDExtensionClassLibraryPtr library,
                                                         GDExtensionInitialization* initialization) {
    GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(initialize_terni);
    init.register_terminator(uninitialize_terni);
    init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
