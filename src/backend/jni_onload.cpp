#include "app/platform/platform.h"

#if APP_PLATFORM_ANDROID
#include <jni.h>

// entry.cpp starts the runtime from a static initializer. Also start here for
// loaders that call JNI_OnLoad without running ELF initializers.
extern "C" void dv_start(void);

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env = nullptr;
    if (vm == nullptr ||
        vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        app::platform::log_error("droneview", "JNI_OnLoad: GetEnv failed");
        return -1;
    }
    app::platform::set_java_vm(vm);
    app::platform::log_info("droneview", "JNI_OnLoad: JavaVM stored (%p)",
                            static_cast<void*>(vm));
#if DRONEVIEW_AUTOSTART
    dv_start();
#endif
    return JNI_VERSION_1_6;
}
#endif
