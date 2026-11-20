set(CMAKE_HOST_SYSTEM "Android-31")
set(CMAKE_HOST_SYSTEM_NAME "Android")
set(CMAKE_HOST_SYSTEM_VERSION "31")
set(CMAKE_HOST_SYSTEM_PROCESSOR "aarch64")

set(_CMAKE_INPUT_TOOLCHAIN_FILE [==[cmake/toolchain-android-hostclang.cmake]==])
set(_CMAKE_SYSTEM_TOOLCHAIN_FILE "/data/data/com.termux/files/home/frida/droneview-menu/cmake/toolchain-android-hostclang.cmake")
include("${_CMAKE_SYSTEM_TOOLCHAIN_FILE}")

set(CMAKE_SYSTEM "Linux")
set(CMAKE_SYSTEM_NAME "Linux")
set(CMAKE_SYSTEM_VERSION "")
set(CMAKE_SYSTEM_PROCESSOR "aarch64")

set(CMAKE_CROSSCOMPILING "TRUE")

set(CMAKE_SYSTEM_LOADED 1)
