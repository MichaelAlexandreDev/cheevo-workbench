// -User.txt: load and save through UserFileStore (what the editors use), then serialize an edited
// copy, so both the passthrough path and the path that writes fields from scratch are frozen.

#include "corpus_support.hpp"

#include "cw/devtools/achievement_logic.hpp"
#include "cw/devtools/user_file.hpp"
#include "cw/devtools/user_file_store.hpp"

#include <stdexcept>
#include <variant>

namespace cw_corpus {
namespace {

cw::AchievementLogic logic(std::string_view text) {
    std::string error;
    auto parsed = cw::parse_achievement_logic(text, error);
    if (!parsed) {
        throw std::runtime_error("Corpus logic does not parse: " + error);
    }
    return *parsed;
}

// Changes one field of the first achievement and the first leaderboard, then appends a new one
// of each with every field set.
void edit(cw::UserFileDocument& document) {
    bool achievement_done = false;
    bool leaderboard_done = false;
    for (auto& entry : document.entries) {
        if (auto* achievement = std::get_if<cw::LocalAchievement>(&entry.asset);
            achievement && !achievement_done) {
            achievement->title = "Edited: title";
            achievement->points = 25;
            achievement_done = true;
        } else if (auto* board = std::get_if<cw::LocalLeaderboard>(&entry.asset);
                   board && !leaderboard_done) {
            board->description = "Edited \"description\"";
            board->lower_is_better = !board->lower_is_better;
            leaderboard_done = true;
        }
    }

    cw::LocalAchievement achievement;
    achievement.logic = logic("0xH0001=1_0xH0002>=3.10.");
    achievement.title = "New achievement";
    achievement.description = "Added by the corpus: 100%";
    achievement.type = "progression";
    achievement.author = "corpus";
    achievement.points = 10;
    cw::UserFileEntry added_achievement;
    added_achievement.asset = achievement;
    document.entries.push_back(added_achievement);

    cw::LocalLeaderboard board;
    board.start = logic("0xH0001=1");
    board.cancel = logic("0xH0001=2");
    board.submit = logic("0xH0001=3");
    board.value = "M:0xH0004*2";
    board.format = "SCORE";
    board.title = "New board";
    board.description = "Highest score";
    cw::UserFileEntry added_board;
    added_board.asset = board;
    document.entries.push_back(added_board);
}

} // namespace

void run_user_files(const std::filesystem::path& input, const std::filesystem::path& out,
                    Results& results) {
    for (const auto& source : files_in(input / "user", ".txt")) {
        const std::string name = source.stem().string();
        const auto file = out / "user" / source.filename();
        write_file(file, read_file(source));

        cw::UserFileStore store;
        std::string error;
        if (!store.load(file, error)) {
            results.add("user", name, "load failed: " + error);
            std::string save_error;
            const bool saved = store.save(cw::UserFileDocument{}, save_error);
            results.add("user", name,
                        saved ? "an empty document was saved over it"
                              : "save refused: " + save_error);
            continue;
        }
        if (!store.save(store.document(), error)) {
            results.add("user", name, "save failed: " + error);
            continue;
        }
        results.add("user", name,
                    "loaded and saved " + std::to_string(store.document().entries.size()) +
                        " entries");

        cw::UserFileDocument edited = store.document();
        edit(edited);
        const auto serialized = cw::serialize_user_file(edited, error);
        if (!serialized) {
            results.add("user", name, "edited copy does not serialize: " + error);
            continue;
        }
        write_file(out / "user" / (name + ".edited.txt"), *serialized);
    }
}

} // namespace cw_corpus
