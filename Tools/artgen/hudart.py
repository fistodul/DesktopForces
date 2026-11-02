#!/usr/bin/env python3
"""HUD art of the game's own, so the core game has a full HUD without a Mobile Forces install.

Same atlas layouts as the original HUD (RageHUD's texture coordinates, in DFHUD.cpp), so the
drawing code doesn't care which set it got; a converted install's originals win
(DF::LoadClassicObject falls back to /Game/MF/UI/HUD). Everything here is drawn from shapes in
this file, nothing from the game's pixels. MPL-2.0 like the code.

    python3 Tools/artgen/hudart.py         ->  Tools/artgen/out/UI/HUD/*.png
    python3 Tools/mfport/mfport.py import --steps hud
"""
from __future__ import annotations

import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parent / 'out' / 'UI' / 'HUD'
SS = 4     # drawn this much bigger, then reduced: smooth edges

RED, BLUE, WHITE, GREY = (205, 40, 40), (55, 70, 235), (235, 235, 240), (150, 150, 155)


class Sheet:
    """A transparent RGBA picture drawn at SS times its size; coordinates are final pixels."""

    def __init__(self, w: int, h: int, fill=(0, 0, 0, 0)):
        self.size = (w, h)
        self.img = Image.new('RGBA', (w * SS, h * SS), fill)
        self.d = ImageDraw.Draw(self.img)

    def poly(self, pts, fill):
        self.d.polygon([(x * SS, y * SS) for x, y in pts], fill=fill)

    def rrect(self, x0, y0, x1, y1, r, fill=None, outline=None, width=1.0):
        self.d.rounded_rectangle((x0 * SS, y0 * SS, x1 * SS - 1, y1 * SS - 1), r * SS, fill=fill, outline=outline, width=int(width * SS))

    def circle(self, cx, cy, r, fill=None, outline=None, width=1.0):
        self.d.ellipse(((cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS), fill=fill, outline=outline, width=int(width * SS))

    def line(self, pts, fill, width=1.0):
        self.d.line([(x * SS, y * SS) for x, y in pts], fill=fill, width=int(width * SS), joint='curve')

    def done(self) -> Image.Image:
        return self.img.resize(self.size, Image.LANCZOS)


def alpha_mask(paint, w: int, h: int) -> np.ndarray:
    """What paint(sheet) covers, 0..1."""
    s = Sheet(w, h)
    paint(s)
    return np.asarray(s.done().getchannel('A'), np.float32) / 255


# --- shapes -----------------------------------------------------------------------------------

SOLDIER = [(32, 2), (39, 5), (41, 13), (38, 20), (47, 25), (52, 40), (55, 64), (50, 66), (45, 44), (44, 66),
           (47, 92), (48, 122), (37, 124), (34, 92), (32, 76), (30, 92), (27, 124), (16, 122), (17, 92), (20, 66),
           (19, 44), (14, 66), (9, 64), (12, 40), (17, 25), (26, 20), (23, 13), (25, 5)]
VEST = [(16, 4), (26, 10), (38, 10), (48, 4), (56, 16), (52, 28), (52, 52), (12, 52), (12, 28), (8, 16)]


def scaled(pts, sx, sy, ox=0.0, oy=0.0):
    return [(x * sx + ox, y * sy + oy) for x, y in pts]


def emblem(s: Sheet, cx, cy, r, team: int, fill):
    """Team marks: three chevrons on a shield (red), a star on a shield (blue)."""
    shield = [(cx - r, cy - r * 0.8), (cx + r, cy - r * 0.8), (cx + r * 0.9, cy + r * 0.2), (cx, cy + r), (cx - r * 0.9, cy + r * 0.2)]
    s.poly(shield, fill)
    hole = (0, 0, 0, 0)
    if team == 1:
        for i in range(3):
            y = cy - r * 0.55 + i * r * 0.42
            s.line([(cx - r * 0.62, y), (cx, y + r * 0.3), (cx + r * 0.62, y)], hole, max(1.0, r * 0.14))
    else:
        pts = []
        for i in range(10):
            a = -math.pi / 2 + i * math.pi / 5
            q = r * (0.68 if i % 2 == 0 else 0.28)
            pts.append((cx + q * math.cos(a), cy - r * 0.08 + q * math.sin(a)))
        s.poly(pts, hole)


def buggy(s: Sheet, x, y, w, h, fill):
    s.poly([(x, y + h * 0.55), (x + w * 0.2, y + h * 0.5), (x + w * 0.35, y + h * 0.1), (x + w * 0.6, y + h * 0.1), (x + w * 0.75, y + h * 0.5),
            (x + w, y + h * 0.55), (x + w, y + h * 0.8), (x, y + h * 0.8)], fill)
    s.circle(x + w * 0.22, y + h * 0.8, h * 0.22, fill)
    s.circle(x + w * 0.78, y + h * 0.8, h * 0.22, fill)


def arrow(s: Sheet, x, y, w, h, fill, left=False):
    pts = [(0, 0.3), (0.5, 0.3), (0.5, 0), (1, 0.5), (0.5, 1), (0.5, 0.7), (0, 0.7)]
    s.poly([(x + (1 - a if left else a) * w, y + b * h) for a, b in pts], fill)


# --- atlases ----------------------------------------------------------------------------------

def boxes() -> Image.Image:
    # columns 0-191 left / middle / right of a wide item, 192-255 a one-block one; rows white,
    # red, blue, grey (BoxRowY), the frame 48 high and the ammo bar's 16 under it
    s = Sheet(256, 256)
    for y, c in zip((0, 64, 128, 192), (WHITE, RED, BLUE, GREY)):
        pale = tuple(min(255, v + 90) for v in c)
        for x0, x1 in ((1, 191), (193, 255)):
            s.rrect(x0, y + 2, x1, y + 46, 7, outline=(*c, 255), width=2.5)
            s.rrect(x0 + 2.5, y + 4.5, x1 - 2.5, y + 43.5, 5, outline=(*pale, 200), width=1)
            s.rrect(x0, y + 48, x1, y + 62, 6, outline=(*c, 255), width=2)
            if c != GREY:                       # free blocks have no key to show
                s.circle(x0 + 9, y + 11, 8, (0, 0, 0, 255))
    return s.done()


def backs() -> Image.Image:
    # grey shapes the HUD tints (added): item panels, ammo bar, health soldier, vest, the panels
    # behind the score and "loadout here" icons
    s = Sheet(256, 256, (0, 0, 0, 255))
    g = (90, 90, 90, 255)
    s.rrect(2, 3, 190, 46, 6, g)
    s.rrect(194, 3, 254, 46, 6, g)
    s.rrect(2, 48, 190, 62, 6, (255, 255, 255, 255))
    s.rrect(194, 48, 254, 62, 6, (255, 255, 255, 255))
    s.poly(scaled(SOLDIER, 1, 1, 0, 64), g)
    s.poly(scaled(VEST, 1, 1, 0, 192), g)
    buggy(s, 66, 66, 34, 22, g)
    arrow(s, 106, 70, 22, 16, g, True)
    emblem(s, 84, 112, 12, 1, g)
    arrow(s, 106, 104, 22, 16, g)
    s.rrect(68, 132, 124, 192, 6, g)
    s.rrect(68, 196, 124, 226, 6, g)
    return s.done()


def icons() -> Image.Image:
    s = Sheet(256, 256)
    for team, x, c in ((1, 0, RED), (0, 64, BLUE)):
        s.rrect(x + 2, 2, x + 62, 68, 7, (235, 235, 240, 235))
        s.rrect(x + 2, 2, x + 62, 68, 7, outline=(*c, 255), width=3)
        emblem(s, x + 32, 34, 22, team, (*c, 255))
        s.rrect(x + 2, 70, x + 62, 98, 7, outline=(*c, 255), width=3)
        s.poly(scaled(SOLDIER, 1, 1, 128 if team == 1 else 192, 0), (*c, 255))
        s.poly(scaled(SOLDIER, 0.76, 0.84, (128 if team == 1 else 192) + 7.5, 10), (0, 0, 0, 0))
        s.poly(scaled(VEST, 1, 1, 128 if team == 1 else 192, 128), (*c, 255))
        s.poly(scaled(VEST, 0.76, 0.76, (128 if team == 1 else 192) + 8, 134), (0, 0, 0, 0))
        buggy(s, x + 4, 130, 30, 20, (*c, 255))
        arrow(s, x + 38, 133, 22, 16, (*c, 255), True)
        s.circle(x + 16, 176, 14, (235, 235, 240, 235), (*c, 255), 2)
        emblem(s, x + 16, 176, 9, team, (*c, 255))
        arrow(s, x + 36, 168, 24, 16, (*c, 255))
    return s.done()


def loadout_back(team: int) -> Image.Image:
    # added over the scene behind the loadout: a soft, dim team mark
    s = Sheet(256, 256, (0, 0, 0, 255))
    c = BLUE if team == 0 else RED
    dim = tuple(v // 7 for v in c)
    emblem(s, 128, 128, 104, team, (*dim, 255))
    img = s.done().filter(ImageFilter.GaussianBlur(5))
    img.putalpha(255)
    return img


def pain(team: int) -> Image.Image:
    # one quarter (the HUD mirrors it): white-hot in the bottom left corner, team colour around it
    y, x = np.mgrid[0:256, 0:256].astype(np.float32)
    d = np.hypot(x, 255 - y) / 255
    glow = np.clip(1 - d, 0, 1) ** 2.2
    hot = np.clip(1 - d * 4.5, 0, 1) ** 2
    c = np.array(BLUE if team == 0 else RED, np.float32)
    rgb = glow[..., None] * c * 1.1 + hot[..., None] * 255
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), 'RGB').convert('RGBA')


def score_boxes() -> Image.Image:
    s = Sheet(128, 128)
    for y, c in zip((0, 32, 64), (RED, WHITE, BLUE)):
        pale = tuple(min(255, v + 110) for v in c)
        s.rrect(0, y + 2, 128, y + 30, 8, (*pale, 255))
        s.rrect(96, y + 2, 128, y + 30, 8, (*c, 255))
        s.rrect(0, y + 2, 128, y + 30, 8, outline=(10, 10, 20, 255), width=2)
    s.rrect(4, 100, 96, 124, 3, (255, 255, 255, 255))
    return s.done()


def safe(s: Sheet, x, y, w, h, c):
    dark = tuple(v // 2 for v in c)
    s.rrect(x, y, x + w, y + h, h * 0.08, (*c, 255))
    s.rrect(x + w * 0.14, y + h * 0.14, x + w * 0.86, y + h * 0.86, h * 0.05, (*dark, 255))
    s.circle(x + w * 0.5, y + h * 0.5, h * 0.2, (*c, 255))
    s.line([(x + w * 0.5, y + h * 0.36), (x + w * 0.5, y + h * 0.64)], (*dark, 255), max(1, h * 0.05))


def score_icons() -> Image.Image:
    s = Sheet(256, 256)
    safe(s, 136, 8, 48, 48, BLUE)
    safe(s, 136, 72, 48, 48, RED)
    s.circle(160, 162, 22, (240, 240, 245, 255), (30, 30, 30, 255), 3)      # alarm clock
    s.circle(142, 142, 7, (200, 200, 205, 255), (30, 30, 30, 255), 2)
    s.circle(178, 142, 7, (200, 200, 205, 255), (30, 30, 30, 255), 2)
    s.line([(160, 162), (160, 148)], (20, 20, 20, 255), 2.5)
    s.line([(160, 162), (170, 168)], (20, 20, 20, 255), 2.5)
    return s.done()


def minimap_icons() -> Image.Image:
    s = Sheet(256, 256)
    for u, c in ((224, BLUE), (192, RED)):
        s.poly([(u + 16, 4), (u + 25, 27), (u + 16, 21), (u + 7, 27)], (*c, 255))    # you
        s.circle(u + 16, 48, 5, (*c, 255))                                            # team mates
        s.rrect(u + 8, 136, u + 24, 152, 3, (235, 190, 50, 255), (*c, 255), 2)        # gold
        safe(s, u + 6, 198, 20, 20, c)
    for u, c in ((64, RED), (96, BLUE)):
        s.circle(u + 16, 208, 10, outline=(*c, 255), width=3)                         # delivery point
        s.circle(u + 16, 208, 3, (*c, 255))
    for u, c in ((96, GREY), (128, RED), (160, BLUE)):
        buggy(s, u + 4, 232, 24, 14, (*c, 255))                                       # trailer
    # the map's frame, cut into 16 px pieces: outer edge x 13.., y 77..; 70 across
    for x, c in ((13, RED), (109, BLUE)):
        pale = tuple(min(255, v + 90) for v in c)
        s.rrect(x + 1, 78, x + 69, 146, 8, outline=(*c, 255), width=4)
        s.rrect(x + 5, 82, x + 65, 142, 5, outline=(*pale, 170), width=1.5)
    return s.done()


CROSSHAIR = 128   # the HUD scales these up a lot: the originals' 32 px rings have flat sides


# What each of the originals' A..X shows, in their 32 px: ring radii, the mark inside ("cross" or
# "ex", its reach, how far from the centre it starts), and whether the centre has a dot.
CROSSHAIRS = [
    ((), None, True), ((), ('cross', 14, 0), True), ((), ('ex', 11, 0), True), ((), ('cross', 8, 0), True),
    ((), ('cross', 11, 0), True), ((), ('cross', 8, 3), False), ((), ('cross', 14, 3), False),
    ((7,), None, True), ((7,), ('cross', 14, 0), True), ((7,), ('ex', 11, 0), True), ((7,), ('cross', 10, 0), True),
    ((7,), ('cross', 12, 0), True), ((7,), ('cross', 14, 3), False),
    ((13,), None, True), ((13,), ('cross', 13, 0), True), ((13,), ('ex', 13, 0), True), ((13,), ('cross', 8, 0), True),
    ((13,), ('cross', 11, 0), True), ((13,), ('cross', 13, 3), False),
    ((13, 7), None, True), ((13, 7), ('cross', 13, 0), True), 'smile', ((13, 7), ('cross', 8, 0), True),
    ((13, 7), ('cross', 13, 3), False),
]


def crosshair(index: int) -> Image.Image:
    """Same marks as the originals' A..X: thin soft lines that fade out, soft rings, a bright
    centre dot. Drawn smooth at any size, and stopping short of the texture's edge (the HUD
    scales these up a lot: a shadow cut at the edge shows as a flat side on a ring).
    In the alpha, half that in the colour, like theirs (the HUD uses only the alpha)."""
    k = CROSSHAIR / 32
    yy, xx = np.mgrid[0:CROSSHAIR, 0:CROSSHAIR].astype(np.float32)
    x, y = (xx + 0.5) / k - 16, (yy + 0.5) / k - 16   # 32 px units from the centre
    rho = np.hypot(x, y)

    def line(along, across, reach, gap):
        body = np.clip((1.4 - np.abs(across)) / 0.8, 0, 1)
        fade = np.clip(1 - along / reach, 0, 1) ** 0.45
        return body * fade * np.clip((along - gap) / 1.0 + 1 if gap else 1, 0, 1) * (along >= 0)

    def ring(r, cx=0.0, cy=0.0, sx=1.0, sy=1.0):
        d = np.hypot((x - cx) / sx, (y - cy) / sy)
        return np.clip((1.5 - np.abs(d - r)) / 1.0, 0, 1) * 0.9

    a = np.zeros_like(x)
    spec = CROSSHAIRS[index]
    if spec == 'smile':
        a = np.maximum.reduce([ring(13), ring(2.2, -4, -3, 1, 2), ring(2.2, 4, -3, 1, 2),
                               ring(8) * np.clip(y - 3.5, 0, 1)])
        dot = True
    else:
        rings, mark, dot = spec
        for r in rings:
            a = np.maximum(a, ring(r))
        if mark:
            kind, reach, gap = mark
            u, v = (x, y) if kind == 'cross' else ((x + y) / 2 ** 0.5, (x - y) / 2 ** 0.5)
            for along, across in ((u, v), (-u, v), (v, u), (-v, u)):
                a = np.maximum(a, line(along, across, reach, gap))
    if dot:
        a = np.maximum(a, np.clip((1.5 - rho) / 0.6, 0, 1))
    rgb = (a * 127.5).astype(np.uint8)
    return Image.fromarray(np.dstack((rgb, rgb, rgb, (a * 255).astype(np.uint8))), 'RGBA')


# --- weapon icons: a side view of each item, 256 x 256, at HUDIconRect ---------------------------

def _rect(s, x0, y0, x1, y1, f):
    s.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], f)


def w_pistol(s, f):
    s.poly([(4, 22), (42, 20), (43, 28), (38, 31), (30, 31), (28, 34), (21, 34), (24, 50), (11, 50), (12, 34), (4, 30)], f)


def w_grenade(s, f):
    s.circle(72, 36, 14, f)
    _rect(s, 66, 16, 78, 24, f)
    s.poly([(78, 17), (90, 20), (88, 34), (84, 33), (85, 23), (78, 23)], f)


def w_knife(s, f):
    s.poly([(101, 12), (111, 16), (126, 36), (121, 41), (106, 24)], f)
    s.poly([(118, 34), (127, 43), (124, 46), (115, 37)], f)
    s.poly([(123, 42), (136, 56), (131, 60), (118, 46)], f)


def w_adrenaline(s, f):
    s.poly([(151, 8), (155, 6), (175, 38), (171, 41)], f)
    s.poly([(147, 15), (160, 6), (162, 9), (149, 18)], f)
    s.poly([(164, 34), (180, 24), (182, 27), (166, 37)], f)
    s.poly([(173, 39), (186, 58), (185, 59), (171, 41)], f)


def w_tripbomb(s, f):
    s.poly([(200, 20), (238, 12), (250, 22), (250, 46), (212, 54), (200, 46)], f)


def w_armour(s, f):
    s.poly(scaled(VEST, 0.8, 0.95, 6, 68), f)


def w_hmg(s, f):
    s.poly([(84, 76), (118, 76), (124, 83), (252, 85), (252, 91), (130, 92), (120, 101), (84, 101), (84, 92), (76, 90), (76, 80)], f)
    s.poly([(106, 100), (116, 100), (138, 124), (130, 126)], f)
    s.poly([(102, 100), (92, 100), (72, 124), (80, 126)], f)
    _rect(s, 150, 80, 172, 85, f)


def w_rocket(s, f):
    s.poly([(6, 148), (112, 146), (126, 140), (152, 144), (152, 166), (126, 170), (112, 164), (6, 162)], f)
    s.poly([(44, 162), (56, 162), (54, 184), (43, 184)], f)
    s.poly([(74, 162), (84, 162), (83, 176), (73, 176)], f)
    _rect(s, 60, 136, 90, 146, f)


def w_shotgun(s, f):
    s.poly([(164, 158), (184, 150), (200, 150), (200, 146), (252, 146), (252, 151), (206, 152), (206, 157), (194, 158), (184, 158), (168, 176), (164, 170)], f)
    _rect(s, 208, 153, 240, 157, f)


def w_mg(s, f):
    s.poly([(4, 212), (22, 208), (30, 208), (34, 204), (62, 204), (64, 209), (100, 209), (100, 213), (68, 214), (66, 220), (42, 220), (42, 244),
            (34, 244), (33, 220), (26, 220), (24, 232), (16, 232), (20, 218), (4, 222)], f)
    _rect(s, 40, 198, 46, 204, f)


def w_rifle(s, f):
    s.poly([(108, 226), (130, 218), (172, 216), (174, 212), (246, 214), (246, 218), (178, 220), (176, 226), (152, 228), (140, 226), (124, 234), (110, 236)], f)
    s.poly([(150, 204), (190, 204), (194, 208), (190, 212), (150, 212), (146, 208)], f)
    _rect(s, 164, 212, 170, 217, f)


WEAPONS = (w_pistol, w_grenade, w_knife, w_adrenaline, w_tripbomb, w_armour, w_hmg, w_rocket, w_shotgun, w_mg, w_rifle)


def weapon_icons() -> Image.Image:
    # gunmetal, light along the top edge and warm along the bottom one
    m = alpha_mask(lambda s: [paint(s, (255, 255, 255, 255)) for paint in WEAPONS], 256, 256)
    above = np.roll(m, 2, axis=0)
    below = np.roll(m, -2, axis=0)
    top = np.clip(m - above, 0, 1)[..., None]
    under = np.clip(m - below, 0, 1)[..., None]
    body = np.broadcast_to(np.array((58, 60, 74), np.float32), (256, 256, 3))
    rgb = body * (1 - top) * (1 - under) + np.array((205, 208, 225), np.float32) * top + np.array((118, 90, 84), np.float32) * under
    return Image.fromarray(np.dstack((rgb, m * 255)).astype(np.uint8), 'RGBA')


def muzzle_flare() -> Image.Image:
    # added at the muzzle on the canvas (RageWeapon.RenderMuzzleFlash), a hot core and spikes
    y, x = np.mgrid[0:128, 0:128].astype(np.float32)
    dx, dy = (x - 63.5) / 64, (y - 63.5) / 64
    r = np.hypot(dx, dy)
    a = np.arctan2(dy, dx)
    spikes = 0.55 + 0.45 * np.abs(np.cos(a * 3.5)) ** 3
    glow = np.clip(1 - r / spikes, 0, 1) ** 2.2
    core = np.clip(1 - r * 3.5, 0, 1)
    rgb = np.dstack((glow * 255, glow * 190 + core * 65, glow * 70 + core * 185))
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), 'RGB').convert('RGBA')


def clear() -> Image.Image:
    # nothing at all: a masked material showing this hides that part of a model
    return Image.new('RGBA', (4, 4), (0, 0, 0, 0))


def vehicle_flag_neutral() -> Image.Image:
    """A plain flag for a driver with no side (the team flags are 2:1 cloth with a border)."""
    h, w = 64, 128
    img = np.full((h, w, 3), 205, np.uint8)
    img[:4], img[-4:], img[:, :4], img[:, -4:] = 120, 120, 120, 120
    return Image.fromarray(img, 'RGB')


SHEETS = {
    'VehicleFlagNeutral': vehicle_flag_neutral,
    'Boxes': boxes, 'Backs': backs, 'Icons': icons, 'ScoreBoxes': score_boxes, 'ScoreIcons': score_icons,
    'MinimapNewIcons': minimap_icons, 'WeaponIcons': weapon_icons,
    'LoadoutBackBlue': lambda: loadout_back(0), 'LoadoutBackRed': lambda: loadout_back(1),
    'OwBlue': lambda: pain(0), 'OwRed': lambda: pain(1), 'MuzzleFlare': muzzle_flare, 'Clear': clear,
    **{f'CrossHair{chr(65 + i)}': (lambda i=i: crosshair(i)) for i in range(24)},
}


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for name, make in SHEETS.items():
        make().save(OUT / f'{name}.png', optimize=True)
    print(f'[hudart] {len(SHEETS)} textures -> {OUT}')


if __name__ == '__main__':
    main()
