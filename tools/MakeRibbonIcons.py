"""Draws the ScanToAlibre ribbon button icons into addon\\res.

Each icon is the add-on's blue tile with a light drawing and an orange accent,
like ShowScan.ico. Every size (16, 24, 32, 48, 64, 256) is drawn on its own at
8 times size and shrunk, with strokes kept at least about a pixel wide and the
mesh's inner lines left out below 32 px so the small ones stay readable.

    python tools\\MakeRibbonIcons.py [--preview out.png]

ShowScan.ico and ScanToAlibre.ico are not drawn here.
"""

import io
import math
import os
import struct
import sys

from PIL import Image, ImageDraw

SIZES = [16, 24, 32, 48, 64, 256]
SS = 8  # supersampling

TILE = (30, 75, 140, 255)
LIGHT = (236, 240, 245, 255)
MESH = (170, 180, 192, 255)
MESH_LINE = (95, 110, 130, 255)
ORANGE = (245, 150, 30, 255)
DARK = (25, 35, 55, 255)

RES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "addon", "res")


class Pen:
    """Draws in a 256 x 256 design space onto a supersampled image."""

    def __init__(self, size):
        self.size = size
        self.k = size * SS / 256.0
        self.img = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)
        self.detail = size >= 32

    def p(self, x, y):
        return (x * self.k, y * self.k)

    def w(self, width, least=1.0):
        # width in design units, never under `least` output pixels
        return max(width * self.k, least * SS)

    def poly(self, pts, fill=None, outline=None, width=0, least=1.0):
        q = [self.p(*a) for a in pts]
        if fill:
            self.d.polygon(q, fill=fill)
        if outline and width:
            self.line(pts + [pts[0]], outline, width, least)

    def line(self, pts, colour, width, least=1.0):
        q = [self.p(*a) for a in pts]
        ww = self.w(width, least)
        self.d.line(q, fill=colour, width=int(round(ww)), joint="curve")
        r = ww / 2
        for x, y in (q[0], q[-1]):
            self.d.ellipse((x - r, y - r, x + r, y + r), fill=colour)

    def dot(self, x, y, radius, colour, least=1.0):
        cx, cy = self.p(x, y)
        r = max(radius * self.k, least * SS / 2)
        self.d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=colour)

    def done(self):
        return self.img.resize((self.size, self.size), Image.LANCZOS)


def tile(pen):
    k = pen.k
    pen.d.rounded_rectangle((3 * k, 3 * k, 253 * k, 253 * k), radius=40 * k, fill=TILE)


def dome_points(cx, base, rx, ry, n):
    return [(cx - rx * math.cos(math.pi * i / n), base - ry * math.sin(math.pi * i / n)) for i in range(n + 1)]


def dome(pen, cx=128, base=200, rx=96, ry=80, fill=MESH):
    """A scanned mesh: a dome, triangulated like ShowScan's."""
    top = dome_points(cx, base, rx, ry, 6)
    pen.poly(top, fill=fill)
    if pen.detail:
        mid = dome_points(cx, base, rx * 0.55, ry * 0.5, 3)
        foot = [(cx - rx + 2 * rx * i / 4, base) for i in range(5)]
        edges = []
        for i in range(3):
            edges += [(mid[i], mid[i + 1])]
        for i in range(6):
            j = min(3, (i + 1) // 2)
            edges += [(top[i], mid[j])]
            if i % 2 == 0:
                edges += [(top[i + 1], mid[min(3, i // 2)])]
        for i, m in enumerate(mid):
            edges += [(m, foot[min(4, i + (1 if i >= 2 else 0))])]
            if 0 < i < 3:
                edges += [(m, foot[i + 1 if i == 1 else 2])]
        edges += [(top[0], mid[0]), (top[6], mid[3]), (mid[0], foot[0]), (mid[3], foot[4]), (mid[1], foot[1]), (mid[2], foot[3])]
        for a, b in edges:
            pen.line([a, b], MESH_LINE, 4, 0.6)
    pen.line(top + [(top[0][0], base)], MESH_LINE, 8, 1.0)


def arrow_down(pen, x, y0, y1, colour, shaft=20, head=56):
    pen.line([(x, y0), (x, y1 - head * 0.6)], colour, shaft, 1.5)
    pen.poly([(x - head / 2, y1 - head * 0.75), (x + head / 2, y1 - head * 0.75), (x, y1)], fill=colour)


# ----------------------------------------------------------------------------
# The buttons

def load(pen):
    tile(pen)
    dome(pen, base=214, rx=96, ry=70)
    arrow_down(pen, 128, 28, 150, ORANGE, shaft=26, head=72)


def refine(pen):
    tile(pen)
    # a rough profile on the left becoming smooth on the right
    pts = []
    for i in range(41):
        t = i / 40
        x = 30 + 196 * t
        y = 190 - 110 * math.sin(math.pi * t)
        if t < 0.5:
            y += (14 if i % 2 else -14) * (1 - 2 * t) + 2
        pts.append((x, y))
    pen.poly([(30, 214), *pts, (226, 214)], fill=MESH)
    pen.line(pts[:21], MESH_LINE, 8, 1.0)
    pen.line(pts[20:], ORANGE, 16, 1.5)
    pen.line([(30, 214), (226, 214)], MESH_LINE, 8, 1.0)
    # a sparkle
    sx, sy, r = 196, 56, 30
    pen.poly([(sx, sy - r), (sx + r * 0.25, sy - r * 0.25), (sx + r, sy), (sx + r * 0.25, sy + r * 0.25),
              (sx, sy + r), (sx - r * 0.25, sy + r * 0.25), (sx - r, sy), (sx - r * 0.25, sy - r * 0.25)], fill=LIGHT)


def orient(pen):
    tile(pen)
    dome(pen, cx=128, base=176, rx=70, ry=62)
    # a turning arrow round it
    cx, cy, r = 128, 142, 86
    a0, a1 = math.radians(200), math.radians(-15)
    pts = [(cx + r * math.cos(a0 + (a1 - a0) * i / 30), cy - r * 0.42 * math.sin(a0 + (a1 - a0) * i / 30)) for i in range(31)]
    pen.line(pts, ORANGE, 18, 1.5)
    ex, ey = pts[-1]
    pen.poly([(ex - 18, ey - 30), (ex + 22, ey + 2), (ex - 26, ey + 22)], fill=ORANGE)
    # axes above it
    pen.line([(104, 70), (104, 26)], LIGHT, 10, 1.0)
    pen.line([(104, 70), (152, 70)], LIGHT, 10, 1.0)
    pen.dot(104, 70, 9, ORANGE, 1.5)


def plane(pen, x, colour, fill, half=16, top=30):
    # a slicing plane seen edge on, slanted for depth
    pts = [(x - half, top), (x + half, top + 20), (x + half, 236), (x - half, 216)]
    pen.poly(pts, fill=fill)
    pen.poly(pts, outline=colour, width=8, least=1.0)


def section(pen):
    tile(pen)
    dome(pen)
    plane(pen, 128, ORANGE, (245, 150, 30, 110))


def section_series(pen):
    tile(pen)
    dome(pen)
    for x in (78, 128, 178):
        plane(pen, x, ORANGE, (245, 150, 30, 70), half=10, top=60)


def surface(pen):
    tile(pen)
    # a curved patch with its control points
    def at(u, v):
        x = 40 + 176 * u + 26 * v
        y = 190 - 110 * v + 34 * math.sin(math.pi * u) * (1 - 0.3 * v) - 50 * math.sin(math.pi * u) * 0.8
        return (x - 13, y)
    n = 4 if pen.detail else 2
    outline = [at(i / 20, 0) for i in range(21)] + [at(1, j / 20) for j in range(21)] + \
              [at(1 - i / 20, 1) for i in range(21)] + [at(0, 1 - j / 20) for j in range(21)]
    pen.poly(outline, fill=LIGHT)
    for i in range(1, n):
        pen.line([at(i / n, j / 20) for j in range(21)], MESH_LINE, 4, 0.6)
        pen.line([at(j / 20, i / n) for j in range(21)], MESH_LINE, 4, 0.6)
    pen.line(outline + [outline[0]], MESH_LINE, 6, 1.0)
    for (u, v) in ((0, 0), (1, 0), (0, 1), (1, 1), (0.5, 0.5)):
        x, y = at(u, v)
        pen.dot(x, y - 26 if (u, v) == (0.5, 0.5) else y, 15, ORANGE, 2.2)


def silhouette(pen):
    tile(pen)
    top = dome_points(128, 210, 96, 80, 24)
    pen.poly(top, fill=DARK)
    pen.line(top + [top[0]], ORANGE, 14, 1.5)
    # looked at straight along: three lines of sight
    for x in (76, 128, 180):
        pen.line([(x, 26), (x, 82)], LIGHT, 9, 1.0)
        pen.poly([(x - 14, 76), (x + 14, 76), (x, 98)], fill=LIGHT)


def compare(pen):
    tile(pen)
    bands = [(60, 120, 230, 255), (50, 190, 120, 255), (240, 215, 50, 255), ORANGE, (220, 50, 40, 255)]
    cx, base, rx, ry = 128, 200, 96, 80
    shape = dome_points(cx, base, rx, ry, 24)
    # colour the dome in vertical bands, then outline it
    band = Image.new("L", pen.img.size, 0)
    ImageDraw.Draw(band).polygon([pen.p(*a) for a in shape], fill=255)
    colours = Image.new("RGBA", pen.img.size, (0, 0, 0, 0))
    cd = ImageDraw.Draw(colours)
    for i, c in enumerate(bands + bands[-2::-1]):
        x0 = cx - rx + 2 * rx * i / 9
        cd.rectangle((x0 * pen.k, 0, (x0 + 2 * rx / 9) * pen.k + 1, pen.img.size[1]), fill=c)
    pen.img.paste(colours, (0, 0), band)
    pen.line(shape + [shape[0]], MESH_LINE, 8, 1.0)
    # the part's outline, a little off the scan
    pen.line(dome_points(cx, base, rx - 22, ry - 22, 24), LIGHT, 8, 1.0)


def help_(pen):
    tile(pen)
    # an open book with a question mark over it
    pen.poly([(128, 196), (40, 178), (40, 214), (128, 232)], fill=LIGHT)
    pen.poly([(128, 196), (216, 178), (216, 214), (128, 232)], fill=MESH)
    pen.line([(128, 196), (128, 232)], MESH_LINE, 6, 0.8)
    pen.dot(128, 92, 76, ORANGE)
    from PIL import ImageFont
    size = int(118 * pen.k)
    font = ImageFont.truetype(os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts", "arialbd.ttf"), size)
    x, y = pen.p(128, 94)
    pen.d.text((x, y), "?", font=font, fill=LIGHT, anchor="mm")


ICONS = {
    "Load": load,
    "Refine": refine,
    "Orient": orient,
    "Section": section,
    "SectionSeries": section_series,
    "Surface": surface,
    "Silhouette": silhouette,
    "Compare": compare,
    "Help": help_,
}


def write_ico(path, images):
    blobs = []
    for im in images:
        b = io.BytesIO()
        im.save(b, "PNG")
        blobs.append(b.getvalue())
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b""
    for im, blob in zip(images, blobs):
        s = im.size[0]
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    data = header + entries + b"".join(blobs)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def main():
    preview = None
    if "--preview" in sys.argv:
        preview = sys.argv[sys.argv.index("--preview") + 1]
    rows = []
    for name, draw in ICONS.items():
        images = []
        for s in SIZES:
            pen = Pen(s)
            draw(pen)
            images.append(pen.done())
        write_ico(os.path.join(RES, name + ".ico"), images)
        rows.append(images)
        print("wrote", name + ".ico")
    if preview:
        sheet = Image.new("RGBA", (16 + 24 + 32 + 48 + 64 + 128 + 70, 140 * len(rows)), (255, 255, 255, 255))
        for r, images in enumerate(rows):
            x = 4
            for im in images:
                if im.size[0] > 128:
                    im = im.resize((128, 128), Image.LANCZOS)
                sheet.alpha_composite(im, (x, r * 140 + 4))
                x += im.size[0] + 10
        sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(preview)


if __name__ == "__main__":
    main()
