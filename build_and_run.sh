#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
BUILD_TYPE="Release"
RUN=1
CLEAN=0
FRAMES=""

usage() {
    cat <<'USAGE'
usage: build_and_run.sh [options]

  --debug         debug build with validation layers
  --clean         delete build dir
  --no-run        build only
  --frames N      exit the demo after N frames
  --build-dir DIR use DIR instead of ./build
  -h, --help      get help
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug) BUILD_TYPE="Debug"; shift ;;
        --clean) CLEAN=1; shift ;;
        --no-run) RUN=0; shift ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown: $1" >&2; usage; exit 1 ;;
    esac
done

missing=0
for tool in cmake glslangValidator; do
    if ! command -v "$tool" &> /dev/null; then
        echo "error: $tool not found in PATH" >&2
        missing=1
    fi
done
if ! command -v c++ &> /dev/null && ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
    echo "error: no compiler found in PATH" >&2
    missing=1
fi
if [[ $missing -ne 0 ]]; then
    echo "deps are missing" >&2
    exit 1
fi

if [[ $CLEAN -eq 1 ]]; then
    echo "removing $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

if command -v nproc &> /dev/null; then
    JOBS="$(nproc)"
elif command -v sysctl &> /dev/null; then
    JOBS="$(sysctl -n hw.ncpu)"
else
    JOBS=4
fi

echo "configuring ($BUILD_TYPE)"
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "building with $JOBS jobs"
cmake --build "$BUILD_DIR" -j "$JOBS"

echo "build complete"

if [[ $RUN -eq 0 ]]; then
    exit 0
fi

if [[ "$OSTYPE" == "darwin"* ]]; then
    BREW_PREFIX="$(brew --prefix 2>/dev/null || true)"
    if [[ -z "$BREW_PREFIX" ]]; then
        for candidate in /opt/homebrew /usr/local; do
            if [[ -d "$candidate/Cellar" ]]; then
                BREW_PREFIX="$candidate"
                break
            fi
        done
    fi

    if [[ -n "$BREW_PREFIX" ]]; then
        for candidate in \
            "$BREW_PREFIX/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json" \
            "$BREW_PREFIX/etc/vulkan/icd.d/MoltenVK_icd.json" \
            "$BREW_PREFIX/opt/molten-vk/share/vulkan/icd.d/MoltenVK_icd.json" \
            "$BREW_PREFIX/share/vulkan/icd.d/MoltenVK_icd.json"
        do
            if [[ -f "$candidate" ]]; then
                export VK_ICD_FILENAMES="$candidate"
                break
            fi
        done

        if [[ -d "$BREW_PREFIX/lib" ]]; then
            export DYLD_FALLBACK_LIBRARY_PATH="$BREW_PREFIX/lib:${DYLD_FALLBACK_LIBRARY_PATH:-}"
        fi

        for candidate in \
            "$BREW_PREFIX/share/vulkan/explicit_layer.d" \
            "$BREW_PREFIX/etc/vulkan/explicit_layer.d"
        do
            if [[ -d "$candidate" ]]; then
                export VK_LAYER_PATH="$candidate"
                break
            fi
        done
    fi

    if [[ -z "${VK_ICD_FILENAMES:-}" ]]; then
        echo "warning: MoltenVK_icd.json not found; vkCreateInstance will fail" >&2
    fi
fi

if [[ ! -x "$BUILD_DIR/xrender_demo" ]]; then
    echo "error: $BUILD_DIR/xrender_demo was not created" >&2
    exit 1
fi

echo "running demo"
cd "$BUILD_DIR"

if [[ -n "$FRAMES" ]]; then
    ./xrender_demo --frames "$FRAMES"
else
    ./xrender_demo
fi
