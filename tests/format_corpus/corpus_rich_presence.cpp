// -Rich.txt: load and save through RichPresenceEditor, which normalizes the script the way
// RAIntegration does and refuses to save a script rcheevos rejects.

#include "corpus_support.hpp"

#include "cw/devtools/rich_presence_editor.hpp"

namespace cw_corpus {

void run_rich_presence(const std::filesystem::path& input, const std::filesystem::path& out,
                       Results& results) {
    for (const auto& source : files_in(input / "rich", ".txt")) {
        const std::string name = source.stem().string();
        const auto file = out / "rich" / source.filename();
        write_file(file, read_file(source));

        cw::RichPresenceEditor editor;
        std::string error;
        if (!editor.load(file, error)) {
            results.add("rich", name, "load failed: " + error);
            continue;
        }
        if (!editor.validation_error().empty()) {
            results.add("rich", name, "invalid: " + editor.validation_error());
        }
        if (editor.save(error)) {
            results.add("rich", name, "saved");
        } else {
            results.add("rich", name, "save refused: " + error);
        }
    }
}

} // namespace cw_corpus
