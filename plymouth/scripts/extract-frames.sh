#!/bin/bash
# Split the Fuuten sprite sheets into the frames Plymouth will show.
# The script plugin has no crop, and it refreshes at 50 Hz. 55 gear
# frames cover the 1.1 s draw, then 45 slogan frames cover the 0.9 s write.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
src="${FUUTEN_IMAGES:-/home/ahjolinna/Projects/fuuten/contents/splash/images}"
out="${root}/theme"
mkdir -p "$out"

extract() {
    local sheet="$1" frames="$2" cell_w="$3" cell_h="$4" count="$5" prefix="$6"
    local i src_i col row x y
    for ((i = 0; i < count; i++)); do
        src_i=$((i * (frames - 1) / (count - 1)))
        col=$((src_i % 10))
        row=$((src_i / 10))
        x=$((col * cell_w))
        y=$((row * cell_h))
        magick "$sheet" -crop "${cell_w}x${cell_h}+${x}+${y}" +repage -strip \
            "PNG32:${out}/${prefix}-${i}.png"
    done
}

extract "$src/animation.png" 100 400 300 55 gear
extract "$src/experiencefreedom.png" 72 400 175 45 text

echo "gear $(ls "$out"/gear-*.png | wc -l) text $(ls "$out"/text-*.png | wc -l)"
du -sh "$out"
