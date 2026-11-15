#pragma once

namespace dv::frontend {

// Creates the ImGui context and the OpenGL3 backend. Must run with the game's
// GL context current (first eglSwapBuffers).
bool initialize(const char* ini_filename, const char* log_filename);
bool initialized();

// Updates the ImGui display size from the real EGL surface.
void set_display_size(int width, int height);

// Draws the Drone View menu and applies the current state to the backend.
void frame();

void shutdown();

// Releases the ImGui context when the previous EGL context is gone. Makes no
// GL calls; the next initialize() creates fresh objects in the new context.
void reset_for_context_loss();

bool touch_captures(float x, float y);
void queue_touch(float x, float y, int action);

// Android soft-keyboard and key-event bridge used by the console.
bool console_input_hit(float x, float y);
bool console_view_active();
void request_console_input_focus();
void request_console_input_blur();
bool text_input_wanted();
bool menu_active();
void queue_menu_key(int imgui_key);
void queue_menu_char(unsigned int codepoint);
void queue_character(unsigned int codepoint);
void queue_key(int imgui_key, bool down);
void set_modifiers(bool ctrl, bool shift, bool alt);

}  // namespace dv::frontend
