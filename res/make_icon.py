"""Generate EchoPad's application icon (res/echopad.ico).

Rendered at 4x and downsampled so the small sizes stay clean. Only needs
Pillow + numpy, which ship with the DSH runtime.
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "echopad.ico")

SUPER = 4
SIZE = 256
S = SIZE * SUPER

# ---------------------------------------------------------------- background
top = np.array([0x2F, 0x6F, 0xED], dtype=np.float64)     # blue
bottom = np.array([0x7A, 0x3F, 0xE4], dtype=np.float64)  # violet

ramp = np.linspace(0.0, 1.0, S, dtype=np.float64)[:, None]
gradient = (top[None, :] * (1.0 - ramp) + bottom[None, :] * ramp)
gradient = np.repeat(gradient[:, None, :], S, axis=1)

background = Image.fromarray(gradient.astype(np.uint8), mode="RGB").convert("RGBA")

# Rounded-corner mask.
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.22), fill=255)
background.putalpha(mask)

# --------------------------------------------------------------------- glyph
glyph = Image.new("L", (S, S), 0)
draw = ImageDraw.Draw(glyph)

# Play triangle.
cx, cy = 0.40 * S, 0.50 * S
half = 0.19 * S
triangle = [(cx - half * 0.85, cy - half), (cx - half * 0.85, cy + half), (cx + half * 0.95, cy)]
draw.polygon(triangle, fill=255)

# Two sound arcs to the right of the triangle.
arc_cx, arc_cy = 0.52 * S, 0.50 * S
stroke = int(0.048 * S)
for radius in (0.145 * S, 0.235 * S):
    draw.arc(
        [arc_cx - radius, arc_cy - radius, arc_cx + radius, arc_cy + radius],
        start=-58,
        end=58,
        fill=255,
        width=stroke,
    )

foreground = Image.new("RGBA", (S, S), (255, 255, 255, 0))
foreground.putalpha(glyph)

composed = Image.alpha_composite(background, foreground)
icon = composed.resize((SIZE, SIZE), Image.LANCZOS)

icon.save(
    OUT,
    format="ICO",
    sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)],
)
print("wrote", OUT, os.path.getsize(OUT), "bytes")
