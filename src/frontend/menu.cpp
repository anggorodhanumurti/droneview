#include "droneview/frontend.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>

#include "app/platform/platform.h"
#include "droneview/backend.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#include "droidsans_ttf.inc"

namespace dv::frontend {
namespace {

struct TouchEvent {
    float x;
    float y;
    int action;
};

std::atomic<bool> g_initialized{false};
bool g_enabled = false;
float g_value = 0.0f;
float g_ui_scale = 1.4f;
float g_font_scale = 1.0f;
constexpr float kFontPixelSize = 18.0f;
bool g_collapsed = false;
bool g_restore_expanded_size = false;
ImVec2 g_expanded_size(390.0f, 240.0f);
std::string g_ini_filename = "droneview.ini";
std::string g_log_filename = "droneview_.txt";
ImGuiStyle g_base_style;

std::mutex g_touch_mutex;
std::deque<TouchEvent> g_touch_events;
bool g_touch_active = false;

std::mutex g_rect_mutex;
float g_menu_x = 0.0f;
float g_menu_y = 0.0f;
float g_menu_w = 0.0f;
float g_menu_h = 0.0f;
bool g_menu_rect_valid = false;

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

void draw_menu() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float titlebar_height =
        ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;

    if (g_collapsed) {
        ImGui::SetNextWindowSize(ImVec2(g_expanded_size.x, titlebar_height),
                                 ImGuiCond_Always);
    } else if (g_restore_expanded_size) {
        ImGui::SetNextWindowSize(g_expanded_size, ImGuiCond_Always);
        g_restore_expanded_size = false;
    } else {
        ImGui::SetNextWindowSize(ImVec2(390.0f, 240.0f), ImGuiCond_FirstUseEver);
    }
    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    // Collapse is handled by the custom title bar, never by ImGui's built-in
    // title-bar state (which is disabled by NoTitleBar).
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
    if (g_collapsed) {
        flags |= ImGuiWindowFlags_NoResize;
    }

    if (!ImGui::Begin("Drone View", nullptr, flags)) {
        ImGui::End();
        return;
    }

    record_menu_rect();

    // Custom title bar: ▼ minimizes, ▶ expands. The rest of the title row is
    // the drag handle because NoTitleBar disables the default one.
    const bool toggle = ImGui::ArrowButton(
        "##droneview_toggle", g_collapsed ? ImGuiDir_Right : ImGuiDir_Down);
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
        ImGui::GetColorU32(ImGuiCol_Text), "Drone View");

    bool expand_clicked = false;
    if (toggle) {
        if (g_collapsed) {
            g_collapsed = false;
            g_restore_expanded_size = true;
            expand_clicked = true;
        } else {
            g_expanded_size = ImGui::GetWindowSize();
            g_collapsed = true;
        }
    }

    if (!g_collapsed && !expand_clicked) {
        ImGui::Separator();
        ImGui::TextDisabled("MLBB Drone View");
        ImGui::Separator();
        ImGui::Checkbox("Enable", &g_enabled);
        ImGui::SetNextItemWidth(220.0f * g_ui_scale);
        ImGui::SliderFloat("Value", &g_value, 0.0f, 20.0f, "%.1f");
        ImGui::Separator();
        const std::string state = dv::backend::status();
        ImGui::TextWrapped("Backend: %s", state.c_str());
        ImGui::TextDisabled("FOV = 40 + Value (clamped 20..120)");
        g_expanded_size = ImGui::GetWindowSize();
    }

    ImGui::End();
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = g_ini_filename.c_str();
    io.LogFilename = g_log_filename.c_str();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;

    ImGui::StyleColorsDark();
    g_base_style = ImGui::GetStyle();
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
    apply_touch_events();
    ImGui::NewFrame();
    draw_menu();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Apply the UI state after rendering so the menu stays responsive even if
    // the game is still resolving IL2CPP bindings.
    dv::backend::apply(g_enabled, g_value);
}

void shutdown() {
    if (!g_initialized.load(std::memory_order_acquire)) {
        return;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    g_initialized.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_touch_mutex);
        g_touch_events.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_rect_mutex);
        g_menu_rect_valid = false;
    }
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

}  // namespace dv::frontend
