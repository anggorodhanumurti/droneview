#pragma once

namespace dv::runtime {

// Called from the eglSwapBuffers replacement. Initializes the frontend once
// the game's GL context is valid; safe to call every frame.
bool ensure_ui();

// Called from the eglSwapBuffers replacement after ensure_ui() succeeded.
void frame();

bool ui_ready();

void shutdown();

}  // namespace dv::runtime
