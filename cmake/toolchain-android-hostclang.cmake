# Alternative Android toolchain for hosts that cannot execute the NDK's
# x86_64 clang (for example an aarch64 Linux machine without binfmt).
#
# It uses the host clang with the NDK sysroot and the host LLD.
#
# Requirements:
#   - clang / clang++ (native host build)
#   - ld.lld (native host build, e.g. Termux lld package)
#   - Android NDK (for sysroot and Android libraries)
#
# Usage:
#   cmake -B build-android -S . \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-android-hostclang.cmake

set(APP_TARGET_ANDROID TRUE)

if(NOT DEFINED ANDROID_NDK)
  if(DEFINED ENV{ANDROID_NDK_HOME})
    set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
  elseif(EXISTS "$ENV{HOME}/android-ndk-r28c")
    set(ANDROID_NDK "$ENV{HOME}/android-ndk-r28c")
  else()
    message(FATAL_ERROR "ANDROID_NDK not found")
  endif()
endif()

set(ANDROID_ABI "arm64-v8a" CACHE STRING "Android ABI")
set(ANDROID_PLATFORM "android-23" CACHE STRING "Android API level")

# Use a Linux system name so CMake does not try to execute the NDK's x86_64
# compiler. APP_TARGET_ANDROID below selects the Android branches in our
# CMakeLists.txt; the target triple and sysroot do the actual cross-compile.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_SYSROOT "${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/sysroot")
set(CMAKE_C_COMPILER /data/data/com.termux/files/usr/bin/clang)
set(CMAKE_CXX_COMPILER /data/data/com.termux/files/usr/bin/clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-android23)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-android23)

set(NDK_CLANG_RESOURCE "${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/19")
set(NDK_TARGET_FLAGS "--target=aarch64-linux-android23 --sysroot=${CMAKE_SYSROOT}")
set(NDK_COMMON_FLAGS "${NDK_TARGET_FLAGS} --rtlib=compiler-rt -stdlib=libc++ -resource-dir=${NDK_CLANG_RESOURCE}")
set(NDK_LINK_FLAGS "${NDK_COMMON_FLAGS} -fuse-ld=/data/data/com.termux/files/usr/bin/ld.lld")
set(APP_NDK_UNWIND "${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/19/lib/linux/aarch64/libunwind.a")

set(CMAKE_C_FLAGS_INIT "${NDK_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${NDK_COMMON_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${NDK_LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${NDK_LINK_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${NDK_LINK_FLAGS}")

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

message(STATUS "[host-clang-android] NDK: ${ANDROID_NDK}")
message(STATUS "[host-clang-android] target: aarch64-linux-android23")
message(STATUS "[host-clang-android] lld: /data/data/com.termux/files/usr/bin/ld.lld")
