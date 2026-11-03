#include "app_state.hpp"

namespace cw {

void report_message(UiState& ui, Notice& notice, const std::string& text, bool ok) {
    ui.status_message = text;
    ui.status_is_error = !ok;
    show_notice(notice, text, ok);
}

} // namespace cw
