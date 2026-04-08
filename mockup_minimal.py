#!/usr/bin/env python3
"""Minimalist Flipper TV Remote UI mockup (portrait 64x128, 4x scale).

- D-pad cross, centered
- Ok = dot in center
- Back (left) and Power (right) at bottom
- 'Hold for alt' hint at bottom center
"""
from PIL import Image, ImageDraw, ImageFont

SCALE = 4
W, H = 64, 128
IMG_W, IMG_H = W * SCALE, H * SCALE
BG = (255, 255, 255)
FG = (0, 0, 0)

img = Image.new("RGB", (IMG_W, IMG_H), BG)
draw = ImageDraw.Draw(img)

try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 6 * SCALE)
    font_sm = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 4 * SCALE)
except OSError:
    font = ImageFont.load_default()
    font_sm = font

S = SCALE

# Center of d-pad
cx, cy = W // 2, H // 2 - 8

# Draw arrows
arrow_len = 14
arrow_w = 7
# Up
draw.polygon([(cx*S, (cy-arrow_len)*S), ((cx-arrow_w)*S, (cy-4)*S), ((cx+arrow_w)*S, (cy-4)*S)], outline=FG, fill=None)
# Down
draw.polygon([(cx*S, (cy+arrow_len)*S), ((cx-arrow_w)*S, (cy+4)*S), ((cx+arrow_w)*S, (cy+4)*S)], outline=FG, fill=None)
# Left
draw.polygon([((cx-arrow_len)*S, cy*S), ((cx-4)*S, (cy-arrow_w)*S), ((cx-4)*S, (cy+arrow_w)*S)], outline=FG, fill=None)
# Right
draw.polygon([((cx+arrow_len)*S, cy*S), ((cx+4)*S, (cy-arrow_w)*S), ((cx+4)*S, (cy+arrow_w)*S)], outline=FG, fill=None)

# Ok button (center dot)
draw.ellipse([(cx-4)*S, (cy-4)*S, (cx+4)*S, (cy+4)*S], outline=FG, width=2*S)

# Back (bottom left)
draw.text((4*S, (H-14)*S), "Back", font=font_sm, fill=FG)
# Power (bottom right)
pw = draw.textlength("Power", font=font_sm)
draw.text(((W-4)*S-pw, (H-14)*S), "Power", font=font_sm, fill=FG)

# Hold for alt hint (bottom center)
draw.text((W//2*S, (H-6)*S), "Hold for alt", font=font_sm, fill=FG, anchor="mm")

img.save("mockup_minimal.png")
print("Saved mockup_minimal.png")
