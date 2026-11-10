cmake_minimum_required(VERSION 3.20)

project(droneview-menu
        VERSION 1.0.0
        DESCRIPTION "Single-purpose MLBB Drone View menu (backend + frontend)"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT APP_TARGET_ANDROID)
  message(FATAL_ERROR
      "droneview-menu targets Android arm64-v8a. Use ./build-android.sh")
endif()

option(DRONEVIEW_AUTOSTART "Start from a static initializer" ON)
if(DRONEVIEW_AUTOSTART)
  set(DRONEVIEW_AUTOSTART_VALUE 1)
else()
  set(DRONEVIEW_AUTOSTART_VALUE 0)
endif()

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui")
set(ANDROID_HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/android-headers")
set(GLOSSHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/glosshook")
set(APP_LIBCXX_STATIC
    "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-android/libc++_static.a")
set(APP_LIBCXXABI_STATIC
    "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-android/libc++abi.a")

# ---------------------------------------------------------------------------
# Dear ImGui 1.85 + OpenGL3 backend
# ---------------------------------------------------------------------------
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp")
target_include_directories(imgui PUBLIC "${IMGUI_DIR}")
target_include_directories(imgui SYSTEM PRIVATE "${ANDROID_HEADERS_DIR}")
target_compile_definitions(imgui PUBLIC APP_HAS_OPENGL_BACKEND=1)
target_link_libraries(imgui PUBLIC GLESv3 EGL)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(imgui PRIVATE -Wno-nontrivial-memcall)
endif()

# ---------------------------------------------------------------------------
# GlossHook (ARM64 prebuilt)
# ---------------------------------------------------------------------------
add_library(glosshook STATIC IMPORTED)
set_target_properties(glosshook PROPERTIES
    IMPORTED_LOCATION "${GLOSSHOOK_DIR}/lib/ARM64/libGlossHook.a")

# ---------------------------------------------------------------------------
# Backend (engine/hooks/IL2CPP) + Frontend (ImGui menu)
# ---------------------------------------------------------------------------
add_library(droneview SHARED
    src/app/core/symbol_resolver.cpp
    src/app/core/target_symbols.cpp
    src/app/elf/elf_module.cpp
    src/app/hooks/hook_engine.cpp
    src/app/il2cpp/il2cpp_api.cpp
    src/app/il2cpp/il2cpp_helpers.cpp
    src/app/platform/platform_android.cpp
    src/backend/drone_backend.cpp
    src/backend/initial_check.cpp
    src/backend/overlay_hook.cpp
    src/backend/input_hook.cpp
    src/backend/runtime.cpp
    src/backend/entry.cpp
    src/backend/jni_onload.cpp
    src/frontend/menu.cpp)

target_include_directories(droneview PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${IMGUI_DIR}"
    "${GLOSSHOOK_DIR}/include")
target_include_directories(droneview SYSTEM PRIVATE "${ANDROID_HEADERS_DIR}")
target_compile_definitions(droneview PRIVATE
    APP_PLATFORM_ANDROID=1
    APP_PLATFORM_HOST=0
    APP_WITH_GLOSSHOOK=1
    DRONEVIEW_AUTOSTART=${DRONEVIEW_AUTOSTART_VALUE})
target_compile_options(droneview PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(droneview PRIVATE
    imgui
    glosshook
    "${APP_LIBCXX_STATIC}"
    "${APP_LIBCXXABI_STATIC}"
    log dl m)
if(DEFINED APP_NDK_UNWIND AND EXISTS "${APP_NDK_UNWIND}")
  target_link_libraries(droneview PRIVATE "${APP_NDK_UNWIND}")
endif()

# Self-contained C++ runtime: do not depend on libc++_shared.so.
target_link_options(droneview PRIVATE
    "-nostdlib++"
    "-Wl,--no-undefined"
    "-Wl,--exclude-libs,ALL"
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/droneview_exports.map")

set_target_properties(droneview PROPERTIES OUTPUT_NAME "cstatic")

message(STATUS "[droneview] autostart: ${DRONEVIEW_AUTOSTART_VALUE}")
message(STATUS "[droneview] output: libcstatic.so (arm64-v8a, static libc++)")
