#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${1:-$SCRIPT_DIR/build}"
shift || true

SEARCH_DIRS=("$@")
if [[ ${#SEARCH_DIRS[@]} -eq 0 ]]; then
    SEARCH_DIRS=("$SCRIPT_DIR/shaders" "$SCRIPT_DIR/examples")
fi

if ! command -v glslangValidator &> /dev/null; then
    echo "glslangValidator not found" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

count=0
for dir in "${SEARCH_DIRS[@]}"; do
    [[ -d "$dir" ]] || continue
    while IFS= read -r -d '' source; do
        base="$(basename "$source")"
        name="${base%.*}"
        stage="${base##*.}"
        glslangValidator -V "$source" -o "$OUTPUT_DIR/${name}_${stage}.spv" --target-env vulkan1.2 > /dev/null
        count=$((count + 1))
    done < <(find "$dir" -type f \( -name '*.vert' -o -name '*.frag' -o -name '*.comp' -o -name '*.geom' -o -name '*.tesc' -o -name '*.tese' \) -print0)
done

echo "compiled $count shaders to $OUTPUT_DIR"
