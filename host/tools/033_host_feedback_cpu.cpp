// CPU-only regression. Real input queries and extracted production geometry;
// no input_windows.cpp, window, input delivery, ImGui runtime or graphics API.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include "../deps/imgui/imgui.h"
// Standard-library headers are already parsed. Only this isolated fixture can
// seed the production input object's current/previous event bytes.
#define private public
#include "../source/input.cpp"
#undef private
bool reshade::input::is_keyboard_layout_german() { return false; }

namespace {
int checks = 0, failures = 0;
void check(bool condition, const char *name) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL: %s\n", name); }
}
using input = reshade::input;
void clear(input &state) {
    std::fill(std::begin(state._keys), std::end(state._keys), uint8_t(0));
    std::fill(std::begin(state._last_keys), std::end(state._last_keys), uint8_t(0));
}
void fresh(input &state, unsigned key) { state._keys[key] = 0x88; }
void held(input &state, unsigned key) { state._keys[key] = state._last_keys[key] = 0x80; }
bool shortcut(input &state, bool was_open = false, bool ignore = false, unsigned active = 0) {
    const auto *_input = &state;
    const bool _show_overlay = was_open, _ignore_shortcuts = ignore, _force_shortcut_modifiers = false;
    const unsigned _overlay_key_data[4] = { input::key_home, 0, 0, 0 };
    const int _input_processing_mode = 2;
    struct Context { unsigned ActiveId; struct { bool WantCaptureMouse, WantCaptureKeyboard, NavVisible; } IO; } context { active, { true, true, false } };
    const auto *_imgui_context = &context;
    bool show_overlay = was_open;
#include "host_shortcut_under_test.inl"
    return show_overlay;
}
struct Geometry {
    ImVec2 pos, size, minimum, maximum, pivot;
    ImGuiCond pos_condition = 0, size_condition = 0;
    unsigned calls = 0;
} geometry;
ImGuiViewport viewport;
float font_size = 16.0f;
}

// Match the real declarations in the pinned ImGui header. These functions only
// capture arguments from the extracted production block; no ImGui context exists.
namespace ImGui {
ImGuiViewport *GetMainViewport() { return &viewport; }
float GetFontSize() { return font_size; }
void SetNextWindowPos(const ImVec2 &pos, ImGuiCond condition, const ImVec2 &pivot) {
    geometry.pos = pos; geometry.pos_condition = condition; geometry.pivot = pivot; ++geometry.calls;
}
void SetNextWindowSize(const ImVec2 &size, ImGuiCond condition) {
    geometry.size = size; geometry.size_condition = condition; ++geometry.calls;
}
void SetNextWindowSizeConstraints(const ImVec2 &minimum, const ImVec2 &maximum, ImGuiSizeCallback callback, void *data) {
    geometry.minimum = minimum; geometry.maximum = maximum; ++geometry.calls;
    check(callback == nullptr && data == nullptr, "geometry has no callback or external user data");
}
}

namespace {
void production_geometry() {
#include "host_geometry_under_test.inl"
}
bool equal(float a, float b) { return std::fabs(a - b) < .001f; }
void geometry_case(float width, float height, float font, ImVec2 origin, ImVec2 size, ImVec2 position) {
    viewport.Pos = origin; viewport.Size = ImVec2(width, height); font_size = font; geometry = {};
    production_geometry();
    check(equal(geometry.size.x, size.x) && equal(geometry.size.y, size.y), "YanYun three-column default size");
    check(equal(geometry.pos.x, position.x) && equal(geometry.pos.y, position.y), "YanYun right/top anchor with viewport origin");
    check(geometry.pos_condition == ImGuiCond_FirstUseEver && geometry.size_condition == ImGuiCond_FirstUseEver,
        "user placement is not overwritten every frame");
    check(geometry.calls == 3 && equal(geometry.pivot.x, 0) && equal(geometry.pivot.y, 0), "single position/size/bounds call and top-left pivot");
    check(geometry.minimum.x > 0 && geometry.minimum.y > 0 && geometry.minimum.x <= geometry.maximum.x && geometry.minimum.y <= geometry.maximum.y,
        "minimum bounds do not exceed maximum bounds");
    check(geometry.minimum.x <= geometry.size.x && geometry.minimum.y <= geometry.size.y, "default must not violate minimum bounds");
    check(geometry.size.x <= geometry.maximum.x && geometry.size.y <= geometry.maximum.y &&
        equal(geometry.maximum.x, width - 16) && equal(geometry.maximum.y, height - 16), "viewport bounds retain the original margin");
}
}

int main() {
    input state(nullptr);
    held(state, input::key_shift); fresh(state, input::key_backspace);
    check(shortcut(state), "Shift+Backspace opens");
    check(!shortcut(state, true), "Shift+Backspace closes");
    state._last_keys[input::key_backspace] = 0x80;
    check(!shortcut(state), "held Backspace repeat transition does not retrigger");
    state._keys[input::key_backspace] = 0x08;
    check(!shortcut(state), "release does not toggle");
    state._last_keys[input::key_backspace] = 0; fresh(state, input::key_backspace);
    check(shortcut(state), "fresh press after release toggles again");
    clear(state); fresh(state, input::key_shift); fresh(state, input::key_backspace);
    check(shortcut(state), "both chord keys first arrive this frame");
    clear(state); fresh(state, input::key_backspace); check(!shortcut(state), "plain Backspace is not a panel key");
    clear(state); fresh(state, input::key_shift); check(!shortcut(state), "plain Shift is not a panel key");
    held(state, input::key_ctrl); fresh(state, input::key_backspace); check(!shortcut(state), "Ctrl modifier is rejected");
    clear(state); held(state, input::key_shift); held(state, input::key_alt); fresh(state, input::key_backspace);
    check(!shortcut(state), "Alt modifier is rejected");
    clear(state); held(state, input::key_backspace); fresh(state, input::key_shift);
    check(!shortcut(state), "Shift after already held Backspace is not a fresh main-key press");
    clear(state); fresh(state, input::key_home); check(shortcut(state), "Home remains a panel key");
    clear(state); fresh(state, input::key_f11); check(!shortcut(state), "F11 does not toggle the panel");
    fresh(state, input::key_home); held(state, input::key_shift); fresh(state, input::key_backspace);
    check(shortcut(state), "Home and chord together still toggle once");
    check(!shortcut(state, false, true), "ignored shortcuts remain ignored");
    check(!shortcut(state, false, false, 7), "active-item shortcut guard remains in force");
    fresh(state, input::key_escape); check(!shortcut(state, true), "Escape close retains priority");

    geometry_case(1920, 1080, 16, ImVec2(0, 0), ImVec2(760, 780), ImVec2(1152, 8));
    geometry_case(1280, 720, 16, ImVec2(0, 0), ImVec2(760, 704), ImVec2(512, 8));
    geometry_case(3840, 2160, 32, ImVec2(0, 0), ImVec2(1520, 1560), ImVec2(2312, 8));
    geometry_case(800, 600, 12, ImVec2(0, 0), ImVec2(570, 584), ImVec2(222, 8));
    geometry_case(300, 200, 16, ImVec2(0, 0), ImVec2(284, 184), ImVec2(8, 8));
    geometry_case(320, 240, 32, ImVec2(0, 0), ImVec2(304, 224), ImVec2(8, 8));
    geometry_case(1920, 1080, 16, ImVec2(100, 50), ImVec2(760, 780), ImVec2(1252, 58));
    std::printf("Host feedback CPU: checks=%d failures=%d; no window, input delivery, DLL or GPU runtime\n", checks, failures);
    return failures ? 1 : 0;
}
