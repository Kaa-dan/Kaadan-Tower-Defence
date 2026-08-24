#pragma once

// The shipped content, loaded once for the whole test binary.
//
// Tests are not game/ and may read files, and having them run against the real
// content directory rather than a fixture means every `ctest` run also proves
// the shipped balance files parse, resolve and describe a routable level. A
// content typo therefore fails the build instead of the game.

#include "game/content/Database.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

inline std::string readWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::printf("cannot open content file %s\n", path.c_str());
        std::abort();
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

// KTD_CONTENT_DIR is set by tests/CMakeLists.txt so this does not depend on the
// directory ctest happened to be invoked from.
inline const ktd::content::ContentDb& shippedContent() {
    static const ktd::content::ContentDb db = [] {
        static const char* const kFiles[] = {"level.ktd", "towers.ktd", "enemies.ktd", "waves.ktd"};
        std::vector<std::string> names;
        std::vector<std::string> texts;
        for (const char* file : kFiles) {
            names.push_back(std::string(KTD_CONTENT_DIR) + "/" + file);
            texts.push_back(readWholeFile(names.back()));
        }

        std::vector<ktd::content::Source> sources;
        for (std::size_t i = 0; i < names.size(); ++i) {
            sources.push_back(ktd::content::Source{names[i], texts[i]});
        }

        ktd::content::ContentDb loaded;
        std::vector<ktd::content::Error> errors;
        if (!ktd::content::load(
                ktd::Span<const ktd::content::Source>(sources.data(), sources.size()), loaded,
                errors)) {
            std::printf("the shipped content does not load:\n");
            for (const ktd::content::Error& error : errors) {
                std::printf("  %s\n", error.format().c_str());
            }
            std::abort();
        }
        return loaded;
    }();
    return db;
}

}  // namespace testing
