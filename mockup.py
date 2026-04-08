#!/usr/bin/env python3
"""Generate a 4x-scale mockup of the Flipper TV Remote landscape d-pad UI.

The Flipper display is 128x64. With ViewOrientationVerticalFlip the canvas
becomes 64 wide x 128 tall (user holds the Flipper sideways, d-pad on right).

Run:  python3 mockup.py          (requires Pillow)
Output: mockup.png  (256x512 px, 4x scale)
"""

from PIL import Image, ImageDraw, ImageFont

SCALE = 4
W, H = 64, 128
IMG_W, IMG_H = W * SCALE, H * SCALE

# Colours (Flipper is monochrome: black on orange-ish, but we use black on white)
BG = (255, 255, 255)
FG = (0, 0, 0)
HIGHLIGHT = (0, 0, 0)
HIGHLIGHT_TEXT = (255, 255, 255)

img = Image.new("RGB", (IMG_W, IMG_H), BG)
draw = ImageDraw.Draw(img)

try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 5 * SCALE)
    font_sm = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 4 * SCALE)
except OSError:
    font = ImageFont.load_default()
    font_sm = font

S = SCALE  # shorthand


def rect(x, y, w, h, filled=False):
    x0, y0 = x * S, y * S
    x1, y1 = (x + w) * S, (y + h) * S
    if filled:
        draw.rounded_rectangle([x0, y0, x1, y1], radius=2 * S, fill=FG)
    else:
        draw.rounded_rectangle([x0, y0, x1, y1], radius=2 * S, outline=FG, width=S)


def text_c(x, y, label, f=None, fill=FG):
    """Draw text centered at (x, y) in display coords."""
    if f is None:
        f = font_sm
    draw.text((x * S, y * S), label, font=f, fill=fill, anchor="mm")


# ── Title bar ──
text_c(32, 5, "TV Remote", font)
draw.line([(0, 11 * S), (W * S, 11 * S)], fill=FG, width=S)

# ── Up button ──
BOX_W_FULL = 50
BOX_H = 12
UP_X = (W - BOX_W_FULL) // 2  # 7
UP_Y = 15
rect(UP_X, UP_Y, BOX_W_FULL, BOX_H)
text_c(32, UP_Y + BOX_H // 2, "Up")
text_c(32, UP_Y + BOX_H + 5, "Vol. Up [HOLD]", font_sm)

# ── Middle row: Left / Ok / Right ──
MID_Y = 38
BOX_W_SIDE = 18
BOX_W_MID = 20
GAP = 3
left_x = 1
ok_x = left_x + BOX_W_SIDE + GAP     # 22
right_x = ok_x + BOX_W_MID + GAP     # 45

rect(left_x, MID_Y, BOX_W_SIDE, BOX_H)
text_c(left_x + BOX_W_SIDE // 2, MID_Y + BOX_H // 2, "Left")

rect(ok_x, MID_Y, BOX_W_MID, BOX_H)
text_c(ok_x + BOX_W_MID // 2, MID_Y + BOX_H // 2, "Ok")

rect(right_x, MID_Y, BOX_W_SIDE, BOX_H)
text_c(right_x + BOX_W_SIDE // 2, MID_Y + BOX_H // 2, "Right")

# Hold labels under each
HOLD_Y = MID_Y + BOX_H + 4
text_c(left_x + BOX_W_SIDE // 2, HOLD_Y, "Ch Dn", font_sm)
text_c(left_x + BOX_W_SIDE // 2, HOLD_Y + 7, "[HOLD]", font_sm)
text_c(ok_x + BOX_W_MID // 2, HOLD_Y, "Home", font_sm)
text_c(ok_x + BOX_W_MID // 2, HOLD_Y + 7, "[HOLD]", font_sm)
text_c(right_x + BOX_W_SIDE // 2, HOLD_Y, "Ch Up", font_sm)
text_c(right_x + BOX_W_SIDE // 2, HOLD_Y + 7, "[HOLD]", font_sm)

# ── Down button ──
DN_Y = 68
rect(UP_X, DN_Y, BOX_W_FULL, BOX_H)
text_c(32, DN_Y + BOX_H // 2, "Down")
text_c(32, DN_Y + BOX_H + 5, "Vol. Dn [HOLD]", font_sm)

# ── Separator ──
draw.line([(0, 90 * S), (W * S, 90 * S)], fill=FG, width=S)

# ── Back button ──
BACK_Y = 94
BACK_W = 30
BACK_X = (W - BACK_W) // 2
rect(BACK_X, BACK_Y, BACK_W, BOX_H)
text_c(32, BACK_Y + BOX_H // 2, "Back")
text_c(32, BACK_Y + BOX_H + 5, "Hold: Exit", font_sm)
text_c(32, BACK_Y + BOX_H + 13, "2xTap: Power", font_sm)

# ── Save ──
out = "mockup.png"
img.save(out)
print(f"Saved {out}  ({IMG_W}x{IMG_H} px, {SCALE}x scale of {W}x{H})")
