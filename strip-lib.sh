#!/data/data/com.termux/files/usr/bin/bash
#
# Strip an already-built libcstatic.so without touching its behavior or UI.
#
#   ./strip-lib.sh                          # strips build-android/libcstatic.so
#   ./strip-lib.sh path/to/libcstatic.so    # strips a specific file
#
# Only .debug_*, .symtab and .strtab are removed. Loadable sections, .dynsym
# and the exported entry points (JNI_OnLoad, dv_start, dv_stop) are preserved.

set -euo pipefail
cd "$(dirname "$0")"

LIB="${1:-build-android/libcstatic.so}"

if [[ ! -f "$LIB" ]]; then
    echo "error: '$LIB' not found" >&2
    exit 1
fi

STRIP_BIN="$(command -v llvm-strip || command -v strip || true)"
if [[ -z "$STRIP_BIN" ]]; then
    echo "error: neither llvm-strip nor strip is available" >&2
    exit 1
fi

before="$(stat -c %s "$LIB")"
"$STRIP_BIN" --strip-unneeded "$LIB"
after="$(stat -c %s "$LIB")"

if command -v readelf >/dev/null 2>&1; then
    missing=0
    for symbol in JNI_OnLoad dv_start dv_stop; do
        if ! readelf --dyn-syms "$LIB" | grep -q " $symbol\$"; then
            echo "error: exported symbol '$symbol' missing after strip" >&2
            missing=1
        fi
    done
    if [[ "$missing" -ne 0 ]]; then
        exit 1
    fi
fi

printf 'stripped %s: %s -> %s bytes\n' "$LIB" "$before" "$after"
