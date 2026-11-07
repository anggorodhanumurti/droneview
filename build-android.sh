#!/data/data/com.termux/files/usr/bin/bash
#
# Android arm64-v8a build using host clang + host lld + NDK sysroot.
#
#   ./build-android.sh            configure + build
#   ./build-android.sh --clean    wipe the build directory first
#   ./build-android.sh -j 4       override parallelism (default 2)
#
# Environment:
#   ANDROID_NDK   NDK root (default: ~/android-ndk-r28c)

set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR="${BUILD_DIR:-build-android}"
JOBS="${JOBS:-2}"
CLEAN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -j|--jobs) JOBS="${2:?missing value for $1}"; shift 2 ;;
        -j*) JOBS="${1#-j}"; shift ;;
        --clean) CLEAN=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

NDK="${ANDROID_NDK:-${ANDROID_NDK_HOME:-$HOME/android-ndk-r28c}}"
if [[ ! -f "$NDK/source.properties" ]]; then
    echo "error: Android NDK not found at '$NDK'" >&2
    exit 1
fi

if [[ "$CLEAN" == 1 ]]; then
    rm -rf "$BUILD_DIR"
fi

cmake -B "$BUILD_DIR" -S . \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-android-hostclang.cmake \
    -DANDROID_NDK="$NDK" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build "$BUILD_DIR" --target droneview -j"$JOBS"

if command -v patchelf >/dev/null 2>&1; then
    [[ -f "$BUILD_DIR/libcstatic.so" ]] && \
        patchelf --set-rpath '$ORIGIN' "$BUILD_DIR/libcstatic.so"
fi

echo "==> artifact"
printf '    %-55s %s\n' "$BUILD_DIR/libcstatic.so" "$(du -h "$BUILD_DIR/libcstatic.so" | cut -f1)"
echo "==> deploy libcstatic.so into lib/arm64-v8a/ (no libc++_shared.so needed)"
