#pragma once

namespace dv::overlay {

// Hooks libEGL's eglSwapBuffers. Returns false when libEGL is not loaded yet;
// the caller retries.
bool install();
bool installed();

}  // namespace dv::overlay
