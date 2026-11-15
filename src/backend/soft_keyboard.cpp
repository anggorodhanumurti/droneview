#include "soft_keyboard.h"

#if APP_PLATFORM_ANDROID

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "app/platform/platform.h"
#include "droneview/frontend.h"
#include "imgui.h"

namespace dv::soft_keyboard {
namespace {

std::mutex g_mutex;
jobject g_host_view = nullptr;
jobject g_edit_text = nullptr;
jobject g_imm = nullptr;
std::thread::id g_ui_thread;
std::atomic<bool> g_visible{false};
std::u16string g_mirror;

JNIEnv* acquire_env() {
    auto* vm = static_cast<JavaVM*>(app::platform::java_vm());
    if (vm == nullptr) {
        return nullptr;
    }
    JNIEnv* env = nullptr;
    const jint result =
        vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return nullptr;
        }
    } else if (result != JNI_OK) {
        return nullptr;
    }
    return env;
}

void drain_exception(JNIEnv* env, const char* where) {
    if (!env->ExceptionCheck()) {
        return;
    }
    env->ExceptionDescribe();
    env->ExceptionClear();
    app::platform::log_error("droneview",
                             "soft keyboard: JNI exception in %s", where);
}

void clear_text_locked(JNIEnv* env) {
    g_mirror.clear();
    if (g_edit_text == nullptr) {
        return;
    }
    jclass text_view_class = env->FindClass("android/widget/TextView");
    if (text_view_class == nullptr) {
        drain_exception(env, "find TextView");
        return;
    }
    jmethodID set_text = env->GetMethodID(
        text_view_class, "setText", "(Ljava/lang/CharSequence;)V");
    jstring empty = env->NewStringUTF("");
    if (set_text != nullptr && empty != nullptr) {
        env->CallVoidMethod(g_edit_text, set_text, empty);
    }
    if (empty != nullptr) {
        env->DeleteLocalRef(empty);
    }
    env->DeleteLocalRef(text_view_class);
    drain_exception(env, "clear text");
}

bool create_locked(JNIEnv* env, jobject host_view) {
    if (g_edit_text != nullptr) {
        return true;
    }
    if (env == nullptr || host_view == nullptr) {
        return false;
    }

    env->PushLocalFrame(64);

    jclass view_class = env->FindClass("android/view/View");
    jclass text_view_class = env->FindClass("android/widget/TextView");
    jclass edit_text_class = env->FindClass("android/widget/EditText");
    jclass context_class = env->FindClass("android/content/Context");
    jclass view_group_class = env->FindClass("android/view/ViewGroup");
    jclass layout_params_class =
        env->FindClass("android/view/ViewGroup$LayoutParams");
    if (view_class == nullptr || text_view_class == nullptr ||
        edit_text_class == nullptr || context_class == nullptr ||
        view_group_class == nullptr || layout_params_class == nullptr) {
        drain_exception(env, "find classes");
        env->PopLocalFrame(nullptr);
        return false;
    }

    jmethodID get_context = env->GetMethodID(
        view_class, "getContext", "()Landroid/content/Context;");
    jmethodID get_root =
        env->GetMethodID(view_class, "getRootView", "()Landroid/view/View;");
    jobject context =
        get_context != nullptr ? env->CallObjectMethod(host_view, get_context)
                               : nullptr;
    jobject root =
        get_root != nullptr ? env->CallObjectMethod(host_view, get_root) : nullptr;
    if (context == nullptr || root == nullptr) {
        drain_exception(env, "resolve context");
        env->PopLocalFrame(nullptr);
        return false;
    }

    jmethodID edit_ctor = env->GetMethodID(
        edit_text_class, "<init>", "(Landroid/content/Context;)V");
    jobject edit = edit_ctor != nullptr
                       ? env->NewObject(edit_text_class, edit_ctor, context)
                       : nullptr;
    if (edit == nullptr) {
        drain_exception(env, "create EditText");
        env->PopLocalFrame(nullptr);
        return false;
    }

    jmethodID set_input_type =
        env->GetMethodID(text_view_class, "setInputType", "(I)V");
    jmethodID set_ime_options =
        env->GetMethodID(text_view_class, "setImeOptions", "(I)V");
    jmethodID set_cursor_visible =
        env->GetMethodID(text_view_class, "setCursorVisible", "(Z)V");
    jmethodID set_background =
        env->GetMethodID(view_class, "setBackgroundColor", "(I)V");
    jmethodID set_focusable =
        env->GetMethodID(view_class, "setFocusableInTouchMode", "(Z)V");

    // TYPE_CLASS_TEXT | TYPE_TEXT_FLAG_MULTI_LINE |
    // TYPE_TEXT_FLAG_NO_SUGGESTIONS.
    if (set_input_type != nullptr) {
        env->CallVoidMethod(edit, set_input_type, 0x000A0001);
    }
    // IME_FLAG_NO_EXTRACT_UI | IME_FLAG_NO_FULLSCREEN.
    if (set_ime_options != nullptr) {
        env->CallVoidMethod(edit, set_ime_options, 0x12000000);
    }
    if (set_background != nullptr) {
        env->CallVoidMethod(edit, set_background, 0);
    }
    if (set_cursor_visible != nullptr) {
        env->CallVoidMethod(edit, set_cursor_visible, JNI_FALSE);
    }
    if (set_focusable != nullptr) {
        env->CallVoidMethod(edit, set_focusable, JNI_TRUE);
    }
    drain_exception(env, "configure EditText");

    jmethodID layout_ctor =
        env->GetMethodID(layout_params_class, "<init>", "(II)V");
    jobject layout_params =
        layout_ctor != nullptr
            ? env->NewObject(layout_params_class, layout_ctor, 1, 1)
            : nullptr;
    jmethodID add_view = env->GetMethodID(
        view_group_class, "addView",
        "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
    if (layout_params != nullptr && add_view != nullptr) {
        env->CallVoidMethod(root, add_view, edit, layout_params);
    }
    drain_exception(env, "attach EditText");

    jmethodID get_service = env->GetMethodID(
        context_class, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;");
    jobject imm = nullptr;
    if (get_service != nullptr) {
        jstring name = env->NewStringUTF("input_method");
        if (name != nullptr) {
            imm = env->CallObjectMethod(context, get_service, name);
            env->DeleteLocalRef(name);
        }
    }
    drain_exception(env, "get InputMethodManager");
    if (imm == nullptr) {
        env->PopLocalFrame(nullptr);
        return false;
    }

    g_host_view = env->NewGlobalRef(host_view);
    g_edit_text = env->NewGlobalRef(edit);
    g_imm = env->NewGlobalRef(imm);
    env->PopLocalFrame(nullptr);

    if (g_host_view == nullptr || g_edit_text == nullptr || g_imm == nullptr) {
        if (g_host_view != nullptr) {
            env->DeleteGlobalRef(g_host_view);
            g_host_view = nullptr;
        }
        if (g_edit_text != nullptr) {
            env->DeleteGlobalRef(g_edit_text);
            g_edit_text = nullptr;
        }
        if (g_imm != nullptr) {
            env->DeleteGlobalRef(g_imm);
            g_imm = nullptr;
        }
        return false;
    }

    app::platform::log_info("droneview",
                            "soft keyboard: EditText bridge created");
    return true;
}

void hide_locked(JNIEnv* env, bool ui_thread) {
    if (g_edit_text != nullptr && g_imm != nullptr) {
        jclass view_class = env->FindClass("android/view/View");
        if (view_class != nullptr) {
            jmethodID get_token = env->GetMethodID(
                view_class, "getWindowToken", "()Landroid/os/IBinder;");
            jobject token =
                get_token != nullptr
                    ? env->CallObjectMethod(g_edit_text, get_token)
                    : nullptr;

            jclass imm_class = env->GetObjectClass(g_imm);
            if (imm_class != nullptr) {
                jmethodID hide_soft = env->GetMethodID(
                    imm_class, "hideSoftInputFromWindow",
                    "(Landroid/os/IBinder;I)Z");
                if (hide_soft != nullptr) {
                    env->CallBooleanMethod(g_imm, hide_soft, token, 0);
                }
                env->DeleteLocalRef(imm_class);
            }
            if (token != nullptr) {
                env->DeleteLocalRef(token);
            }

            if (ui_thread) {
                jmethodID clear_focus =
                    env->GetMethodID(view_class, "clearFocus", "()V");
                if (clear_focus != nullptr) {
                    env->CallVoidMethod(g_edit_text, clear_focus);
                }
                jmethodID request_focus =
                    env->GetMethodID(view_class, "requestFocus", "()Z");
                if (request_focus != nullptr && g_host_view != nullptr) {
                    env->CallBooleanMethod(g_host_view, request_focus);
                }
            }
            env->DeleteLocalRef(view_class);
        }
    }
    clear_text_locked(env);
    g_visible.store(false, std::memory_order_release);
    drain_exception(env, "hide");
}

}  // namespace

void prepare(JNIEnv* env, jobject host_view) {
    if (env == nullptr) {
        env = acquire_env();
    }
    if (env == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    create_locked(env, host_view);
}

void show(JNIEnv* env, jobject host_view) {
    if (env == nullptr) {
        env = acquire_env();
    }
    if (env == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!create_locked(env, host_view)) {
        return;
    }
    g_ui_thread = std::this_thread::get_id();
    clear_text_locked(env);

    jclass view_class = env->FindClass("android/view/View");
    if (view_class != nullptr) {
        jmethodID request_focus =
            env->GetMethodID(view_class, "requestFocus", "()Z");
        if (request_focus != nullptr) {
            env->CallBooleanMethod(g_edit_text, request_focus);
        }
        env->DeleteLocalRef(view_class);
    }

    bool shown = false;
    jclass imm_class = env->GetObjectClass(g_imm);
    if (imm_class != nullptr) {
        jmethodID show_soft = env->GetMethodID(
            imm_class, "showSoftInput", "(Landroid/view/View;I)Z");
        if (show_soft != nullptr) {
            shown = env->CallBooleanMethod(g_imm, show_soft, g_edit_text, 0) == JNI_TRUE;
        }
        env->DeleteLocalRef(imm_class);
    }
    drain_exception(env, "show");
    g_visible.store(true, std::memory_order_release);
    app::platform::log_info("droneview", "soft keyboard: show (%s)",
                            shown ? "accepted" : "pending");
}

void hide(JNIEnv* env) {
    if (env == nullptr) {
        env = acquire_env();
    }
    if (env == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool ui_thread = std::this_thread::get_id() == g_ui_thread;
    const bool was_visible = g_visible.load(std::memory_order_acquire);
    hide_locked(env, ui_thread);
    if (was_visible) {
        app::platform::log_info("droneview", "soft keyboard: hide");
    }
}

bool visible() {
    return g_visible.load(std::memory_order_acquire);
}

void destroy() {
    JNIEnv* env = acquire_env();
    if (env != nullptr) {
        std::lock_guard<std::mutex> lock(g_mutex);
        const bool ui_thread = std::this_thread::get_id() == g_ui_thread;
        hide_locked(env, ui_thread);
        if (ui_thread && g_edit_text != nullptr) {
            jclass view_class = env->FindClass("android/view/View");
            jmethodID get_parent = view_class != nullptr
                                       ? env->GetMethodID(
                                             view_class, "getParent",
                                             "()Landroid/view/ViewParent;")
                                       : nullptr;
            jobject parent = get_parent != nullptr
                                 ? env->CallObjectMethod(g_edit_text, get_parent)
                                 : nullptr;
            if (parent != nullptr) {
                jclass view_group_class =
                    env->FindClass("android/view/ViewGroup");
                jmethodID remove_view =
                    view_group_class != nullptr
                        ? env->GetMethodID(
                              view_group_class, "removeView",
                              "(Landroid/view/View;)V")
                        : nullptr;
                if (remove_view != nullptr) {
                    env->CallVoidMethod(parent, remove_view, g_edit_text);
                }
                env->DeleteLocalRef(parent);
                if (view_group_class != nullptr) {
                    env->DeleteLocalRef(view_group_class);
                }
            }
            if (view_class != nullptr) {
                env->DeleteLocalRef(view_class);
            }
        }
        drain_exception(env, "destroy");
        if (g_edit_text != nullptr) {
            env->DeleteGlobalRef(g_edit_text);
            g_edit_text = nullptr;
        }
        if (g_host_view != nullptr) {
            env->DeleteGlobalRef(g_host_view);
            g_host_view = nullptr;
        }
        if (g_imm != nullptr) {
            env->DeleteGlobalRef(g_imm);
            g_imm = nullptr;
        }
        g_ui_thread = std::thread::id();
        g_mirror.clear();
    }
    g_visible.store(false, std::memory_order_release);
}

void poll() {
    if (!g_visible.load(std::memory_order_acquire)) {
        return;
    }
    JNIEnv* env = acquire_env();
    if (env == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_edit_text == nullptr) {
        return;
    }

    env->PushLocalFrame(16);
    jclass text_view_class = env->FindClass("android/widget/TextView");
    jmethodID get_text = text_view_class != nullptr
                             ? env->GetMethodID(
                                   text_view_class, "getText",
                                   "()Ljava/lang/CharSequence;")
                             : nullptr;
    jobject editable =
        get_text != nullptr ? env->CallObjectMethod(g_edit_text, get_text)
                            : nullptr;
    jclass object_class = env->FindClass("java/lang/Object");
    jmethodID to_string = object_class != nullptr
                              ? env->GetMethodID(object_class, "toString",
                                                 "()Ljava/lang/String;")
                              : nullptr;
    jstring text = (editable != nullptr && to_string != nullptr)
                       ? static_cast<jstring>(
                             env->CallObjectMethod(editable, to_string))
                       : nullptr;
    if (text == nullptr) {
        drain_exception(env, "read IME text");
        env->PopLocalFrame(nullptr);
        return;
    }

    const jsize length = env->GetStringLength(text);
    std::u16string utf16(static_cast<std::size_t>(length), u'\0');
    if (length > 0) {
        env->GetStringRegion(text, 0, length,
                             reinterpret_cast<jchar*>(&utf16[0]));
    }
    env->PopLocalFrame(nullptr);
    drain_exception(env, "read IME text");

    if (utf16 == g_mirror) {
        return;
    }

    std::size_t prefix = 0;
    const std::size_t old_length = g_mirror.size();
    const std::size_t new_length = utf16.size();
    while (prefix < old_length && prefix < new_length &&
           g_mirror[prefix] == utf16[prefix]) {
        ++prefix;
    }

    if (dv::frontend::menu_active() &&
        dv::frontend::console_view_active() &&
        !dv::frontend::text_input_wanted()) {
        bool submitted = false;
        for (std::size_t i = prefix; i < new_length; ++i) {
            const char16_t unit = utf16[i];
            if (unit == u'\n' || unit == u'\r') {
                dv::frontend::queue_menu_key(ImGuiKey_Enter);
                submitted = true;
                break;
            }
            unsigned int codepoint = unit;
            if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < new_length) {
                const char16_t low = utf16[i + 1];
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    codepoint = 0x10000u +
                                ((static_cast<unsigned int>(unit) - 0xD800u)
                                 << 10) +
                                (static_cast<unsigned int>(low) - 0xDC00u);
                    ++i;
                }
            }
            dv::frontend::queue_menu_char(codepoint);
        }
        g_mirror = utf16;
        if (submitted) {
            clear_text_locked(env);
        }
        return;
    }

    for (std::size_t i = prefix; i < old_length; ++i) {
        dv::frontend::queue_key(ImGuiKey_Backspace, true);
        dv::frontend::queue_key(ImGuiKey_Backspace, false);
    }

    bool submitted = false;
    for (std::size_t i = prefix; i < new_length; ++i) {
        const char16_t unit = utf16[i];
        if (unit == u'\n' || unit == u'\r') {
            dv::frontend::queue_key(ImGuiKey_Enter, true);
            dv::frontend::queue_key(ImGuiKey_Enter, false);
            submitted = true;
            break;
        }
        unsigned int codepoint = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < new_length) {
            const char16_t low = utf16[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000u +
                            ((static_cast<unsigned int>(unit) - 0xD800u) << 10) +
                            (static_cast<unsigned int>(low) - 0xDC00u);
                ++i;
            }
        }
        dv::frontend::queue_character(codepoint);
    }

    g_mirror = utf16;
    if (submitted) {
        clear_text_locked(env);
    }
}

}  // namespace dv::soft_keyboard

#endif  // APP_PLATFORM_ANDROID
