#include "droneview/frontend.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include <GLES3/gl3.h>

#include "app/platform/platform.h"
#include "droneview/backend.h"

#include "backend/soft_keyboard.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui_console.h"

#include "droidsans_ttf.inc"
#include "icon_rgba.inc"

namespace dv::frontend {
namespace {

struct TouchEvent {
    float x;
    float y;
    int action;
};

std::atomic<bool> g_initialized{false};
bool g_enabled = false;
constexpr int kFovPresetCount = 3;
constexpr int kNoFovPreset = -1;
int g_fov_index = kNoFovPreset;
constexpr float kFovPresets[kFovPresetCount] = {40.0f, 60.0f, 80.0f};
float g_ui_scale = 1.4f;
float g_font_scale = 1.0f;
constexpr float kFontPixelSize = 18.0f;
constexpr float kFontSizeRatio = 0.028f;
constexpr float kFontPixelMin = 22.0f;
constexpr float kFontPixelMax = 38.0f;
constexpr float kUiScaleBase = 13.0f;
constexpr float kConsoleWidth = 600.0f;
constexpr float kConsoleLogHeight = 220.0f;
constexpr float kIconDisplaySize = 64.0f;
constexpr float kIconDragThreshold = 4.0f;
bool g_collapsed = false;
bool g_restore_position = false;
float g_restore_x = 0.0f;
float g_restore_y = 0.0f;
bool g_icon_position_valid = false;
float g_icon_x = 40.0f;
float g_icon_y = 40.0f;
bool g_icon_drag_moved = false;
float g_icon_drag_start_x = 0.0f;
float g_icon_drag_start_y = 0.0f;
float g_icon_window_start_x = 0.0f;
float g_icon_window_start_y = 0.0f;
GLuint g_icon_texture = 0;
std::unique_ptr<ImGuiConsole> g_console;
std::string g_ini_filename = "droneview.ini";
std::string g_log_filename = "droneview_.txt";
ImGuiStyle g_base_style;
bool g_display_configured = false;

std::mutex g_touch_mutex;
std::deque<TouchEvent> g_touch_events;
bool g_touch_active = false;

enum class InputEventType { Character, Key };
struct InputEvent {
    InputEventType type;
    unsigned int codepoint;
    int key;
    bool down;
};
std::mutex g_input_mutex;
std::deque<InputEvent> g_input_events;
std::atomic<bool> g_ctrl{false};
std::atomic<bool> g_shift{false};
std::atomic<bool> g_alt{false};
std::atomic<bool> g_text_input_wanted{false};
std::atomic<bool> g_console_view_active{false};

enum class MenuEventType { Key, Char };
struct MenuEvent {
    MenuEventType type;
    int key;
    unsigned int codepoint;
};
std::mutex g_menu_mutex;
std::deque<MenuEvent> g_menu_events;
std::atomic<bool> g_control_menu_active{false};
int g_control_menu_index = 0;
std::string g_control_menu_status;

std::mutex g_rect_mutex;
float g_menu_x = 0.0f;
float g_menu_y = 0.0f;
float g_menu_w = 0.0f;
float g_menu_h = 0.0f;
bool g_menu_rect_valid = false;
float g_console_input_x = 0.0f;
float g_console_input_y = 0.0f;
float g_console_input_w = 0.0f;
float g_console_input_h = 0.0f;
bool g_console_input_valid = false;
std::atomic<bool> g_console_focus_request{false};
std::atomic<bool> g_console_blur_request{false};

void record_menu_rect() {
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_rect_mutex);
    g_menu_x = pos.x;
    g_menu_y = pos.y;
    g_menu_w = size.x;
    g_menu_h = size.y;
    g_menu_rect_valid = true;
}

void record_console_input_rect() {
    ImVec2 min;
    ImVec2 max;
    const bool valid = g_console && g_console->GetInputRect(&min, &max);
    std::lock_guard<std::mutex> lock(g_rect_mutex);
    g_console_input_valid = valid;
    if (valid) {
        g_console_input_x = min.x;
        g_console_input_y = min.y;
        g_console_input_w = max.x - min.x;
        g_console_input_h = max.y - min.y;
    }
}

void clear_console_input_rect() {
    std::lock_guard<std::mutex> lock(g_rect_mutex);
    g_console_input_valid = false;
}

bool consume_console_focus_request() {
    return g_console_focus_request.exchange(false, std::memory_order_acq_rel);
}

bool consume_console_blur_request() {
    return g_console_blur_request.exchange(false, std::memory_order_acq_rel);
}

void apply_touch_events() {
    std::deque<TouchEvent> pending;
    {
        std::lock_guard<std::mutex> lock(g_touch_mutex);
        pending.swap(g_touch_events);
    }
    ImGuiIO& io = ImGui::GetIO();
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const TouchEvent& event = pending[i];
        if (event.action == 2 && !g_touch_active) {
            io.MousePos = ImVec2(event.x, event.y);
            continue;
        }
        if (event.action == 0) {
            io.MousePos = ImVec2(event.x, event.y);
            io.MouseDown[0] = true;
            g_touch_active = true;
            if (i + 1 < pending.size()) {
                std::lock_guard<std::mutex> lock(g_touch_mutex);
                for (std::size_t j = pending.size(); j > i + 1; --j) {
                    g_touch_events.push_front(pending[j - 1]);
                }
            }
            break;
        }
        if (event.action == 1 || event.action == 3) {
            io.MousePos = ImVec2(event.x, event.y);
            if (g_touch_active) {
                io.MouseDown[0] = false;
                g_touch_active = false;
            }
            break;
        }
        io.MousePos = ImVec2(event.x, event.y);
    }
}

void apply_key_events() {
    ImGuiIO& io = ImGui::GetIO();
    if (!g_text_input_wanted.load(std::memory_order_relaxed)) {
        g_ctrl.store(false, std::memory_order_relaxed);
        g_shift.store(false, std::memory_order_relaxed);
        g_alt.store(false, std::memory_order_relaxed);
    }
    io.KeyCtrl = g_ctrl.load(std::memory_order_relaxed);
    io.KeyShift = g_shift.load(std::memory_order_relaxed);
    io.KeyAlt = g_alt.load(std::memory_order_relaxed);

    std::deque<InputEvent> pending;
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        pending.swap(g_input_events);
    }

    // Characters before the next key transition are safe to apply together.
    // A single key transition is applied per frame so a quick down/up pair is
    // not collapsed before ImGui observes the press.
    std::size_t next = 0;
    for (; next < pending.size(); ++next) {
        const InputEvent& event = pending[next];
        if (event.type == InputEventType::Character) {
            io.AddInputCharacter(event.codepoint);
            continue;
        }
        const int index = io.KeyMap[event.key];
        if (index >= 0) {
            io.KeysDown[index] = event.down;
        }
        ++next;
        break;
    }
    if (next < pending.size()) {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        for (std::size_t i = pending.size(); i > next; --i) {
            g_input_events.push_front(pending[i - 1]);
        }
    }
}

void create_icon_texture() {
    if (g_icon_texture != 0) {
        return;
    }
    if (kIconRgbaLen != static_cast<unsigned int>(kIconWidth * kIconHeight * 4)) {
        app::platform::log_error("droneview", "icon: invalid RGBA size");
        return;
    }
    glGenTextures(1, &g_icon_texture);
    if (g_icon_texture == 0) {
        app::platform::log_error("droneview", "icon: glGenTextures failed");
        return;
    }
    glBindTexture(GL_TEXTURE_2D, g_icon_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kIconWidth, kIconHeight, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, kIconRgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    app::platform::log_info("droneview", "icon texture created: %dx%d",
                            kIconWidth, kIconHeight);
}

void destroy_icon_texture() {
    if (g_icon_texture != 0) {
        glDeleteTextures(1, &g_icon_texture);
        g_icon_texture = 0;
    }
}

bool control_menu_fov_selected() {
    return g_fov_index >= 0 && g_fov_index < kFovPresetCount;
}

float control_menu_fov_override() {
    return control_menu_fov_selected() ? kFovPresets[g_fov_index] : -1.0f;
}

int control_menu_fov_value() {
    return control_menu_fov_selected()
               ? static_cast<int>(kFovPresets[g_fov_index])
               : -1;
}

void minimize_overlay() {
    if (g_icon_texture == 0) {
        return;
    }
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    g_icon_x = std::max(0.0f, std::min(pos.x, display.x - kIconDisplaySize));
    g_icon_y = std::max(0.0f, std::min(pos.y, display.y - kIconDisplaySize));
    g_icon_position_valid = true;
    g_collapsed = true;
}

std::string trim_ascii(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

void activate_control_menu_item(int index) {
    switch (index) {
        case 0:
            g_enabled = !g_enabled;
            g_control_menu_status =
                g_enabled ? "Drone View ON" : "Drone View OFF";
            break;
        case 1:
            g_fov_index = 0;
            g_control_menu_status = "FOV set to 40";
            break;
        case 2:
            g_fov_index = 1;
            g_control_menu_status = "FOV set to 60";
            break;
        case 3:
            g_fov_index = 2;
            g_control_menu_status = "FOV set to 80";
            break;
        case 4:
            g_control_menu_status =
                control_menu_fov_selected()
                    ? "Current FOV: " +
                          std::to_string(control_menu_fov_value())
                    : "Current FOV: game default (no preset selected)";
            break;
        case 5:
            g_control_menu_active.store(false, std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

enum class MenuIndicator { None, Checkbox, Toggle };

void draw_control_menu_row(int index, const char* text, bool selected,
                           MenuIndicator indicator, bool on) {
    const float row_height = ImGui::GetTextLineHeight();
    const float font_size = ImGui::GetFontSize();
    const ImVec4 accent(0.62f, 0.48f, 1.0f, 1.0f);
    const ImVec4 selected_text(240.0f / 255.0f, 248.0f / 255.0f,
                               255.0f / 255.0f, 1.0f);

    // Reserve a marker column on the left; the checkbox/toggle is drawn at the
    // right edge of the row.
    const float marker_width = font_size * 0.9f;
    const float space_width = std::max(1.0f, ImGui::CalcTextSize(" ").x);
    const int spaces = std::max(
        1, static_cast<int>(std::lround(marker_width / space_width)));
    const std::string prefix(static_cast<std::size_t>(spaces), ' ');

    char label[192];
    std::snprintf(label, sizeof(label), "%s%s###drone_control_%d",
                  prefix.c_str(), text, index);

    const ImVec2 text_pos = ImGui::GetCursorScreenPos();
    const float row_right =
        text_pos.x + ImGui::GetContentRegionAvail().x -
        ImGui::GetStyle().FramePadding.x;
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Text, selected_text);
    }
    const bool clicked =
        ImGui::Selectable(label, selected, ImGuiSelectableFlags_None,
                          ImVec2(0.0f, row_height));
    if (selected) {
        ImGui::PopStyleColor();
    }

    const float center_y = text_pos.y + row_height * 0.5f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 accent_col = ImGui::GetColorU32(accent);
    const ImU32 indicator_on_col = ImGui::GetColorU32(ImGuiCol_NavHighlight);
    const ImU32 muted_col =
        ImGui::GetColorU32(ImVec4(0.55f, 0.55f, 0.55f, 1.0f));

    if (selected) {
        const float marker_h = font_size * 0.55f;
        const float marker_w = font_size * 0.35f;
        draw->AddTriangleFilled(
            ImVec2(text_pos.x, center_y - marker_h * 0.5f),
            ImVec2(text_pos.x, center_y + marker_h * 0.5f),
            ImVec2(text_pos.x + marker_w, center_y), accent_col);
    }

    if (indicator == MenuIndicator::Checkbox) {
        const float box = std::round(font_size * 0.8f);
        const ImVec2 box_min(row_right - box,
                             std::round(center_y - box * 0.5f));
        const ImVec2 box_max(box_min.x + box, box_min.y + box);
        const float border = std::max(1.5f, font_size * 0.09f);
        draw->AddRect(box_min, box_max, on ? indicator_on_col : muted_col, 2.0f,
                      0, border);
        if (on) {
            const float thickness = std::max(2.0f, font_size * 0.13f);
            draw->AddLine(
                ImVec2(box_min.x + box * 0.22f, box_min.y + box * 0.52f),
                ImVec2(box_min.x + box * 0.44f, box_min.y + box * 0.76f),
                indicator_on_col, thickness);
            draw->AddLine(
                ImVec2(box_min.x + box * 0.44f, box_min.y + box * 0.76f),
                ImVec2(box_min.x + box * 0.80f, box_min.y + box * 0.24f),
                indicator_on_col, thickness);
        }
    } else if (indicator == MenuIndicator::Toggle) {
        const float track_h = std::round(font_size * 0.9f);
        const float track_w = std::round(track_h * 2.0f);
        const ImVec2 track_min(row_right - track_w,
                               std::round(center_y - track_h * 0.5f));
        const ImVec2 track_max(track_min.x + track_w, track_min.y + track_h);
        draw->AddRectFilled(track_min, track_max,
                            on ? indicator_on_col
                               : ImGui::GetColorU32(ImGuiCol_FrameBg),
                            track_h * 0.5f);
        const float knob_radius = track_h * 0.5f - 2.0f;
        const float knob_x = on ? track_max.x - track_h * 0.5f
                                : track_min.x + track_h * 0.5f;
        draw->AddCircleFilled(ImVec2(knob_x, center_y), knob_radius,
                              ImGui::GetColorU32(ImGuiCol_Text));
    }

    if (clicked) {
        g_control_menu_index = index;
        activate_control_menu_item(index);
    }
}

void process_control_menu_events() {
    std::deque<MenuEvent> events;
    {
        std::lock_guard<std::mutex> lock(g_menu_mutex);
        events.swap(g_menu_events);
    }
    constexpr int kItemCount = 6;
    for (const MenuEvent& event : events) {
        if (!g_control_menu_active.load(std::memory_order_relaxed)) {
            break;
        }
        if (event.type == MenuEventType::Char) {
            const unsigned int c = event.codepoint;
            if (c == 'k' || c == 'K') {
                g_control_menu_index =
                    (g_control_menu_index + kItemCount - 1) % kItemCount;
            } else if (c == 'j' || c == 'J') {
                g_control_menu_index = (g_control_menu_index + 1) % kItemCount;
            } else if (c == 'q' || c == 'Q') {
                minimize_overlay();
            } else if (c == ' ' || c == '\r' || c == '\n') {
                activate_control_menu_item(g_control_menu_index);
            } else if (c >= '0' && c <= '5') {
                g_control_menu_index =
                    c == '0' ? 5 : static_cast<int>(c - '1');
            }
            continue;
        }
        switch (event.key) {
            case ImGuiKey_UpArrow:
                g_control_menu_index =
                    (g_control_menu_index + kItemCount - 1) % kItemCount;
                break;
            case ImGuiKey_DownArrow:
                g_control_menu_index = (g_control_menu_index + 1) % kItemCount;
                break;
            case ImGuiKey_Home:
                g_control_menu_index = 0;
                break;
            case ImGuiKey_End:
                g_control_menu_index = kItemCount - 1;
                break;
            case ImGuiKey_Enter:
            case ImGuiKey_KeyPadEnter:
            case ImGuiKey_Space:
                activate_control_menu_item(g_control_menu_index);
                break;
            case ImGuiKey_Escape:
                g_control_menu_active.store(false, std::memory_order_relaxed);
                break;
            default:
                break;
        }
    }
}

bool handle_control_menu_line(const std::string& line) {
    if (!g_control_menu_active.load(std::memory_order_relaxed)) {
        return false;
    }
    const std::string command = trim_ascii(line);
    if (command.empty()) {
        return false;
    }
    if (command == "0") {
        g_control_menu_index = 5;
        activate_control_menu_item(5);
        return true;
    }
    if (command.size() == 1 && command[0] >= '1' && command[0] <= '5') {
        const int item = static_cast<int>(command[0] - '1');
        g_control_menu_index = item;
        activate_control_menu_item(item);
        return true;
    }
    if (command == "q" || command == "Q") {
        minimize_overlay();
        return true;
    }
    return false;
}

void draw_control_menu() {
    const ImVec4 accent(0.62f, 0.48f, 1.0f, 1.0f);
    const ImVec4 dim(0.62f, 0.62f, 0.62f, 1.0f);

    ImGui::TextColored(accent, "Drone View Control");
    ImGui::TextDisabled("Numbered menu - keyboard or tap");
    ImGui::Separator();

    static const char* const kLabels[] = {
        "1. Drone View",
        "2. FOV: Medium (40)",
        "3. FOV: Wide (60)",
        "4. FOV: Birdeye (80)",
        "5. Show Backend Status",
        "0. Exit Menu",
    };

    for (int i = 0; i < 6; ++i) {
        const bool selected = i == g_control_menu_index;
        char row_text[160];
        if (i == 0) {
            std::snprintf(row_text, sizeof(row_text), "%s: %s", kLabels[i],
                          g_enabled ? "ON" : "OFF");
        } else {
            std::snprintf(row_text, sizeof(row_text), "%s", kLabels[i]);
        }
        if (i == 0) {
            draw_control_menu_row(i, row_text, selected, MenuIndicator::Toggle,
                                  g_enabled);
        } else if (i >= 1 && i <= kFovPresetCount) {
            draw_control_menu_row(i, row_text, selected,
                                  MenuIndicator::Checkbox,
                                  g_fov_index == i - 1);
        } else {
            draw_control_menu_row(i, row_text, selected, MenuIndicator::None,
                                  false);
        }
    }

    ImGui::Separator();
    if (control_menu_fov_selected()) {
        ImGui::Text("Status: Drone View %s | FOV %d",
                    g_enabled ? "ON" : "OFF", control_menu_fov_value());
    } else {
        ImGui::Text("Status: Drone View %s | FOV game default",
                    g_enabled ? "ON" : "OFF");
    }
    if (!g_control_menu_status.empty()) {
        ImGui::TextWrapped("%s", g_control_menu_status.c_str());
    }
    ImGui::Separator();
    ImGui::TextColored(dim, "enter select  1-5 jump  0 exit  q minimize");
}

void draw_menu() {
    const ImGuiStyle& style = ImGui::GetStyle();
    // Never hide the menu behind an icon that could not be created.
    if (g_collapsed && g_icon_texture == 0) {
        g_collapsed = false;
    }

    const bool console_open = !g_collapsed;
    g_console_view_active.store(console_open, std::memory_order_relaxed);

    if (g_collapsed) {
        if (dv::soft_keyboard::visible()) {
            dv::soft_keyboard::hide(nullptr);
        }
        if (g_console) {
            g_console->DeactivateInput();
        }
        {
            std::lock_guard<std::mutex> lock(g_menu_mutex);
            g_menu_events.clear();
        }
        if (!g_icon_position_valid) {
            g_icon_x = 40.0f;
            g_icon_y = 40.0f;
        }
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        g_icon_x = std::max(0.0f, std::min(g_icon_x, display.x - kIconDisplaySize));
        g_icon_y = std::max(0.0f, std::min(g_icon_y, display.y - kIconDisplaySize));

        ImGui::SetNextWindowPos(ImVec2(g_icon_x, g_icon_y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(kIconDisplaySize, kIconDisplaySize),
                                 ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const ImGuiWindowFlags icon_flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##droneview_icon", nullptr, icon_flags)) {
            const ImVec2 icon_min = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##droneview_icon_button",
                                   ImVec2(kIconDisplaySize, kIconDisplaySize));
            const ImVec2 icon_max(icon_min.x + kIconDisplaySize,
                                  icon_min.y + kIconDisplaySize);
            ImGui::GetWindowDrawList()->AddImage(
                reinterpret_cast<ImTextureID>(static_cast<intptr_t>(g_icon_texture)),
                icon_min, icon_max);
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }

            if (ImGui::IsItemActivated()) {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const ImVec2 window = ImGui::GetWindowPos();
                g_icon_drag_start_x = mouse.x;
                g_icon_drag_start_y = mouse.y;
                g_icon_window_start_x = window.x;
                g_icon_window_start_y = window.y;
                g_icon_drag_moved = false;
            }
            if (ImGui::IsItemActive()) {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float dx = mouse.x - g_icon_drag_start_x;
                const float dy = mouse.y - g_icon_drag_start_y;
                if (!g_icon_drag_moved &&
                    (std::fabs(dx) > kIconDragThreshold ||
                     std::fabs(dy) > kIconDragThreshold)) {
                    g_icon_drag_moved = true;
                }
                if (g_icon_drag_moved) {
                    g_icon_x = g_icon_window_start_x + dx;
                    g_icon_y = g_icon_window_start_y + dy;
                    const ImVec2 drag_display = ImGui::GetIO().DisplaySize;
                    g_icon_x = std::max(
                        0.0f, std::min(g_icon_x, drag_display.x - kIconDisplaySize));
                    g_icon_y = std::max(
                        0.0f, std::min(g_icon_y, drag_display.y - kIconDisplaySize));
                    ImGui::SetWindowPos(ImVec2(g_icon_x, g_icon_y));
                }
            }
            if (ImGui::IsItemDeactivated()) {
                if (!g_icon_drag_moved) {
                    g_collapsed = false;
                    g_restore_position = true;
                    g_restore_x = g_icon_x;
                    g_restore_y = g_icon_y;
                }
                g_icon_drag_moved = false;
            }
            record_menu_rect();
            clear_console_input_rect();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    const float max_height = std::max(ImGui::GetIO().DisplaySize.y, 1.0f);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(kConsoleWidth, 0.0f), ImVec2(kConsoleWidth, max_height));
    if (g_restore_position) {
        ImGui::SetNextWindowPos(ImVec2(g_restore_x, g_restore_y),
                                ImGuiCond_Always);
        g_restore_position = false;
    } else {
        ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    }
    // Collapse is handled by the custom title bar, never by ImGui's built-in
    // title-bar state (which is disabled by NoTitleBar).
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_AlwaysAutoResize;

    const char* window_title = "Drone Console";

    if (!ImGui::Begin(window_title, nullptr, flags)) {
        ImGui::End();
        return;
    }

    record_menu_rect();

    // Custom title bar: ▼ minimizes. The rest of the title row is the drag
    // handle because NoTitleBar disables the default one.
    const bool toggle = ImGui::ArrowButton("##droneview_toggle", ImGuiDir_Down);
    ImGui::SameLine();

    const ImVec2 title_pos = ImGui::GetCursorScreenPos();
    const float title_width = ImGui::GetContentRegionAvail().x;
    if (title_width > 0.0f) {
        ImGui::InvisibleButton("##droneview_drag",
                               ImVec2(title_width, ImGui::GetFrameHeight()));
        if (ImGui::IsItemActive() &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 pos = ImGui::GetWindowPos();
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(pos.x + delta.x, pos.y + delta.y));
        }
    }
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(title_pos.x + style.FramePadding.x,
               title_pos.y + style.FramePadding.y),
        ImGui::GetColorU32(ImGuiCol_Text), window_title);

    if (toggle) {
        minimize_overlay();
    }

    if (!g_collapsed) {
        ImGui::Separator();

        if (g_control_menu_active.load(std::memory_order_relaxed)) {
            clear_console_input_rect();
            if (g_console) {
                const bool focus = consume_console_focus_request();
                const bool blur = consume_console_blur_request();
                if (focus) {
                    g_console->RequestInputFocus();
                } else if (blur) {
                    g_console->DeactivateInput();
                }
                g_console->DrawInputBar();
                record_console_input_rect();
            }
            process_control_menu_events();
            ImGui::Separator();
            draw_control_menu();
        } else if (g_console) {
            const bool focus = consume_console_focus_request();
            const bool blur = consume_console_blur_request();
            if (focus) {
                g_console->RequestInputFocus();
            } else if (blur) {
                g_console->DeactivateInput();
            }
            g_console->DrawInputBar();
            ImGui::Separator();
            g_console->DrawLogWindow(false, kConsoleLogHeight);
            record_console_input_rect();
        }
    }
    ImGui::End();
}

bool rebuild_font_at(float pixel_size) {
    ImGuiIO& io = ImGui::GetIO();
    const bool had_texture = io.Fonts->TexID != 0;

    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();

    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    font_config.OversampleH = 2;
    font_config.OversampleV = 2;
    font_config.PixelSnapH = true;
    ImFont* font = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(kDroidSansTtf),
        static_cast<int>(kDroidSansTtf_len), pixel_size, &font_config,
        io.Fonts->GetGlyphRangesDefault());
    if (font != nullptr) {
        io.FontDefault = font;
        g_font_scale = 1.0f;
    } else {
        io.Fonts->AddFontDefault();
        io.FontDefault = nullptr;
        g_font_scale = g_ui_scale;
    }
    io.FontGlobalScale = g_font_scale;

    if (!io.Fonts->Build()) {
        io.Fonts->Clear();
        io.Fonts->AddFontDefault();
        io.FontDefault = nullptr;
        g_font_scale = g_ui_scale;
        io.FontGlobalScale = g_font_scale;
        if (!io.Fonts->Build()) {
            return false;
        }
    }
    // When device objects already exist the font texture must be recreated
    // now; before the first frame the backend builds it from the new atlas.
    if (had_texture) {
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }
    return true;
}

void configure_ui_for_display(int width, int height) {
    const float min_dim = static_cast<float>(std::min(width, height));
    float pixel_size = std::round(min_dim * kFontSizeRatio);
    pixel_size = std::max(kFontPixelMin, std::min(pixel_size, kFontPixelMax));
    g_ui_scale = pixel_size / kUiScaleBase;
    g_display_configured = true;
    if (rebuild_font_at(pixel_size)) {
        app::platform::log_info(
            "droneview",
            "UI font: DroidSans @ %.0fpx, scale %.2f (display %dx%d)",
            pixel_size, g_ui_scale, width, height);
    } else {
        app::platform::log_error(
            "droneview", "UI font rebuild failed for display %dx%d", width,
            height);
    }
}

}  // namespace

bool initialize(const char* ini_filename, const char* log_filename) {
    if (g_initialized.load(std::memory_order_acquire)) {
        return true;
    }
    if (ini_filename != nullptr && *ini_filename != '\0') {
        g_ini_filename = ini_filename;
    }
    if (log_filename != nullptr && *log_filename != '\0') {
        g_log_filename = log_filename;
    }

    // Start from the fixed fallback; the first real surface size replaces it
    // with a display-derived, crisply rasterized font.
    g_ui_scale = 1.4f;
    g_font_scale = 1.0f;
    g_display_configured = false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = g_ini_filename.c_str();
    io.LogFilename = g_log_filename.c_str();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;

    // Map the keys used by InputText to our own KeysDown indices. No platform
    // keyboard backend exists for this overlay, so the index values are ours.
    io.KeyMap[ImGuiKey_Tab] = 1;
    io.KeyMap[ImGuiKey_LeftArrow] = 2;
    io.KeyMap[ImGuiKey_RightArrow] = 3;
    io.KeyMap[ImGuiKey_UpArrow] = 4;
    io.KeyMap[ImGuiKey_DownArrow] = 5;
    io.KeyMap[ImGuiKey_PageUp] = 6;
    io.KeyMap[ImGuiKey_PageDown] = 7;
    io.KeyMap[ImGuiKey_Home] = 8;
    io.KeyMap[ImGuiKey_End] = 9;
    io.KeyMap[ImGuiKey_Insert] = 10;
    io.KeyMap[ImGuiKey_Delete] = 11;
    io.KeyMap[ImGuiKey_Backspace] = 12;
    io.KeyMap[ImGuiKey_Space] = 13;
    io.KeyMap[ImGuiKey_Enter] = 14;
    io.KeyMap[ImGuiKey_Escape] = 15;
    io.KeyMap[ImGuiKey_KeyPadEnter] = 16;
    io.KeyMap[ImGuiKey_A] = 17;
    io.KeyMap[ImGuiKey_C] = 18;
    io.KeyMap[ImGuiKey_V] = 19;
    io.KeyMap[ImGuiKey_X] = 20;
    io.KeyMap[ImGuiKey_Z] = 21;
    io.KeyMap[ImGuiKey_Y] = 22;

    ImGui::StyleColorsDark();
    g_base_style = ImGui::GetStyle();
    // Near-opaque, slightly darker backdrop so log text stays legible over
    // bright game content.
    g_base_style.Colors[ImGuiCol_WindowBg] =
        ImVec4(0.05f, 0.05f, 0.06f, 0.98f);
    ImGui::GetStyle().ScaleAllSizes(g_ui_scale);

    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    font_config.OversampleH = 2;
    font_config.OversampleV = 2;
    font_config.PixelSnapH = true;
    ImFont* font = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(kDroidSansTtf),
        static_cast<int>(kDroidSansTtf_len), kFontPixelSize, &font_config,
        io.Fonts->GetGlyphRangesDefault());
    if (font != nullptr) {
        io.FontDefault = font;
        g_font_scale = 1.0f;
        app::platform::log_info("droneview",
                                "UI font: embedded DroidSans @ %.0fpx",
                                kFontPixelSize);
    } else {
        io.Fonts->AddFontDefault();
        g_font_scale = g_ui_scale;
        app::platform::log_error(
            "droneview", "UI font: embedded DroidSans failed; using default");
    }
    io.FontGlobalScale = g_font_scale;

    if (!io.Fonts->Build()) {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        ImGui::DestroyContext();
        return false;
    }

    create_icon_texture();

    g_console = std::make_unique<ImGuiConsole>("Drone Console", 256);
    g_console->SetInputHint("ENTER MENU NUMBER HERE");
    g_console->SetLineHandler(handle_control_menu_line);
    g_console->System().RegisterCommand(
        "status", "Show the drone view backend status", []() {
            g_console->System().Log(csys::INFO)
                << dv::backend::status() << csys::endl;
        });
    g_console->System().RegisterCommand(
        "enable", "Enable or disable drone view (0 or 1)",
        [](int value) { g_enabled = value != 0; },
        csys::Arg<int>("0_or_1"));
    g_console->System().RegisterCommand(
        "fov", "Set the FOV preset (40, 60, or 80)",
        [](int fov) {
            if (fov == 40) {
                g_fov_index = 0;
            } else if (fov == 60) {
                g_fov_index = 1;
            } else if (fov == 80) {
                g_fov_index = 2;
            } else {
                g_console->System().Log(csys::ERROR)
                    << "fov: expected 40, 60, or 80" << csys::endl;
            }
        },
        csys::Arg<int>("fov"));
    g_console->System().RegisterCommand(
        "drone", "Open the numbered Drone View control menu", []() {
            {
                std::lock_guard<std::mutex> lock(g_menu_mutex);
                g_menu_events.clear();
            }
            g_control_menu_index = 0;
            g_control_menu_status.clear();
            if (g_console) {
                g_console->DeactivateInput();
            }
            g_control_menu_active.store(true, std::memory_order_relaxed);
        });
    g_console->System().Log(csys::INFO)
        << "Drone Console ready. The control menu is open; type 'help' for "
           "commands."
        << csys::endl;
    // Start with the drone control menu as the primary interface.
    g_console->System().RunCommand("drone");

    g_initialized.store(true, std::memory_order_release);
    return true;
}

bool initialized() {
    return g_initialized.load(std::memory_order_acquire);
}

void set_display_size(int width, int height) {
    if (!g_initialized.load(std::memory_order_acquire)) {
        return;
    }
    if (width > 0 && height > 0) {
        ImGui::GetIO().DisplaySize =
            ImVec2(static_cast<float>(width), static_cast<float>(height));
        if (!g_display_configured) {
            configure_ui_for_display(width, height);
        }
    }
}

void frame() {
    if (!g_initialized.load(std::memory_order_acquire)) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplOpenGL3_NewFrame();
    io.FontGlobalScale = g_font_scale;
    {
        ImGuiStyle scaled = g_base_style;
        scaled.ScaleAllSizes(g_ui_scale);
        ImGui::GetStyle() = scaled;
    }
    dv::soft_keyboard::poll();
    apply_touch_events();
    apply_key_events();
    ImGui::NewFrame();
    draw_menu();
    ImGui::Render();
    g_text_input_wanted.store(io.WantTextInput, std::memory_order_relaxed);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Apply the UI state after rendering so the menu stays responsive even if
    // the game is still resolving IL2CPP bindings. A non-positive FOV means no
    // preset is selected, so the game keeps its original field of view.
    dv::backend::apply(g_enabled, control_menu_fov_override());
}

void shutdown() {
    if (!g_initialized.load(std::memory_order_acquire)) {
        return;
    }
    dv::soft_keyboard::destroy();
    destroy_icon_texture();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    g_console.reset();
    g_initialized.store(false, std::memory_order_release);
    g_text_input_wanted.store(false, std::memory_order_relaxed);
    g_console_view_active.store(false, std::memory_order_relaxed);
    g_control_menu_active.store(false, std::memory_order_relaxed);
    g_control_menu_index = 0;
    g_control_menu_status.clear();
    {
        std::lock_guard<std::mutex> lock(g_menu_mutex);
        g_menu_events.clear();
    }
    g_ctrl.store(false, std::memory_order_relaxed);
    g_shift.store(false, std::memory_order_relaxed);
    g_alt.store(false, std::memory_order_relaxed);
    g_touch_active = false;
    {
        std::lock_guard<std::mutex> lock(g_touch_mutex);
        g_touch_events.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        g_input_events.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_rect_mutex);
        g_menu_rect_valid = false;
        g_console_input_valid = false;
    }
    g_console_focus_request.store(false, std::memory_order_relaxed);
    g_console_blur_request.store(false, std::memory_order_relaxed);
    g_display_configured = false;
}

void reset_for_context_loss() {
    if (!g_initialized.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    dv::soft_keyboard::destroy();
    // The old EGL context is gone, so GL deletion is invalid here. The
    // backend's GL objects leak with that context; this is safer than issuing
    // GL calls in the new context.
    g_icon_texture = 0;
    ImGui::DestroyContext();
    g_console.reset();
    g_touch_active = false;
    g_text_input_wanted.store(false, std::memory_order_relaxed);
    g_console_view_active.store(false, std::memory_order_relaxed);
    g_control_menu_active.store(false, std::memory_order_relaxed);
    g_control_menu_index = 0;
    g_control_menu_status.clear();
    {
        std::lock_guard<std::mutex> lock(g_menu_mutex);
        g_menu_events.clear();
    }
    g_ctrl.store(false, std::memory_order_relaxed);
    g_shift.store(false, std::memory_order_relaxed);
    g_alt.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_touch_mutex);
        g_touch_events.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        g_input_events.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_rect_mutex);
        g_menu_rect_valid = false;
        g_console_input_valid = false;
    }
    g_console_focus_request.store(false, std::memory_order_relaxed);
    g_console_blur_request.store(false, std::memory_order_relaxed);
    g_display_configured = false;
}

bool touch_captures(float x, float y) {
    std::lock_guard<std::mutex> lock(g_rect_mutex);
    return g_menu_rect_valid && x >= g_menu_x && y >= g_menu_y &&
           x <= g_menu_x + g_menu_w && y <= g_menu_y + g_menu_h;
}

void queue_touch(float x, float y, int action) {
    std::lock_guard<std::mutex> lock(g_touch_mutex);
    if (action == 2 && !g_touch_events.empty() &&
        g_touch_events.back().action == 2) {
        g_touch_events.back().x = x;
        g_touch_events.back().y = y;
        return;
    }
    if (g_touch_events.size() < 32) {
        g_touch_events.push_back(TouchEvent{x, y, action});
    }
}

bool console_input_hit(float x, float y) {
    std::lock_guard<std::mutex> lock(g_rect_mutex);
    return g_console_input_valid && x >= g_console_input_x &&
           y >= g_console_input_y &&
           x <= g_console_input_x + g_console_input_w &&
           y <= g_console_input_y + g_console_input_h;
}

bool console_view_active() {
    return g_console_view_active.load(std::memory_order_relaxed);
}

void request_console_input_focus() {
    g_console_focus_request.store(true, std::memory_order_release);
}

void request_console_input_blur() {
    g_console_blur_request.store(true, std::memory_order_release);
}

bool text_input_wanted() {
    return g_text_input_wanted.load(std::memory_order_relaxed);
}

bool menu_active() {
    return g_control_menu_active.load(std::memory_order_relaxed);
}

void queue_menu_key(int imgui_key) {
    if (imgui_key < 0 || imgui_key >= ImGuiKey_COUNT) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_menu_mutex);
    if (g_menu_events.size() < 64) {
        g_menu_events.push_back(MenuEvent{MenuEventType::Key, imgui_key, 0});
    }
}

void queue_menu_char(unsigned int codepoint) {
    if (codepoint == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_menu_mutex);
    if (g_menu_events.size() < 128) {
        g_menu_events.push_back(
            MenuEvent{MenuEventType::Char, 0, codepoint});
    }
}

void queue_character(unsigned int codepoint) {
    if (codepoint == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_input_mutex);
    if (g_input_events.size() < 512) {
        g_input_events.push_back(
            InputEvent{InputEventType::Character, codepoint, 0, false});
    }
}

void queue_key(int imgui_key, bool down) {
    if (imgui_key < 0 || imgui_key >= ImGuiKey_COUNT) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_input_mutex);
    if (g_input_events.size() < 512) {
        g_input_events.push_back(
            InputEvent{InputEventType::Key, 0, imgui_key, down});
    }
}

void set_modifiers(bool ctrl, bool shift, bool alt) {
    g_ctrl.store(ctrl, std::memory_order_relaxed);
    g_shift.store(shift, std::memory_order_relaxed);
    g_alt.store(alt, std::memory_order_relaxed);
}

}  // namespace dv::frontend
