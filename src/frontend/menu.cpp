#include "droneview/frontend.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include <GLES3/gl3.h>

#include "app/platform/platform.h"
#include "droneview/backend.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

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
int g_fov_index = 0;
constexpr float kFovPresets[] = {40.0f, 60.0f, 80.0f};
float g_ui_scale = 1.4f;
float g_font_scale = 1.0f;
constexpr float kFontPixelSize = 18.0f;
constexpr float kMenuWidth = 390.0f;
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

void draw_menu() {
    const ImGuiStyle& style = ImGui::GetStyle();
    // The menu is only interactive once the backend verified MLBB v2.2.23 or v2.2.16.
    const bool ui_enabled = dv::backend::initial_checks_passed();

    // Keep the status line visible while the version is unsupported.
    if (!ui_enabled && g_collapsed) {
        g_collapsed = false;
    }
    // Never hide the menu behind an icon that could not be created.
    if (g_collapsed && g_icon_texture == 0) {
        g_collapsed = false;
    }

    if (g_collapsed) {
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
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    // Fixed width, height follows the content. This keeps the wrapped status
    // line at the bottom visible instead of clipping it at a fixed height.
    const float max_height = std::max(ImGui::GetIO().DisplaySize.y, 1.0f);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(kMenuWidth, 0.0f), ImVec2(kMenuWidth, max_height));
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
    if (g_collapsed) {
        flags |= ImGuiWindowFlags_NoResize;
    }

    if (!ImGui::Begin("Drone View", nullptr, flags)) {
        ImGui::End();
        return;
    }

    record_menu_rect();

    // Custom title bar: ▼ minimizes. The rest of the title row is the drag
    // handle because NoTitleBar disables the default one.
    ImGui::BeginDisabled(!ui_enabled);
    const bool toggle = ImGui::ArrowButton("##droneview_toggle", ImGuiDir_Down);
    ImGui::EndDisabled();
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

    if (toggle && g_icon_texture != 0) {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        g_icon_x = std::max(0.0f, std::min(pos.x, display.x - kIconDisplaySize));
        g_icon_y = std::max(0.0f, std::min(pos.y, display.y - kIconDisplaySize));
        g_icon_position_valid = true;
        g_collapsed = true;
    }

    if (!g_collapsed) {
        ImGui::Separator();
        ImGui::BeginDisabled(!ui_enabled);
        ImGui::Checkbox("Enable", &g_enabled);
        ImGui::TextUnformatted("FOV");
        ImGui::RadioButton("Medium (FOV 40)", &g_fov_index, 0);
        ImGui::RadioButton("Wide (FOV 60)", &g_fov_index, 1);
        ImGui::RadioButton("Birdeye (FOV 80)", &g_fov_index, 2);
        ImGui::EndDisabled();
        ImGui::Separator();
        const std::string state = dv::backend::status();
        ImGui::TextWrapped("%s", state.c_str());
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

    create_icon_texture();

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
    dv::backend::apply(g_enabled, kFovPresets[g_fov_index]);
}

void shutdown() {
    if (!g_initialized.load(std::memory_order_acquire)) {
        return;
    }
    destroy_icon_texture();
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

void reset_for_context_loss() {
    if (!g_initialized.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    // The old EGL context is gone, so GL deletion is invalid here. The
    // backend's GL objects leak with that context; this is safer than issuing
    // GL calls in the new context.
    g_icon_texture = 0;
    ImGui::DestroyContext();
    g_touch_active = false;
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
