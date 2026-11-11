#pragma once

namespace dv::overlay {

// Hooks libEGL's eglSwapBuffers. Returns false when libEGL is not loaded yet;
// the caller retries.
bool install();
bool installed();

// Disables the overlay UI and prevents it from reinitializing. The hook itself
// stays installed, so later swaps pass through to the original.
void shutdown();

}  // namespace dv::overlay
