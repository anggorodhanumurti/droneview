#include "overlay_hook.h"

#include <atomic>
#include <mutex>
#include <thread>

#include "app/elf/elf_module.h"
#include "app/hooks/hook_engine.h"
#include "app/platform/platform.h"
#include "droneview/frontend.h"
#include "runtime.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>

namespace dv::overlay {
namespace {

std::mutex g_install_mutex;
std::mutex g_ui_mutex;
bool g_installed = false;
bool g_failure_logged = false;

using EglSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
std::atomic<EglSwapBuffersFn> g_original_swap{nullptr};
std::atomic<bool> g_original_wait_logged{false};
std::atomic<bool> g_ui_ready{false};
std::atomic<bool> g_disabled{false};
std::atomic<int> g_ui_retry_frames{0};
EGLContext g_ui_context = EGL_NO_CONTEXT;

const char* safe_gl_string(GLenum name) {
    return reinterpret_cast<const char*>(glGetString(name));
}

void ensure_ui_locked() {
    if (g_disabled.load(std::memory_order_acquire)) {
        return;
    }

    const EGLContext context = eglGetCurrentContext();
    if (context == EGL_NO_CONTEXT) {
        g_ui_retry_frames.store(120, std::memory_order_relaxed);
        app::platform::log_error(
            "droneview",
            "overlay: no current EGL context on swap thread (tid=%llu)",
            static_cast<unsigned long long>(app::platform::thread_id()));
        return;
    }

    const EGLSurface draw_surface = eglGetCurrentSurface(EGL_DRAW);
    if (draw_surface == EGL_NO_SURFACE) {
        g_ui_retry_frames.store(120, std::memory_order_relaxed);
        app::platform::log_error(
            "droneview",
            "overlay: no current EGL surface on swap thread (tid=%llu)",
            static_cast<unsigned long long>(app::platform::thread_id()));
        return;
    }

    if (g_ui_ready.load(std::memory_order_acquire)) {
        if (g_ui_context == context) {
            return;
        }
        // The game destroyed and recreated its EGL context. ImGui's GL objects
        // belong to the old context; release the CPU-side context without
        // touching GL and recreate it on the new context below.
        g_ui_ready.store(false, std::memory_order_release);
        g_ui_retry_frames.store(0, std::memory_order_relaxed);
        dv::frontend::reset_for_context_loss();
        app::platform::log_info(
            "droneview", "overlay: EGL context changed; reinitializing (tid=%llu)",
            static_cast<unsigned long long>(app::platform::thread_id()));
    }

    // A failed init is retried later; the first swap can happen before the GL
    // context is fully usable.
    if (g_ui_retry_frames.load(std::memory_order_relaxed) > 0) {
        g_ui_retry_frames.fetch_sub(1, std::memory_order_relaxed);
        return;
    }

    const char* gl_version = safe_gl_string(GL_VERSION);
    const char* gl_renderer = safe_gl_string(GL_RENDERER);
    if (dv::runtime::ensure_ui()) {
        g_ui_context = context;
        g_ui_ready.store(true, std::memory_order_release);
        app::platform::log_info(
            "droneview",
            "overlay UI ready (tid=%llu ctx=%p draw=%p gl='%s' renderer='%s')",
            static_cast<unsigned long long>(app::platform::thread_id()),
            reinterpret_cast<void*>(context), reinterpret_cast<void*>(draw_surface),
            gl_version != nullptr ? gl_version : "?",
            gl_renderer != nullptr ? gl_renderer : "?");
    } else {
        g_ui_retry_frames.store(120, std::memory_order_relaxed);
        app::platform::log_error(
            "droneview", "UI init failed; retrying (tid=%llu ctx=%p gl='%s')",
            static_cast<unsigned long long>(app::platform::thread_id()),
            reinterpret_cast<void*>(context), gl_version != nullptr ? gl_version : "?");
    }
}

EGLBoolean overlay_swap(EGLDisplay display, EGLSurface surface) {
    EglSwapBuffersFn original = g_original_swap.load(std::memory_order_acquire);
    if (original == nullptr) {
        // Dobby/GlossHook makes the hook live before install() publishes the
        // trampoline. Spin briefly instead of dropping the frame.
        for (int i = 0; i < 4096 && original == nullptr; ++i) {
            std::this_thread::yield();
            original = g_original_swap.load(std::memory_order_acquire);
        }
        if (original == nullptr) {
            if (!g_original_wait_logged.exchange(true, std::memory_order_acq_rel)) {
                app::platform::log_error(
                    "droneview",
                    "overlay: original eglSwapBuffers not published yet (tid=%llu)",
                    static_cast<unsigned long long>(app::platform::thread_id()));
            }
            return EGL_FALSE;
        }
        g_original_wait_logged.store(false, std::memory_order_release);
    }

    std::lock_guard<std::mutex> lock(g_ui_mutex);
    if (!g_disabled.load(std::memory_order_acquire)) {
        ensure_ui_locked();
        const EGLContext current = eglGetCurrentContext();
        if (g_ui_ready.load(std::memory_order_acquire) &&
            dv::runtime::ui_ready() && current != EGL_NO_CONTEXT &&
            current == g_ui_context) {
            EGLint width = 0;
            EGLint height = 0;
            if (eglQuerySurface(display, surface, EGL_WIDTH, &width) == EGL_TRUE &&
                eglQuerySurface(display, surface, EGL_HEIGHT, &height) == EGL_TRUE) {
                dv::frontend::set_display_size(width, height);
            }
            dv::runtime::frame();
        }
    }
    return original(display, surface);
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
    if (result.original == nullptr) {
        if (!g_failure_logged) {
            app::platform::log_error(
                "droneview",
                "overlay: hook returned no original eglSwapBuffers (retrying)");
            g_failure_logged = true;
        }
        return false;
    }
    g_original_swap.store(reinterpret_cast<EglSwapBuffersFn>(result.original),
                          std::memory_order_release);
    g_original_wait_logged.store(false, std::memory_order_release);
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

void shutdown() {
    std::lock_guard<std::mutex> lock(g_ui_mutex);
    g_disabled.store(true, std::memory_order_release);
    g_ui_ready.store(false, std::memory_order_release);
    g_ui_retry_frames.store(0, std::memory_order_relaxed);
    g_ui_context = EGL_NO_CONTEXT;
}

}  // namespace dv::overlay
