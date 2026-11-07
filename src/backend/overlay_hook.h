#pragma once

namespace dv::overlay {

// Hooks libEGL's eglSwapBuffers. Returns false when libEGL is not loaded yet;
// the caller retries.
bool install();
bool installed();

// Marks the overlay UI as no longer renderable. The hook itself stays
// installed, but subsequent swaps skip the ImGui frame.
void shutdown();

}  // namespace dv::overlay
