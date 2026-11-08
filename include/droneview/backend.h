#pragma once

#include <string>

namespace dv::backend {

// Starts the resolution worker. Safe to call once; never blocks.
void initialize();

// Stops the worker and releases bindings.
void shutdown();

// True when liblogic/libcsharp were found, IL2CPP was bound, and the camera
// field/methods were resolved.
bool ready();

// Human readable resolution state (safe to call from the render thread).
std::string status();

// Applies or restores the drone FOV. Called from the render thread every
// frame; cheap once bound. Safe to call before ready().
void apply(bool enabled, float fov);

}  // namespace dv::backend
