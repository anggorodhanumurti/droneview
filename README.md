# MLBB Droneview Mod

<p align="center">
  <img src="droneview_wide.jpg" alt="MLBB Drone View Mod - Wide preset" width="600">
</p>

Single-purpose MLBB **Drone View** menu. No modular loader, no backends to
select, no extra features. One shared library, split into:

- **backend** — ELF/GlossHook, IL2CPP resolution, `eglSwapBuffers` and Unity
  input hooks, Drone View camera control.
- **frontend** — Dear ImGui 1.85 + OpenGL3 menu (Enable checkbox + FOV radio
  presets), touch handling.

## Layout

```text
droneview-menu/
├── include/droneview/backend.h      backend API
├── include/droneview/frontend.h     frontend API
├── src/backend/
│   ├── drone_backend.cpp            liblogic/libcsharp -> IL2CPP -> Camera
│   ├── overlay_hook.cpp             eglSwapBuffers hook
│   ├── input_hook.cpp               UnityPlayer.nativeInjectEvent hook
│   ├── runtime.cpp                  UI lifecycle
│   ├── entry.cpp                    .init_array autostart + dv_start/dv_stop
│   └── jni_onload.cpp               JNI_OnLoad autostart
├── src/frontend/menu.cpp            ImGui menu + touch
├── src/app/                         foundation borrowed from cpp_rebuild
├── dependencies/imgui/              Dear ImGui 1.85
├── dependencies/glosshook/          ARM64 prebuilt
└── dependencies/android-headers/
```

## Behavior

- The menu appears on the first frame after the library is loaded.
- `Enable` off → original camera FOV is restored.
- `Enable` on → `GameMethod.ShowCamera()` (if present) and
  `UnityEngine.Camera.set_fieldOfView(FOV)`, clamped to `20..120`.
- `FOV` radio presets: `Medium` = 40, `Wide` = 60, `Birdeye` = 80.
- Binding resolution runs on a worker thread; the render thread is never
  blocked. `liblogic.so` is the primary source, `libcsharp.so` fills missing
  symbols. The worker never `dlopen`s a library the game has not loaded.

## Build (Android arm64-v8a)

```bash
cd droneview-menu
./build-android.sh
```

Output:

```text
build-android/libcstatic.so
```

- ELF64 AArch64, Android 23, SONAME `libcstatic.so`
- statically linked libc++, **no `libc++_shared.so` dependency**

## How to Install

1. This mod only support MLBB v2.2.16, get it here: [MLBB 2.2.16](https://t0pgamemurah.xyz/downloads/MLBB_2.2.16.12322.zip)
2. Download latest Release APK from the [latest Release](https://github.com/anggorodhanumurti/droneview/releases/latest) page.
3. Extract and Install Apk inside those zip files
4. Clone MLBB and Launch MLBB from inside Meta+ Evolution Space

## Logs

Filter tag `droneview`:

```bash
adb logcat | grep -i droneview
```

Healthy sequence:

```text
droneview: background thread started
droneview: overlay hook installed: eglSwapBuffers @ ...
droneview: IL2CPP from liblogic.so: merged 19/19
droneview: ready: m_Camra=... show=... getFov=... setFov=...
droneview: UI initialized; Drone View menu active
droneview: input hook: nativeInjectEvent resolved from JNI table @ ...
droneview: input hook installed: nativeInjectEvent @ ...
droneview: drone enabled, original FOV ...
```

## Notes

- ARM64 only. A 32-bit Multispace plugin process will ignore
  `lib/arm64-v8a/`.
- `nativeInjectEvent` is resolved from libunity's JNINativeMethod table by
  matching the `nativeInjectEvent` name, the
  `(Landroid/view/InputEvent;)Z` signature, and an executable target inside
  `libunity`. The legacy `libunity + 0x1888f60` slot is only a fallback; if
  neither resolves, input is skipped (menu still renders).
- If the game resets FOV every frame, the frame loop re-applies it.
- The overlay tracks the EGL context used by ImGui. If the game recreates its
  context (for example after background/foreground), the overlay releases the
  old ImGui context and reinitializes on the new one.
- `dv_stop` disables the overlay and prevents reinitialization. The
  `eglSwapBuffers` hook stays installed and forwards swaps to the original.
