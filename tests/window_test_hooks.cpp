// ImGui's test engine hooks for cw_window_tests (built against imgui_test_hooks, never shipped).
// ImGui calls them for every widget while TestEngineHookItems is on; they must be defined once in
// the executable, so every window test shares this file.

#include "window_test_support.hpp"

namespace cw_test {

std::vector<RecordedItem>& recorded_items() {
    static std::vector<RecordedItem> items;
    return items;
}

} // namespace cw_test

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* context, ImGuiID id, const ImRect& rect,
                                 const ImGuiLastItemData*) {
    const ImGuiWindow* window = context->CurrentWindow;
    cw_test::recorded_items().push_back({id, rect, window ? window->RootWindow->Name : "", ""});
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label,
                                  ImGuiItemStatusFlags) {
    auto& items = cw_test::recorded_items();
    for (auto item = items.rbegin(); item != items.rend(); ++item) {
        if (item->id == id) {
            item->label = label ? label : "";
            return;
        }
    }
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) {
    return nullptr;
}
