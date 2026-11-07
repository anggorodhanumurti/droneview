#include "input_hook.h"

#include <atomic>
#include <mutex>
#include <string>

#include "app/hooks/hook_engine.h"
#include "app/platform/platform.h"
#include "droneview/frontend.h"

#if APP_PLATFORM_ANDROID
#include <jni.h>

#include "imgui.h"
#endif

namespace dv::input {
namespace {

std::mutex g_mutex;
bool g_installed = false;
bool g_failure_logged = false;
app::uptr g_last_target = 0;

#if APP_PLATFORM_ANDROID
using InjectEventFn = jboolean (*)(JNIEnv*, jobject, jobject);
InjectEventFn g_original = nullptr;
std::atomic<int> g_event_logs{0};
std::atomic<bool> g_swallow_touch{false};

bool feed_motion(JNIEnv* env, jobject thiz, jobject event) {
    if (env == nullptr || thiz == nullptr || event == nullptr ||
        !dv::frontend::initialized()) {
        return false;
    }

    jclass event_class = env->GetObjectClass(event);
    if (event_class == nullptr) {
        return false;
    }
    jmethodID get_action = env->GetMethodID(event_class, "getAction", "()I");
    jmethodID get_x = env->GetMethodID(event_class, "getX", "()F");
    jmethodID get_y = env->GetMethodID(event_class, "getY", "()F");
    if (get_action == nullptr || get_x == nullptr || get_y == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(event_class);
        return false;
    }
    const jint action = env->CallIntMethod(event, get_action);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(event_class);
        return false;
    }
    const int action_code = static_cast<int>(action) & 0xff;
    if (action_code != 0 && action_code != 1 && action_code != 2 &&
        action_code != 3) {
        env->DeleteLocalRef(event_class);
        return false;
    }

    const float raw_x = env->CallFloatMethod(event, get_x);
    const float raw_y = env->CallFloatMethod(event, get_y);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(event_class);
        return false;
    }

    jclass view_class = env->GetObjectClass(thiz);
    jmethodID get_width =
        view_class != nullptr ? env->GetMethodID(view_class, "getWidth", "()I") : nullptr;
    jmethodID get_height =
        view_class != nullptr ? env->GetMethodID(view_class, "getHeight", "()I") : nullptr;
    const int view_w = (get_width != nullptr) ? env->CallIntMethod(thiz, get_width) : 0;
    const int view_h = (get_height != nullptr) ? env->CallIntMethod(thiz, get_height) : 0;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (view_class != nullptr) {
        env->DeleteLocalRef(view_class);
    }

    ImGuiIO& io = ImGui::GetIO();
    const float scale_x = (view_w > 0 && io.DisplaySize.x > 0.0f)
                              ? io.DisplaySize.x / static_cast<float>(view_w)
                              : 1.0f;
    const float scale_y = (view_h > 0 && io.DisplaySize.y > 0.0f)
                              ? io.DisplaySize.y / static_cast<float>(view_h)
                              : 1.0f;
    const float x = raw_x * scale_x;
    const float y = raw_y * scale_y;

    if (action_code == 0) {
        g_swallow_touch.store(dv::frontend::touch_captures(x, y));
    }
    const bool consumed = g_swallow_touch.load();
    if (consumed) {
        dv::frontend::queue_touch(x, y, action_code);
    }
    if (action_code == 1 || action_code == 3) {
        g_swallow_touch.store(false);
    }

    if ((action_code == 0 || action_code == 1 || action_code == 3) &&
        g_event_logs.fetch_add(1) < 64) {
        app::platform::log_info(
            "droneview", "touch action=%d x=%.1f,%.1f view=%dx%d menu=%.1f,%.1f consumed=%d",
            action_code, raw_x, raw_y, view_w, view_h, x, y, consumed ? 1 : 0);
    }
    env->DeleteLocalRef(event_class);
    return consumed;
}

jboolean input_replacement(JNIEnv* env, jobject thiz, jobject event) {
    const bool consumed = feed_motion(env, thiz, event);
    if (consumed) {
        return JNI_TRUE;
    }
    if (g_original != nullptr) {
        return g_original(env, thiz, event);
    }
    return JNI_FALSE;
}
#endif

}  // namespace

bool install() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_installed) {
        return true;
    }
#if APP_PLATFORM_ANDROID
    const app::uptr libunity = app::platform::module_base("libunity.so");
    if (libunity == 0) {
        return false;
    }
    // JNINativeMethod table entry for nativeInjectEvent in the target Unity
    // build. Validate that the slot points inside libunity before hooking:
    // hooking an unresolved/garbage slot is what freezes the game.
    constexpr app::uptr kSlot = 0x1888f60;
    const app::uptr target = *reinterpret_cast<app::uptr*>(libunity + kSlot);
    if (target == 0) {
        return false;
    }
    const std::string target_path =
        app::platform::module_path(reinterpret_cast<const void*>(target));
    if (target_path.find("libunity") == std::string::npos) {
        if (!g_failure_logged) {
            app::platform::log_error(
                "droneview",
                "input hook: slot 0x%lx -> 0x%lx outside libunity (path='%s'); skipping",
                static_cast<unsigned long>(libunity + kSlot),
                static_cast<unsigned long>(target), target_path.c_str());
            g_failure_logged = true;
        }
        return false;
    }
    if (target == g_last_target) {
        return false;
    }
    g_last_target = target;

    app::hooks::HookEngine engine;
    const app::hooks::HookResult result = engine.install(
        reinterpret_cast<void*>(target), reinterpret_cast<void*>(&input_replacement));
    if (!result.ok) {
        if (!g_failure_logged) {
            app::platform::log_error("droneview", "input hook failed: %s",
                                     result.error.c_str());
            g_failure_logged = true;
        }
        return false;
    }
    g_original = reinterpret_cast<InjectEventFn>(result.original);
    g_installed = true;
    g_failure_logged = false;
    app::platform::log_info("droneview",
                            "input hook installed: nativeInjectEvent @ 0x%lx",
                            static_cast<unsigned long>(target));
    return true;
#else
    return false;
#endif
}

bool installed() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_installed;
}

}  // namespace dv::input
