#!/bin/bash
# Still frames of the same placement the Plymouth script uses.
# Usage: preview.sh WIDTH HEIGHT GEAR_INDEX TEXT_INDEX OUT [prompt]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
theme="${root}/theme"
sw="$1"
sh="$2"
gear_i="$3"
text_i="$4"
out="$5"
prompt="${6:-}"

python3 - "$sw" "$sh" "$gear_i" "$text_i" "$out" "$prompt" "$theme" << 'PY'
import subprocess, sys
sw, sh = int(sys.argv[1]), int(sys.argv[2])
gear_i, text_i = int(sys.argv[3]), int(sys.argv[4])
out, prompt, theme = sys.argv[5], sys.argv[6], sys.argv[7]
scale = min(sw * 0.86 / 700, sh * 0.42 / 338)
scale = max(scale, 0.05)
gw, gh = max(1, int(400 * scale)), max(1, int(300 * scale))
tw, th = max(1, int(400 * scale)), max(1, int(175 * scale))
ox = (sw - 700 * scale) / 2
oy = (sh - 338 * scale) / 2
tx = ox + 300 * scale
ty = oy + 163 * scale
cmd = [
    "magick", "-size", f"{sw}x{sh}", "xc:black",
    "(", f"{theme}/gear-{gear_i}.png", "-filter", "Lanczos", "-resize", f"{gw}x{gh}!", ")",
    "-geometry", f"+{round(ox)}+{round(oy)}", "-composite",
]
if text_i >= 0:
    cmd += [
        "(", f"{theme}/text-{text_i}.png", "-filter", "Lanczos", "-resize", f"{tw}x{th}!", ")",
        "-geometry", f"+{round(tx)}+{round(ty)}", "-composite",
    ]
if prompt:
    font = max(16, min(48, int(18 * scale)))
    # Approximate the pango label the script draws, then sit it on the slogan.
    label = subprocess.check_output([
        "magick", "-background", "none", "-fill", "white", "-font", "DejaVu-Sans",
        "-pointsize", str(font), f"label:{prompt}", "-format", "%w %h", "info:"
    ], text=True).split()
    pw, ph = int(label[0]), int(label[1])
    stars = "* * * *"
    slabel = subprocess.check_output([
        "magick", "-background", "none", "-fill", "white", "-font", "DejaVu-Sans",
        "-pointsize", str(font), f"label:{stars}", "-format", "%w %h", "info:"
    ], text=True).split()
    ew, eh = int(slabel[0]), int(slabel[1])
    gap = 8 * scale
    bottom = ty - 16 * scale
    top = bottom - ph - gap - eh
    if top < 12:
        top = 12
    center = tx + tw / 2
    cmd += [
        "(", "-background", "none", "-fill", "white", "-font", "DejaVu-Sans",
        "-pointsize", str(font), f"label:{prompt}", ")",
        "-geometry", f"+{round(center - pw / 2)}+{round(top)}", "-composite",
        "(", "-background", "none", "-fill", "white", "-font", "DejaVu-Sans",
        "-pointsize", str(font), f"label:{stars}", ")",
        "-geometry", f"+{round(center - ew / 2)}+{round(top + ph + gap)}", "-composite",
    ]
cmd.append(out)
subprocess.check_call(cmd)
print(f"{out} scale={scale:.3f} gear={gw}x{gh}@{round(ox)},{round(oy)} text={tw}x{th}@{round(tx)},{round(ty)}")
PY
