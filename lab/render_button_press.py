#!/usr/bin/env python3
"""Render a 52x16 TC002 button-press radial light-wave preview.

Usage:
  python lab/render_button_press.py
  python lab/render_button_press.py --button 2 --output lab/build/middle.gif

Outputs:
  lab/build/button_press.gif
  lab/build/button_press_x10.gif
"""

import argparse
import base64
from pathlib import Path

from PIL import Image, ImageDraw


W, H = 52, 16
BTN_WIDTH = 12
BTN_GAP = 1
BTN_HEIGHT = 16

BG = (0, 0, 0)
GUIDE = (14, 18, 24)
ACTIVE_GUIDE = (20, 55, 48)
SPARK = (255, 255, 238)
WAVE_CORE = (255, 248, 198)
WAVE_INNER = (85, 245, 255)
WAVE_OUTER = (26, 85, 220)
WAVE_GLOW = (6, 18, 55)

HERE = Path(__file__).resolve().parent
BUILD = HERE / "build"


def clamp(value, low, high):
    return max(low, min(high, value))


def blend(a, b, t):
    t = clamp(t, 0.0, 1.0)
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def add_color(base, color, strength):
    strength = clamp(strength, 0.0, 1.0)
    return tuple(clamp(int(base[i] + color[i] * strength), 0, 255) for i in range(3))


def draw_button_guide(draw, index, color):
    x = index * (BTN_WIDTH + BTN_GAP)
    draw.rectangle((x, H - 2, x + BTN_WIDTH - 1, H - 1), fill=color)


def radial_wave_color(x, y, radius, fade):
    origin_x = (W - 1) / 2.0
    origin_y = 0.0
    distance = ((x - origin_x) ** 2 + (y - origin_y) ** 2) ** 0.5
    delta = abs(distance - radius)

    if delta < 0.45:
        color = WAVE_CORE
        strength = 1.0
    elif delta < 1.45:
        color = WAVE_INNER
        strength = 0.78
    elif delta < 2.85:
        color = WAVE_OUTER
        strength = 0.46
    elif delta < 4.6:
        color = WAVE_GLOW
        strength = 0.25
    else:
        return BG

    return add_color(BG, color, strength * fade)


def render_frame(active_button, frame_index, total_frames):
    img = Image.new("RGB", (W, H), BG)
    draw = ImageDraw.Draw(img)

    for i in range(4):
        draw_button_guide(draw, i, ACTIVE_GUIDE if i == active_button else GUIDE)

    progress = frame_index / max(1, total_frames - 1)
    radius = progress * 31.5
    fade = 1.0 - progress * 0.36

    px = img.load()
    for y in range(H):
        for x in range(W):
            wave = radial_wave_color(x, y, radius, fade)
            if wave != BG:
                px[x, y] = add_color(px[x, y], wave, 1.0)

    spark_level = 1.0 if frame_index < 3 else max(0.0, 1.0 - (frame_index - 2) / 5.0)
    spark = add_color(BG, SPARK, spark_level)
    draw.point((W // 2 - 1, 0), fill=spark)
    draw.point((W // 2, 0), fill=spark)
    if frame_index < 5:
        draw.point((W // 2 - 1, 1), fill=blend(WAVE_INNER, SPARK, 0.4))
        draw.point((W // 2, 1), fill=blend(WAVE_INNER, SPARK, 0.4))

    return img


def save_gif(frames, output, scale):
    output.parent.mkdir(parents=True, exist_ok=True)
    if scale > 1:
        frames = [
            frame.resize((W * scale, H * scale), Image.Resampling.NEAREST)
            for frame in frames
        ]
    frames[0].save(
        output,
        save_all=True,
        append_images=frames[1:],
        loop=0,
        duration=70,
        disposal=2,
        optimize=False,
    )


def main():
    parser = argparse.ArgumentParser(description="Render TC002 radial light-wave GIFs.")
    parser.add_argument("--button", type=int, default=1, choices=range(4),
                        help="Button index to animate: 0 knob, 1 left, 2 middle, 3 right.")
    parser.add_argument("--frames", type=int, default=16,
                        help="Frame count for one press animation.")
    parser.add_argument("--scale", type=int, default=10,
                        help="Scale factor for the large preview GIF.")
    parser.add_argument("--output", default=str(BUILD / "button_press.gif"),
                        help="Path for the native 52x16 GIF.")
    args = parser.parse_args()

    frames = [render_frame(args.button, i, args.frames) for i in range(args.frames)]
    output = Path(args.output)
    large_output = output.with_name(output.stem + f"_x{args.scale}" + output.suffix)

    save_gif(frames, output, 1)
    save_gif(frames, large_output, args.scale)

    raw = output.read_bytes()
    b64 = "data:image/gif;base64," + base64.b64encode(raw).decode("ascii")
    (output.parent / "button_press_base64.txt").write_text(
        "button_press: " + b64 + "\n",
        encoding="ascii",
    )

    print(f"wrote {output}")
    print(f"wrote {large_output}")
    print(f"wrote {output.parent / 'button_press_base64.txt'}")


if __name__ == "__main__":
    main()
