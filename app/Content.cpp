#include "Content.h"

namespace ktd::app {

bool ContentSource::reload(const Platform& platform, content::ContentDb& out) {
    errors_.clear();

    // Load order does not matter -- the database resolves references in a
    // second pass -- so this list is only about which files exist.
    static const char* const kFiles[] = {"level.ktd", "towers.ktd", "enemies.ktd", "waves.ktd"};

    std::vector<std::string> names;
    std::vector<std::string> texts;
    for (const char* file : kFiles) {
        std::string path = directory_ + "/" + file;
        std::string text;
        std::string error;
        if (!platform.readFile(path, text, error)) {
            errors_.push_back(error);
            return false;
        }
        names.push_back(std::move(path));
        texts.push_back(std::move(text));
    }

    std::vector<content::Source> sources;
    sources.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        sources.push_back(content::Source{names[i], texts[i]});
    }

    // Built into a scratch database and only adopted once it is known good, so
    // a syntax error cannot leave the game running on half-loaded content.
    content::ContentDb loaded;
    std::vector<content::Error> problems;
    if (!content::load(Span<const content::Source>(sources.data(), sources.size()), loaded,
                       problems)) {
        for (const content::Error& problem : problems) {
            errors_.push_back(problem.format());
        }
        return false;
    }
    if (!loaded.usable()) {
        errors_.push_back("the content loaded but describes no playable level");
        return false;
    }

    out = std::move(loaded);
    return true;
}

void resimulate(World& world, const std::vector<Command>& commands, std::uint64_t throughTick) {
    std::size_t next = 0;
    for (std::uint64_t tick = 0; tick < throughTick; ++tick) {
        // Commands are applied before the step whose tick they were recorded
        // on, which is the order the live loop applies them in. Getting this
        // backwards would make a reload of unchanged content produce a
        // different run, which is the one thing it must never do.
        while (next < commands.size() && commands[next].tick == tick) {
            (void)world.placeTower(commands[next].cell, commands[next].tower);
            ++next;
        }
        world.step();
        if (world.state() != GameState::Playing) {
            // The rebalanced run ended earlier than the recorded one. Applying
            // the rest of the log to a finished world would be meaningless.
            break;
        }
    }
}

}  // namespace ktd::app
