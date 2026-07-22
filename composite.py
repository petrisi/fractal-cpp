#!/usr/bin/env python3
"""Composite 14 fractal showcase images into a 4×3 grid with labels."""

from PIL import Image, ImageDraw, ImageFont
import sys, os

CELL_W, CELL_H = 800, 450
LABEL_H = 36
MARGIN = 4
COLS = 4
ROWS = 4  # 14 images → 3 full rows + 1 row with 2 cells

# Fractal names in grid order (4 per row)
FRACALS = [
    ("mandelbrot",     "Mandelbrot"),
    ("julia",          "Julia"),
    ("burnship",       "Burnship"),
    ("tricorn",        "Tricorn"),
    ("multibrot",      "Multibrot (d=5)"),
    ("newton",         "Newton (d=3)"),
    ("phoenix",        "Phoenix"),
    ("magnet",         "Magnet 1"),
    ("barnsley",       "Barnsley"),
    ("nova",           "Nova"),
    ("transcendental", "Transcendental (sin)"),
    ("rational",       "Rational"),
    ("clifford",       "Clifford"),
    ("lyapunov",       "Lyapunov"),
]

BG = (20, 20, 25)
LABEL_BG = (30, 30, 40)
LABEL_FG = (220, 220, 230)
BORDER = (60, 60, 80)

showcase_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "showcase")

# Load all images
images = []
for name, _ in FRACALS:
    path = os.path.join(showcase_dir, f"{name}.png")
    img = Image.open(path).convert("RGB")
    images.append(img)

# Try to find a reasonable font
font_paths = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
]
font = None
for fp in font_paths:
    if os.path.exists(fp):
        font = ImageFont.truetype(fp, 22)
        break
if font is None:
    font = ImageFont.load_default()

# Build composite
total_w = COLS * CELL_W + (COLS + 1) * MARGIN
total_h = ROWS * (CELL_H + LABEL_H) + (ROWS + 1) * MARGIN

composite = Image.new("RGB", (total_w, total_h), BG)

for idx, (img, (_, label)) in enumerate(zip(images, FRACALS)):
    row = idx // COLS
    col = idx % COLS
    x = MARGIN + col * (CELL_W + MARGIN)
    y = MARGIN + row * (CELL_H + LABEL_H + MARGIN)

    # Draw image
    composite.paste(img, (x, y))

    # Draw label bar
    ly = y + CELL_H + MARGIN
    draw = ImageDraw.Draw(composite)
    draw.rectangle([x, ly, x + CELL_W - 1, ly + LABEL_H - 1], fill=LABEL_BG, outline=BORDER)

    # Center the label text
    try:
        bbox = draw.textbbox((0, 0), label, font=font)
        tw = bbox[2] - bbox[0]
        tx = x + (CELL_W - tw) // 2
        ty = ly + (LABEL_H - (bbox[3] - bbox[1])) // 2
    except Exception:
        tx = x + CELL_W // 2
        ty = ly + LABEL_H // 2
    draw.text((tx, ty), label, fill=LABEL_FG, font=font)

# Fill remaining empty cells with dark panels
for idx in range(len(FRACALS), COLS * ROWS):
    row = idx // COLS
    col = idx % COLS
    x = MARGIN + col * (CELL_W + MARGIN)
    y = MARGIN + row * (CELL_H + LABEL_H + MARGIN)
    draw = ImageDraw.Draw(composite)
    draw.rectangle([x, y, x + CELL_W - 1, y + CELL_H - 1], fill=(15, 15, 20), outline=BORDER)
    ly = y + CELL_H + MARGIN
    draw.rectangle([x, ly, x + CELL_W - 1, ly + LABEL_H - 1], fill=(15, 15, 20), outline=BORDER)

out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "showcase", "fractal-showcase.png")
composite.save(out_path, "PNG")
print(f"Wrote {out_path} ({composite.size[0]}x{composite.size[1]})")
