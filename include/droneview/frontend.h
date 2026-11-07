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

bool touch_captures(float x, float y);
void queue_touch(float x, float y, int action);

}  // namespace dv::frontend
