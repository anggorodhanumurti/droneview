#pragma once

namespace dv::input {

// Hooks UnityPlayer.nativeInjectEvent once the Unity JNI table is valid.
// Returns false (and can be retried) while libunity is not ready.
bool install();
bool installed();

}  // namespace dv::input
