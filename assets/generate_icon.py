#!/usr/bin/env python3
"""Generate crisp multi-resolution brand icons (reclaim.ico and reclaim_256.png) for Reclaim."""

import os
from PIL import Image, ImageDraw


def draw_reclaim_icon(size: int) -> Image.Image:
    # Supersample 4x for razor-sharp anti-aliased vector geometry
    scale = 4
    s = size * scale
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    pad = int(s * 0.06)
    radius = int(s * 0.20)

    # Carbon obsidian rounded square base (#0D1117) with subtle steel border (#30363D)
    draw.rounded_rectangle(
        [pad, pad, s - pad, s - pad],
        radius=radius,
        fill=(13, 17, 23, 255),
        outline=(48, 54, 61, 255),
        width=max(2, int(s * 0.025)),
    )

    # Geometric treemap / reclaimed-blocks motif inside safe margin
    m = int(s * 0.21)
    gap = max(2, int(s * 0.045))
    inner = s - 2 * m
    r_block = max(2, int(s * 0.05))

    # Left tall block: Signal Amber (#D97706) representing reclaimed space
    left_w = int(inner * 0.50)
    draw.rounded_rectangle(
        [m, m, m + left_w, m + inner],
        radius=r_block,
        fill=(217, 119, 6, 255),
    )

    # Subtle bright amber highlight bar inside left block
    hl_pad = max(2, int(s * 0.035))
    draw.rounded_rectangle(
        [m + hl_pad, m + hl_pad, m + left_w - hl_pad, m + int(inner * 0.24)],
        radius=max(1, r_block // 2),
        fill=(245, 158, 11, 255),
    )

    # Right top block: Cobalt Blue (#4C9AFF)
    rx = m + left_w + gap
    right_w = inner - left_w - gap
    top_h = int(inner * 0.44)
    draw.rounded_rectangle(
        [rx, m, rx + right_w, m + top_h],
        radius=r_block,
        fill=(76, 154, 255, 255),
    )

    # Right bottom-left block: Cyan Teal (#39C5CF)
    by = m + top_h + gap
    bot_h = inner - top_h - gap
    sub_w = (right_w - gap) // 2
    draw.rounded_rectangle(
        [rx, by, rx + sub_w, by + bot_h],
        radius=r_block,
        fill=(57, 197, 207, 255),
    )

    # Right bottom-right block: Emerald (#56D364)
    draw.rounded_rectangle(
        [rx + sub_w + gap, by, rx + right_w, by + bot_h],
        radius=r_block,
        fill=(86, 211, 100, 255),
    )

    return img.resize((size, size), Image.Resampling.LANCZOS)


def main() -> None:
    out_dir = os.path.dirname(os.path.abspath(__file__))
    img_256 = draw_reclaim_icon(256)
    png_path = os.path.join(out_dir, "reclaim_256.png")
    ico_path = os.path.join(out_dir, "reclaim.ico")

    img_256.save(png_path, format="PNG")
    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    img_256.save(ico_path, format="ICO", sizes=sizes)
    print(f"Generated {png_path} and {ico_path}")


if __name__ == "__main__":
    main()
