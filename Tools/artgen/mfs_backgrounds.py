"""Menu pictures from Mobile Forces Source (Edgaras "Gregaras" Ziaugra), whole frame and team
badge kept; only the engine logo in the corner changes: "source" (Valve's) becomes "unreal".

    python3 Tools/artgen/mfs_backgrounds.py <Mobile-Forces-Source checkout>

reads mp/game/mobileforcessource/materials/console/background0{1,2}.vtf (DXT5) and writes
SourceArt/UI/T_MenuBackground{2,1}.png. Needs Pillow, numpy, OpenCV (cv2.inpaint).
"""
from __future__ import annotations

import io
import struct
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'SourceArt' / 'UI'
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'

# picture -> (output, the logo's box (x0, y0, x1, y1), white face?)
PICTURES = {
    'background02.vtf': ('T_MenuBackground1.png', (775, 841, 998, 963), True),
    'background01.vtf': ('T_MenuBackground2.png', (787, 429, 991, 486), False),
}


def read_vtf(path: Path) -> Image.Image:
    """VTF 7.2, one mip, DXT1/3/5: the high-res image is the last block of the file."""
    b = path.read_bytes()
    w, h = struct.unpack_from('<HH', b, 16)
    fmt = struct.unpack_from('<i', b, 52)[0]
    fourcc = {13: b'DXT1', 14: b'DXT3', 15: b'DXT5'}[fmt]
    size = w * h // (2 if fmt == 13 else 1)
    data = b[len(b) - size:]
    hdr = struct.pack('<4sIIIIIII44s', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000, h, w, size, 0, 1, b'\0' * 44)
    hdr += struct.pack('<II4sIIIII', 32, 0x4, fourcc, 0, 0, 0, 0, 0)
    hdr += struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
    return Image.open(io.BytesIO(hdr + data)).convert('RGB')


def replace_logo(img: Image.Image, box: tuple[int, int, int, int], white: bool) -> Image.Image:
    a = np.asarray(img).astype(np.int32)
    x0, y0, x1, y1 = box
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    # the orange mark reaches out of the box: paint it out around the box (smooth frame glow there)
    near = np.zeros(r.shape, bool)
    near[y0 - 30:y1 + 30, x0 - 30:x1 + 30] = True
    near[y0:y1, x0:x1] = False
    orange = near & (r > 90) & (r - b > 35) & (r > g)
    mask = cv2.dilate(orange.astype(np.uint8) * 255, np.ones((3, 3), np.uint8), iterations=2)
    out = Image.fromarray(cv2.inpaint(np.asarray(img)[:, :, ::-1].copy(), mask, 6, cv2.INPAINT_TELEA)[:, :, ::-1])
    # a fresh box in the old one's style: bevelled white, or plain black with a thin rim
    draw = ImageDraw.Draw(out)
    if white:
        draw.rectangle(box, fill=(96, 96, 96))
        bevel = 8
        draw.polygon([(x0 + 2, y0 + 2), (x1 - 2, y0 + 2), (x1 - 2 - bevel, y0 + 2 + bevel), (x0 + 2 + bevel, y0 + 2 + bevel),
                      (x0 + 2 + bevel, y1 - 2 - bevel), (x0 + 2, y1 - 2)], fill=(214, 214, 214))
        draw.polygon([(x1 - 2, y0 + 2), (x1 - 2, y1 - 2), (x0 + 2, y1 - 2), (x0 + 2 + bevel, y1 - 2 - bevel),
                      (x1 - 2 - bevel, y1 - 2 - bevel), (x1 - 2 - bevel, y0 + 2 + bevel)], fill=(128, 128, 128))
        face = (x0 + 2 + bevel, y0 + 2 + bevel, x1 - 2 - bevel, y1 - 2 - bevel)
        draw.rectangle(face, fill=(250, 250, 250))
        ink = (106, 106, 106)
    else:
        draw.rectangle(box, fill=(4, 4, 4), outline=(46, 46, 46))
        face = (x0 + 3, y0 + 3, x1 - 3, y1 - 3)
        ink = (70, 70, 70)
    pad = 6
    area = (face[0] + pad, face[1] + pad, face[2] - pad, face[3] - pad)
    size = 8
    font = ImageFont.truetype(FONT, size)
    while True:
        nxt = ImageFont.truetype(FONT, size + 1)
        l, t, rr, bb = draw.textbbox((0, 0), 'unreal', font=nxt)
        if rr - l > area[2] - area[0] or bb - t > area[3] - area[1]:
            break
        size, font = size + 1, nxt
    l, t, rr, bb = draw.textbbox((0, 0), 'unreal', font=font)
    draw.text((area[0] + (area[2] - area[0] - (rr - l)) // 2 - l, area[1] + (area[3] - area[1] - (bb - t)) // 2 - t),
              'unreal', font=font, fill=ink)
    return out


def main(argv: list[str]) -> None:
    src = Path(argv[0]) / 'mp/game/mobileforcessource/materials/console'
    OUT.mkdir(parents=True, exist_ok=True)
    for vtf, (name, box, white) in PICTURES.items():
        img = replace_logo(read_vtf(src / vtf), box, white)
        img.save(OUT / name, optimize=True)
        print(f'{vtf} -> {OUT / name} {img.size}')


if __name__ == '__main__':
    main(sys.argv[1:] or [str(ROOT.parent / 'MoFo' / 'Mobile-Forces-Source')])
