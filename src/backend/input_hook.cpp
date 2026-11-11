#include "input_hook.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/elf/elf_module.h"
#include "app/hooks/hook_engine.h"
#include "app/platform/platform.h"
#include "droneview/frontend.h"

#if APP_PLATFORM_ANDROID
#include <elf.h>
#include <jni.h>
#include <link.h>

#include "imgui.h"
#endif

namespace dv::input {
namespace {

std::mutex g_mutex;
bool g_installed = false;
bool g_failure_logged = false;
app::uptr g_last_target = 0;
bool g_table_lookup_done = false;
app::uptr g_table_target = 0;
app::uptr g_legacy_target = 0;

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

struct UnitySegment {
    app::uptr start = 0;
    app::uptr end = 0;
    std::uint32_t flags = 0;
};

struct UnityModule {
    std::string path;
    std::vector<UnitySegment> segments;
    bool found = false;
};

int collect_unity_segments_callback(struct dl_phdr_info* info, std::size_t, void* data) {
    auto* module = static_cast<UnityModule*>(data);
    if (module == nullptr || info == nullptr || info->dlpi_name == nullptr ||
        info->dlpi_phdr == nullptr || info->dlpi_phnum <= 0) {
        return 0;
    }
    const std::string path(info->dlpi_name);
    const auto slash = path.find_last_of('/');
    const std::string basename =
        slash == std::string::npos ? path : path.substr(slash + 1);
    if (basename != "libunity.so" && path.find("libunity.so") == std::string::npos) {
        return 0;
    }
    module->path = path;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const auto& phdr = info->dlpi_phdr[i];
        if (phdr.p_type != PT_LOAD) {
            continue;
        }
        UnitySegment segment;
        segment.start = static_cast<app::uptr>(info->dlpi_addr) + phdr.p_vaddr;
        segment.end = segment.start + phdr.p_memsz;
        segment.flags = phdr.p_flags;
        if (segment.end > segment.start) {
            module->segments.push_back(segment);
        }
    }
    module->found = !module->segments.empty();
    return module->found ? 1 : 0;
}

const UnitySegment* find_unity_segment(const std::vector<UnitySegment>& segments,
                                       app::uptr address, std::size_t size) {
    if (size == 0) {
        return nullptr;
    }
    for (const auto& segment : segments) {
        if (address >= segment.start && address <= segment.end &&
            size <= static_cast<std::size_t>(segment.end - address)) {
            return &segment;
        }
    }
    return nullptr;
}

bool read_unity_cstring(const std::vector<UnitySegment>& segments,
                        app::uptr address, std::string* value) {
    if (value == nullptr) {
        return false;
    }
    const UnitySegment* segment = find_unity_segment(segments, address, 1);
    if (segment == nullptr || (segment->flags & PF_R) == 0) {
        return false;
    }
    const char* bytes = reinterpret_cast<const char*>(address);
    const std::size_t available = static_cast<std::size_t>(segment->end - address);
    const std::size_t limit = std::min<std::size_t>(available, 512);
    for (std::size_t i = 0; i < limit; ++i) {
        if (bytes[i] == '\0') {
            value->assign(bytes, i);
            return true;
        }
    }
    return false;
}

std::vector<app::uptr> find_unity_cstring(const std::vector<UnitySegment>& segments,
                                          const char* needle) {
    std::vector<app::uptr> matches;
    if (needle == nullptr || *needle == '\0') {
        return matches;
    }
    const std::size_t needle_size = std::strlen(needle);
    // Prefer data/rodata; fall back to every readable segment if needed.
    for (int pass = 0; pass < 2 && matches.empty(); ++pass) {
        for (const auto& segment : segments) {
            if ((segment.flags & PF_R) == 0) {
                continue;
            }
            if (pass == 0 && (segment.flags & PF_X) != 0) {
                continue;
            }
            const std::size_t size =
                static_cast<std::size_t>(segment.end - segment.start);
            if (size < needle_size + 1) {
                continue;
            }
            const char* bytes = reinterpret_cast<const char*>(segment.start);
            for (std::size_t i = 0; i + needle_size < size; ++i) {
                if (bytes[i] != needle[0]) {
                    continue;
                }
                if (std::memcmp(bytes + i, needle, needle_size) == 0 &&
                    bytes[i + needle_size] == '\0') {
                    matches.push_back(segment.start + i);
                    if (matches.size() >= 32) {
                        return matches;
                    }
                }
            }
        }
    }
    return matches;
}

struct NativeMethodEntry {
    app::uptr entry = 0;
    app::uptr target = 0;
    std::string signature;
};

std::vector<NativeMethodEntry> find_native_method_entries(
    const std::vector<UnitySegment>& segments,
    std::vector<app::uptr> name_addresses) {
    std::vector<NativeMethodEntry> entries;
    constexpr char kSignature[] = "(Landroid/view/InputEvent;)Z";
    const std::size_t pointer_size = sizeof(app::uptr);
    const std::size_t entry_size = 3 * pointer_size;
    const app::uptr alignment_mask = static_cast<app::uptr>(pointer_size) - 1;

    std::sort(name_addresses.begin(), name_addresses.end());
    for (const auto& segment : segments) {
        if ((segment.flags & PF_R) == 0 || (segment.flags & PF_X) != 0) {
            continue;
        }
        app::uptr address = (segment.start + alignment_mask) & ~alignment_mask;
        for (; address + entry_size <= segment.end; address += pointer_size) {
            const app::uptr name = *reinterpret_cast<const app::uptr*>(address);
            if (!std::binary_search(name_addresses.begin(), name_addresses.end(),
                                    name)) {
                continue;
            }
            const app::uptr signature_address =
                *reinterpret_cast<const app::uptr*>(address + pointer_size);
            std::string signature;
            if (!read_unity_cstring(segments, signature_address, &signature) ||
                signature != kSignature) {
                continue;
            }
            const app::uptr target =
                *reinterpret_cast<const app::uptr*>(address + 2 * pointer_size);
            const UnitySegment* target_segment =
                find_unity_segment(segments, target, 1);
            if (target_segment == nullptr || (target_segment->flags & PF_X) == 0) {
                continue;
            }
            entries.push_back(NativeMethodEntry{address, target, signature});
            if (entries.size() >= 8) {
                return entries;
            }
        }
    }
    return entries;
}

app::uptr resolve_from_jni_table() {
    UnityModule module;
    dl_iterate_phdr(collect_unity_segments_callback, &module);
    if (!module.found) {
        return 0;
    }
    const std::vector<app::uptr> names =
        find_unity_cstring(module.segments, "nativeInjectEvent");
    if (names.empty()) {
        app::platform::log_info(
            "droneview", "input hook: nativeInjectEvent string not found in libunity");
        return 0;
    }
    const std::vector<NativeMethodEntry> entries =
        find_native_method_entries(module.segments, names);
    if (entries.empty()) {
        app::platform::log_info(
            "droneview", "input hook: nativeInjectEvent JNI table entry not found");
        return 0;
    }
    for (const auto& entry : entries) {
        app::platform::log_info(
            "droneview",
            "input hook: JNI table candidate entry=0x%lx target=0x%lx signature='%s'",
            static_cast<unsigned long>(entry.entry),
            static_cast<unsigned long>(entry.target), entry.signature.c_str());
    }
    return entries.front().target;
}

app::uptr resolve_from_legacy_slot(app::uptr libunity) {
    constexpr app::uptr kSlot = 0x1888f60;
    app::elf::ElfModule unity;
    std::optional<app::elf::ElfModule::MemoryRange> range;
    if (unity.open("libunity.so")) {
        range = unity.memory_range();
    }
    const app::uptr slot_address = libunity + kSlot;
    if (!range || slot_address < range->start ||
        slot_address + sizeof(app::uptr) > range->start + range->size) {
        if (!g_failure_logged) {
            app::platform::log_error(
                "droneview",
                "input hook: legacy slot 0x%lx outside libunity mapping (range=%s) tid=%llu",
                static_cast<unsigned long>(slot_address),
                range ? "known" : "unknown",
                static_cast<unsigned long long>(app::platform::thread_id()));
            g_failure_logged = true;
        }
        return 0;
    }
    const app::uptr target = *reinterpret_cast<app::uptr*>(slot_address);
    if (target == 0) {
        if (!g_failure_logged) {
            app::platform::log_error("droneview",
                                     "input hook: legacy slot empty");
            g_failure_logged = true;
        }
        return 0;
    }
    const std::string target_path =
        app::platform::module_path(reinterpret_cast<const void*>(target));
    if (target_path.find("libunity") == std::string::npos) {
        if (!g_failure_logged) {
            app::platform::log_error(
                "droneview",
                "input hook: legacy slot 0x%lx -> 0x%lx outside libunity (path='%s'); skipping",
                static_cast<unsigned long>(slot_address),
                static_cast<unsigned long>(target), target_path.c_str());
            g_failure_logged = true;
        }
        return 0;
    }
    app::platform::log_info(
        "droneview", "input hook: legacy slot 0x%lx -> 0x%lx path='%s' tid=%llu",
        static_cast<unsigned long>(slot_address),
        static_cast<unsigned long>(target), target_path.c_str(),
        static_cast<unsigned long long>(app::platform::thread_id()));
    return target;
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

    if (!g_table_lookup_done) {
        g_table_lookup_done = true;
        g_table_target = resolve_from_jni_table();
        if (g_table_target != 0) {
            app::platform::log_info(
                "droneview",
                "input hook: nativeInjectEvent resolved from JNI table @ 0x%lx",
                static_cast<unsigned long>(g_table_target));
        } else {
            app::platform::log_info(
                "droneview",
                "input hook: JNI table lookup failed; trying legacy slot");
        }
    }

    app::uptr target = g_table_target;
    if (target == 0) {
        if (g_legacy_target == 0) {
            g_legacy_target = resolve_from_legacy_slot(libunity);
        }
        target = g_legacy_target;
    }
    if (target == 0) {
        return false;
    }

    const std::string target_path =
        app::platform::module_path(reinterpret_cast<const void*>(target));
    if (target_path.find("libunity") == std::string::npos) {
        if (!g_failure_logged) {
            app::platform::log_error(
                "droneview",
                "input hook: resolved target 0x%lx outside libunity (path='%s'); skipping",
                static_cast<unsigned long>(target), target_path.c_str());
            g_failure_logged = true;
        }
        return false;
    }
    if (target == g_last_target) {
        return false;
    }

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
    // Only remember the target after a successful install so a failed attempt
    // can be retried instead of blocking every later frame.
    g_last_target = target;
    g_original = reinterpret_cast<InjectEventFn>(result.original);
    g_installed = true;
    g_failure_logged = false;
    app::platform::log_info(
        "droneview", "input hook installed: nativeInjectEvent @ 0x%lx tid=%llu",
        static_cast<unsigned long>(target),
        static_cast<unsigned long long>(app::platform::thread_id()));
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
