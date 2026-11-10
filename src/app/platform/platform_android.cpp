#include "app/platform/platform.h"

#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace app::platform {

namespace {

std::atomic<void*> g_java_vm{nullptr};

int to_prot(MemoryProtection protection) {
    int prot = 0;
    const auto value = static_cast<std::uint32_t>(protection);
    if (value & static_cast<std::uint32_t>(MemoryProtection::Read)) {
        prot |= PROT_READ;
    }
    if (value & static_cast<std::uint32_t>(MemoryProtection::Write)) {
        prot |= PROT_WRITE;
    }
    if (value & static_cast<std::uint32_t>(MemoryProtection::Execute)) {
        prot |= PROT_EXEC;
    }
    return prot;
}

struct FindContext {
    std::string wanted;
    std::uintptr_t base = 0;
};

int find_module_callback(struct dl_phdr_info* info, std::size_t, void* data) {
    auto* ctx = static_cast<FindContext*>(data);
    if (ctx == nullptr || info == nullptr || info->dlpi_name == nullptr) {
        return 0;
    }
    const std::string path(info->dlpi_name);
    if (path.empty()) {
        return 0;
    }
    const auto slash = path.find_last_of('/');
    const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    if (base == ctx->wanted || path == ctx->wanted || path.find(ctx->wanted) != std::string::npos) {
        ctx->base = static_cast<std::uintptr_t>(info->dlpi_addr);
        return 1;
    }
    return 0;
}

}  // namespace

void log_info(const char* tag, const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, tag != nullptr ? tag : "app", fmt, args);
    va_end(args);
}

void log_error(const char* tag, const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_ERROR, tag != nullptr ? tag : "app", fmt, args);
    va_end(args);
}

void* open_library(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    return dlopen(name, RTLD_NOW);
}

void* find_symbol(void* handle, const char* name) {
    if (handle == nullptr || name == nullptr) {
        return nullptr;
    }
    return dlsym(handle, name);
}

void close_library(void* handle) {
    if (handle != nullptr) {
        dlclose(handle);
    }
}

std::uintptr_t module_base(const char* name) {
    if (name == nullptr || *name == '\0') {
        return 0;
    }
    FindContext ctx{name, 0};
    dl_iterate_phdr(find_module_callback, &ctx);
    return ctx.base;
}

std::string module_path(const void* addr) {
    if (addr == nullptr) {
        return {};
    }
    Dl_info info{};
    if (dladdr(addr, &info) == 0 || info.dli_fname == nullptr) {
        return {};
    }
    return info.dli_fname;
}

bool protect_memory(void* addr, std::size_t size, MemoryProtection protection) {
    if (addr == nullptr || size == 0) {
        return false;
    }
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return false;
    }
    const std::uintptr_t page_mask = static_cast<std::uintptr_t>(page_size) - 1u;
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(addr) & ~page_mask;
    const std::uintptr_t end =
        (reinterpret_cast<std::uintptr_t>(addr) + size + page_mask) & ~page_mask;
    return mprotect(reinterpret_cast<void*>(start), end - start, to_prot(protection)) == 0;
}

int api_level() {
    char value[PROP_VALUE_MAX] = {};
    const int len = __system_property_get("ro.build.version.sdk", value);
    if (len <= 0) {
        return 0;
    }
    return std::atoi(value);
}

std::uint64_t thread_id() {
    return static_cast<std::uint64_t>(::syscall(SYS_gettid));
}

void set_java_vm(void* vm) {
    g_java_vm.store(vm, std::memory_order_release);
}

void* java_vm() {
    void* vm = g_java_vm.load(std::memory_order_acquire);
    if (vm != nullptr) {
        return vm;
    }
    // Loaders that inject the library natively may never call JNI_OnLoad.
    // JNI_GetCreatedJavaVMs is exported by libnativehelper.so on Android.
    using GetCreatedJavaVmsFn = int (*)(void**, int, int*);
    auto resolve = []() -> GetCreatedJavaVmsFn {
        auto fn = reinterpret_cast<GetCreatedJavaVmsFn>(
            dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs"));
        if (fn != nullptr) {
            return fn;
        }
        void* helper = dlopen("libnativehelper.so", RTLD_NOW);
        if (helper == nullptr) {
            return nullptr;
        }
        return reinterpret_cast<GetCreatedJavaVmsFn>(
            dlsym(helper, "JNI_GetCreatedJavaVMs"));
    };
    static std::atomic<GetCreatedJavaVmsFn> cached{nullptr};
    GetCreatedJavaVmsFn get_created = cached.load(std::memory_order_acquire);
    if (get_created == nullptr) {
        get_created = resolve();
        if (get_created == nullptr) {
            return nullptr;
        }
        cached.store(get_created, std::memory_order_release);
    }
    void* vms[1] = {};
    int count = 0;
    if (get_created(vms, 1, &count) != 0 || count < 1 || vms[0] == nullptr) {
        return nullptr;
    }
    g_java_vm.store(vms[0], std::memory_order_release);
    return vms[0];
}

}  // namespace app::platform
