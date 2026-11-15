#pragma once

#if APP_PLATFORM_ANDROID
#include <jni.h>
#endif

namespace dv::soft_keyboard {

#if APP_PLATFORM_ANDROID

// Creates the invisible EditText without focusing it. Call on the UI thread
// from a touch event so the view is attached and laid out before show().
void prepare(JNIEnv* env, jobject host_view);

// Creates (once), focuses and shows an invisible EditText used to receive IME
// input. Must be called on the UI thread with the game's view.
void show(JNIEnv* env, jobject host_view);

// Hides the IME and clears the EditText scratch buffer. Passing a null env
// attaches the current thread; focus restoration is skipped in that case.
void hide(JNIEnv* env = nullptr);

// Hides the IME and releases the Java global references. Used when the UI or
// EGL context is torn down.
void destroy();

// Reads new IME text and feeds it to the frontend as characters/keys.
// Called from the render thread each frame.
void poll();

bool visible();

#else

inline void poll() {}
inline bool visible() { return false; }
inline void destroy() {}

#endif

}  // namespace dv::soft_keyboard
