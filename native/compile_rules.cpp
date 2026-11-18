// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
//
// terni_compile_rules <game folder> [mod folder ...] <spirv cache folder>
// Loads a game folder, with mod folders on top if given, prints its errors and warnings
// (file:line: message) and compiles its rules into the SPIR-V cache. Needs no GPU.
// Exit code 0: the cache is complete.
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "terni/game.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <game folder> [mod folder ...] <spirv cache folder>\n", argv[0]);
        return 2;
    }
    try {
        const std::vector<std::string> folders(argv + 1, argv + argc - 1);
        const auto game = terni::load_game(folders);
        for (const auto& w : game->warnings) std::fprintf(stderr, "warning: %s\n", w.text().c_str());
        for (const auto& e : game->errors) std::fprintf(stderr, "error: %s\n", e.text().c_str());
        if (!game->ok()) return 1;
        const auto s = terni::compile_rules(*game, argv[argc - 1]);
        std::printf("rules: %u compiled, %u cached (%.1f ms)\n", s.compiled, s.cached, s.ms);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
