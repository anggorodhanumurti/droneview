#include "initial_check.h"

#include "app/platform/platform.h"

#if APP_PLATFORM_ANDROID
#include <jni.h>
#endif

namespace dv::initial_check {

namespace {

constexpr std::string_view kSupportedVersion = "2.2.16";

// MLBB exposes the native IL2CPP runtime through these libraries. They appear
// in this order during startup; any one of them means the game has begun
// loading its native side.
const char* const kGameLibraries[] = {
    "libfirst.so",
    "libcsharp.so",
    "liblogic.so",
};

#if APP_PLATFORM_ANDROID

struct JniEnvScope {
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    bool attached = false;

    bool acquire() {
        vm = static_cast<JavaVM*>(app::platform::java_vm());
        if (vm == nullptr) {
            return false;
        }
        jint result = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (result == JNI_EDETACHED) {
            if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
                env = nullptr;
                return false;
            }
            attached = true;
        } else if (result != JNI_OK) {
            env = nullptr;
            return false;
        }
        return env != nullptr;
    }

    ~JniEnvScope() {
        if (attached && vm != nullptr) {
            vm->DetachCurrentThread();
        }
    }
};

bool jstring_to_std(JNIEnv* env, jstring value, std::string* out) {
    if (env == nullptr || value == nullptr || out == nullptr) {
        return false;
    }
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        env->ExceptionClear();
        return false;
    }
    *out = chars;
    env->ReleaseStringUTFChars(value, chars);
    return true;
}

bool detect_installed_version(std::string* version) {
    if (version == nullptr) {
        return false;
    }
    JniEnvScope scope;
    if (!scope.acquire()) {
        return false;
    }
    JNIEnv* env = scope.env;

    bool ok = false;
    jclass activity_thread = env->FindClass("android/app/ActivityThread");
    jclass context_class = env->FindClass("android/content/Context");
    jclass package_manager_class =
        env->FindClass("android/content/pm/PackageManager");
    jclass package_info_class = env->FindClass("android/content/pm/PackageInfo");

    jobject application = nullptr;
    jobject package_manager = nullptr;
    jobject package_info = nullptr;
    jstring package_name = nullptr;
    jstring version_name = nullptr;

    do {
        if (activity_thread == nullptr || context_class == nullptr ||
            package_manager_class == nullptr || package_info_class == nullptr) {
            env->ExceptionClear();
            break;
        }

        jmethodID current_application = env->GetStaticMethodID(
            activity_thread, "currentApplication",
            "()Landroid/app/Application;");
        if (current_application == nullptr) {
            env->ExceptionClear();
            break;
        }
        application =
            env->CallStaticObjectMethod(activity_thread, current_application);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            break;
        }
        if (application == nullptr) {
            break;
        }

        jmethodID get_package_name =
            env->GetMethodID(context_class, "getPackageName",
                             "()Ljava/lang/String;");
        jmethodID get_package_manager =
            env->GetMethodID(context_class, "getPackageManager",
                             "()Landroid/content/pm/PackageManager;");
        if (get_package_name == nullptr || get_package_manager == nullptr) {
            env->ExceptionClear();
            break;
        }

        package_name =
            static_cast<jstring>(env->CallObjectMethod(application, get_package_name));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            break;
        }
        if (package_name == nullptr) {
            break;
        }

        package_manager =
            env->CallObjectMethod(application, get_package_manager);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            break;
        }
        if (package_manager == nullptr) {
            break;
        }

        jmethodID get_package_info = env->GetMethodID(
            package_manager_class, "getPackageInfo",
            "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
        if (get_package_info == nullptr) {
            env->ExceptionClear();
            break;
        }
        package_info = env->CallObjectMethod(package_manager, get_package_info,
                                             package_name, 0);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            break;
        }
        if (package_info == nullptr) {
            break;
        }

        jfieldID version_name_field = env->GetFieldID(
            package_info_class, "versionName", "Ljava/lang/String;");
        if (version_name_field == nullptr) {
            env->ExceptionClear();
            break;
        }
        version_name = static_cast<jstring>(
            env->GetObjectField(package_info, version_name_field));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            break;
        }
        if (version_name == nullptr) {
            break;
        }

        ok = jstring_to_std(env, version_name, version);
    } while (false);

    if (version_name != nullptr) {
        env->DeleteLocalRef(version_name);
    }
    if (package_name != nullptr) {
        env->DeleteLocalRef(package_name);
    }
    if (package_info != nullptr) {
        env->DeleteLocalRef(package_info);
    }
    if (package_manager != nullptr) {
        env->DeleteLocalRef(package_manager);
    }
    if (application != nullptr) {
        env->DeleteLocalRef(application);
    }
    if (package_info_class != nullptr) {
        env->DeleteLocalRef(package_info_class);
    }
    if (package_manager_class != nullptr) {
        env->DeleteLocalRef(package_manager_class);
    }
    if (context_class != nullptr) {
        env->DeleteLocalRef(context_class);
    }
    if (activity_thread != nullptr) {
        env->DeleteLocalRef(activity_thread);
    }
    return ok;
}

#endif

}  // namespace

bool any_game_library_loaded() {
    for (const char* name : kGameLibraries) {
        if (app::platform::module_base(name) != 0) {
            return true;
        }
    }
    return false;
}

bool is_supported_version(std::string_view version) {
    if (version.size() < kSupportedVersion.size()) {
        return false;
    }
    if (version.compare(0, kSupportedVersion.size(), kSupportedVersion) != 0) {
        return false;
    }
    if (version.size() == kSupportedVersion.size()) {
        return true;
    }
    const char next = version[kSupportedVersion.size()];
    return next == '.' || next == '-' || next == '_' || next == ' ';
}

std::string unsupported_version_status(const std::string& version) {
    const std::string detected = version.empty() ? "unknown" : version;
    return "Not Ready: " + detected +
           " is not supported! please download MLBB v2.2.16";
}

Result run() {
    Result result;
#if APP_PLATFORM_ANDROID
    if (!detect_installed_version(&result.version)) {
        result.status = Status::VersionPending;
        return result;
    }
#else
    return result;
#endif
    if (!is_supported_version(result.version)) {
        result.status = Status::VersionUnsupported;
        return result;
    }
    if (!any_game_library_loaded()) {
        result.status = Status::GameLibraryMissing;
        return result;
    }
    result.status = Status::Passed;
    return result;
}

}  // namespace dv::initial_check
