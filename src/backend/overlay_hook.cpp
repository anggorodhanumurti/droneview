#include "overlay_hook.h"

#include <atomic>
#include <mutex>

#include "app/elf/elf_module.h"
#include "app/hooks/hook_engine.h"
#include "app/platform/platform.h"
#include "droneview/frontend.h"
#include "runtime.h"

#include <EGL/egl.h>

namespace dv::overlay {
namespace {

std::mutex g_install_mutex;
bool g_installed = false;
bool g_failure_logged = false;

using EglSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
EglSwapBuffersFn g_original_swap = nullptr;
std::atomic<bool> g_ui_ready{false};
std::atomic<bool> g_ui_initializing{false};
std::atomic<int> g_ui_retry_frames{0};

void ensure_ui() {
    if (g_ui_ready.load(std::memory_order_acquire)) {
        return;
    }
    // A failed init is retried later; the first swap can happen before the GL
    // context is fully usable.
    if (g_ui_retry_frames.load(std::memory_order_relaxed) > 0) {
        g_ui_retry_frames.fetch_sub(1, std::memory_order_relaxed);
        return;
    }
    bool expected = false;
    if (!g_ui_initializing.compare_exchange_strong(expected, true,
                                                   std::memory_order_acq_rel)) {
        return;
    }
    if (dv::runtime::ensure_ui()) {
        g_ui_ready.store(true, std::memory_order_release);
        app::platform::log_info("droneview", "overlay UI ready");
    } else {
        g_ui_retry_frames.store(120, std::memory_order_relaxed);
        app::platform::log_error("droneview", "UI init failed; retrying");
    }
    g_ui_initializing.store(false, std::memory_order_release);
}

EGLBoolean overlay_swap(EGLDisplay display, EGLSurface surface) {
    ensure_ui();
    if (g_ui_ready.load(std::memory_order_acquire)) {
        EGLint width = 0;
        EGLint height = 0;
        if (eglQuerySurface(display, surface, EGL_WIDTH, &width) == EGL_TRUE &&
            eglQuerySurface(display, surface, EGL_HEIGHT, &height) == EGL_TRUE) {
            dv::frontend::set_display_size(width, height);
        }
        dv::runtime::frame();
    }
    if (g_original_swap == nullptr) {
        return EGL_FALSE;
    }
    return g_original_swap(display, surface);
}

}  // namespace

bool install() {
    std::lock_guard<std::mutex> lock(g_install_mutex);
    if (g_installed) {
        return true;
    }
    app::elf::ElfModule egl;
    if (!egl.open("libEGL.so")) {
        if (!g_failure_logged) {
            app::platform::log_error("droneview",
                                     "overlay: libEGL.so not loaded (retrying)");
            g_failure_logged = true;
        }
        return false;
    }
    const app::uptr address = egl.symbol("eglSwapBuffers");
    if (address == 0) {
        if (!g_failure_logged) {
            app::platform::log_error("droneview",
                                     "overlay: eglSwapBuffers not found");
            g_failure_logged = true;
        }
        return false;
    }

    app::hooks::HookEngine engine;
    const app::hooks::HookResult result = engine.install(
        reinterpret_cast<void*>(address), reinterpret_cast<void*>(&overlay_swap));
    if (!result.ok) {
        if (!g_failure_logged) {
            app::platform::log_error("droneview", "overlay hook failed: %s (retrying)",
                                     result.error.c_str());
            g_failure_logged = true;
        }
        return false;
    }
    g_original_swap = reinterpret_cast<EglSwapBuffersFn>(result.original);
    g_installed = true;
    g_failure_logged = false;
    app::platform::log_info("droneview",
                            "overlay hook installed: eglSwapBuffers @ 0x%lx",
                            static_cast<unsigned long>(address));
    return true;
}

bool installed() {
    std::lock_guard<std::mutex> lock(g_install_mutex);
    return g_installed;
}

}  // namespace dv::overlay
