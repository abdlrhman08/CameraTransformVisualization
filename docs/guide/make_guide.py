#!/usr/bin/env python3
"""Builds docs/MVP_Visualizer_Guide.pdf from this script and the screenshots in fig/.

    pip install reportlab
    python docs/guide/make_guide.py            # writes docs/MVP_Visualizer_Guide.pdf

Fonts: Fira Sans (text) and JetBrains Mono (code) are embedded. Paths below are
the Arch Linux locations; change FONT_DIR if yours differ.
"""
import math
import os
import re
import sys

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.pdfmetrics import registerFontFamily
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.graphics.shapes import Drawing, Line, Polygon, Rect, String
from reportlab.platypus import (BaseDocTemplate, CondPageBreak, Frame, Image, KeepTogether, PageBreak,
                                PageTemplate, Paragraph, Preformatted, Spacer, Table, TableStyle)
from reportlab.platypus.flowables import Flowable

HERE = os.path.dirname(os.path.abspath(__file__))
FIG = os.path.join(HERE, "fig")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "MVP_Visualizer_Guide.pdf")
FONT_DIR = "/usr/share/fonts/TTF"

# ---------------------------------------------------------------- fonts and styles
pdfmetrics.registerFont(TTFont("Body", f"{FONT_DIR}/FiraSans-Regular.ttf"))
pdfmetrics.registerFont(TTFont("Body-B", f"{FONT_DIR}/FiraSans-SemiBold.ttf"))
pdfmetrics.registerFont(TTFont("Body-I", f"{FONT_DIR}/FiraSans-Italic.ttf"))
pdfmetrics.registerFont(TTFont("Head", f"{FONT_DIR}/FiraSans-Bold.ttf"))
pdfmetrics.registerFont(TTFont("Mono", f"{FONT_DIR}/JetBrainsMono-Regular.ttf"))
pdfmetrics.registerFont(TTFont("Mono-B", f"{FONT_DIR}/JetBrainsMono-Bold.ttf"))
registerFontFamily("Body", normal="Body", bold="Body-B", italic="Body-I", boldItalic="Body-B")
registerFontFamily("Mono", normal="Mono", bold="Mono-B", italic="Mono", boldItalic="Mono-B")

INK = colors.HexColor("#1d2230")
MUTED = colors.HexColor("#5b6475")
ACCENT = colors.HexColor("#3d5bd9")
RULE = colors.HexColor("#d5d9e2")
CODEBG = colors.HexColor("#f4f5f8")
NOTEBG = colors.HexColor("#eef2ff")

body = ParagraphStyle("body", fontName="Body", fontSize=10, leading=14.6, textColor=INK, spaceAfter=6)
small = ParagraphStyle("small", parent=body, fontSize=8.6, leading=11.5, textColor=MUTED)
caption = ParagraphStyle("caption", parent=small, alignment=TA_CENTER, spaceBefore=3, spaceAfter=10)
h1 = ParagraphStyle("h1", fontName="Head", fontSize=18, leading=23, textColor=INK, spaceBefore=4, spaceAfter=10)
h2 = ParagraphStyle("h2", fontName="Head", fontSize=12.5, leading=16, textColor=INK, spaceBefore=12, spaceAfter=5)
h3 = ParagraphStyle("h3", fontName="Head", fontSize=10.5, leading=14, textColor=ACCENT, spaceBefore=8, spaceAfter=3)
eq = ParagraphStyle("eq", parent=body, fontName="Body-I", alignment=TA_CENTER, spaceBefore=4, spaceAfter=8)
bullet = ParagraphStyle("bullet", parent=body, leftIndent=14, bulletIndent=3, spaceAfter=3)
cell = ParagraphStyle("cell", parent=body, fontSize=8.8, leading=11.8, spaceAfter=0)
cellh = ParagraphStyle("cellh", parent=cell, fontName="Head", textColor=colors.white)
codest = ParagraphStyle("code", fontName="Mono", fontSize=7.6, leading=9.8, textColor=INK)
note = ParagraphStyle("note", parent=body, fontSize=9.4, leading=13.2, spaceAfter=0)


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def fmt(s):
    """Light markup: `code`, **bold**, *italic*, ^-1 / ^3 / ^2, and literal <sub>/<super> tags."""
    s = esc(s)
    for tg in ("sub", "super"):
        s = s.replace(f"&lt;{tg}&gt;", f"<{tg}>").replace(f"&lt;/{tg}&gt;", f"</{tg}>")
    s = re.sub(r"`([^`]+)`", r'<font name="Mono" size="8.8" color="#2a3a8c">\1</font>', s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", s)
    s = re.sub(r"(?<![\w*])\*([^*]+)\*(?![\w*])", r"<i>\1</i>", s)
    for a, b in (("^-1", "-1"), ("^3", "3"), ("^2", "2")):
        s = s.replace(a, f"<super>{b}</super>")
    return s


story = []


def P(t, st=body): story.append(Paragraph(fmt(t), st))
def RAW(t, st=body): story.append(Paragraph(t, st))
def H1(t): story.extend([CondPageBreak(95 * mm), Paragraph(esc(t), h1)])
def H2(t): story.extend([CondPageBreak(70 * mm), Paragraph(esc(t), h2)])
def H3(t): story.extend([CondPageBreak(30 * mm), Paragraph(esc(t), h3)])
def EQ(t): story.append(Paragraph(t, eq))
def SP(h=4): story.append(Spacer(1, h))


def BUL(items):
    for t in items:
        story.append(Paragraph(fmt(t), bullet, bulletText="•"))
    story.append(Spacer(1, 3))


def STEPS(items):
    for i, t in enumerate(items, 1):
        story.append(Paragraph(fmt(t), bullet, bulletText=f"{i}."))
    story.append(Spacer(1, 6))


def CODE(text, title=None):
    t = Table([[Preformatted(text.strip("\n"), codest)]], colWidths=[170 * mm])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), CODEBG), ("BOX", (0, 0), (-1, -1), 0.5, RULE),
                           ("LEFTPADDING", (0, 0), (-1, -1), 7), ("RIGHTPADDING", (0, 0), (-1, -1), 7),
                           ("TOPPADDING", (0, 0), (-1, -1), 5), ("BOTTOMPADDING", (0, 0), (-1, -1), 5)]))
    items = ([Paragraph(esc(title), small)] if title else []) + [t]
    story.append(KeepTogether(items) if text.count("\n") < 45 else t)
    story.append(Spacer(1, 7))


def NOTE(t):
    tb = Table([[Paragraph(fmt(t), note)]], colWidths=[170 * mm])
    tb.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), NOTEBG), ("LINEBEFORE", (0, 0), (0, -1), 2.2, ACCENT),
                            ("LEFTPADDING", (0, 0), (-1, -1), 9), ("RIGHTPADDING", (0, 0), (-1, -1), 9),
                            ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    story.extend([tb, Spacer(1, 8)])


def TABLE(header, rows, widths):
    data = [[Paragraph(fmt(h), cellh) for h in header]] + [[Paragraph(fmt(c), cell) for c in r] for r in rows]
    t = Table(data, colWidths=[w * mm for w in widths], repeatRows=1)
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), INK), ("VALIGN", (0, 0), (-1, -1), "TOP"),
                           ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f7f8fb")]),
                           ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
                           ("LEFTPADDING", (0, 0), (-1, -1), 5), ("RIGHTPADDING", (0, 0), (-1, -1), 5),
                           ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4)]))
    story.append(KeepTogether([t]) if len(rows) <= 8 else t)
    story.append(Spacer(1, 9))


def matrix(rows, w=13):
    ms = ParagraphStyle("m", parent=body, fontName="Body-I", fontSize=9.4, leading=12, alignment=TA_CENTER, spaceAfter=0)
    t = Table([[Paragraph(c, ms) for c in r] for r in rows], colWidths=[w * mm] * len(rows[0]))
    n = len(rows) - 1
    t.setStyle(TableStyle([("LINEBEFORE", (0, 0), (0, -1), 0.9, INK), ("LINEAFTER", (-1, 0), (-1, -1), 0.9, INK),
                           ("LINEABOVE", (0, 0), (0, 0), 0.9, INK), ("LINEBELOW", (0, n), (0, n), 0.9, INK),
                           ("LINEABOVE", (-1, 0), (-1, 0), 0.9, INK), ("LINEBELOW", (-1, n), (-1, n), 0.9, INK),
                           ("TOPPADDING", (0, 0), (-1, -1), 1.5), ("BOTTOMPADDING", (0, 0), (-1, -1), 1.5),
                           ("LEFTPADDING", (0, 0), (-1, -1), 1), ("RIGHTPADDING", (0, 0), (-1, -1), 1)]))
    return t


def MATROW(items):
    """A centred row of labels (strings) and matrices."""
    cells, widths = [], []
    lab = ParagraphStyle("ml", parent=body, fontName="Body-I", alignment=TA_CENTER, spaceAfter=0)
    for it in items:
        if isinstance(it, str):
            cells.append(Paragraph(it, lab))
            widths.append(max(8, 2.3 * len(re.sub("<[^>]+>", "", it))) * mm)
        else:
            cells.append(it)
            widths.append(sum(it._argW) + 6)
    t = Table([cells], colWidths=widths)
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                           ("LEFTPADDING", (0, 0), (-1, -1), 2), ("RIGHTPADDING", (0, 0), (-1, -1), 2)]))
    story.extend([t, Spacer(1, 8)])


def img(name, width_mm):
    path = os.path.join(FIG, name)
    iw, ih = ImageReader(path).getSize()
    return Image(path, width=width_mm * mm, height=width_mm * mm * ih / iw)


def IMG(name, width_mm, cap=None):
    items = [img(name, width_mm)]
    if cap:
        items.append(Paragraph(fmt(cap), caption))
    story.append(KeepTogether(items))


def IMGROW(names_widths, cap=None):
    t = Table([[img(n, w) for n, w in names_widths]], colWidths=[(w + 4) * mm for _, w in names_widths])
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("ALIGN", (0, 0), (-1, -1), "CENTER")]))
    items = [t]
    if cap:
        items.append(Paragraph(fmt(cap), caption))
    story.append(KeepTogether(items))


class Annotated(Flowable):
    """An image with numbered markers at fractional (x, y) positions from the top-left."""

    def __init__(self, name, width_mm, marks):
        super().__init__()
        self.path = os.path.join(FIG, name)
        iw, ih = ImageReader(self.path).getSize()
        self.width, self.height = width_mm * mm, width_mm * mm * ih / iw
        self.marks = marks

    def wrap(self, aw, ah):
        return self.width, self.height

    def draw(self):
        c = self.canv
        c.drawImage(self.path, 0, 0, self.width, self.height)
        for n, fx, fy in self.marks:
            x, y = fx * self.width, (1 - fy) * self.height
            c.setFillColor(ACCENT)
            c.setStrokeColor(colors.white)
            c.setLineWidth(1.2)
            c.circle(x, y, 7, fill=1, stroke=1)
            c.setFillColor(colors.white)
            c.setFont("Head", 8.5)
            c.drawCentredString(x, y - 3, str(n))


# ---------------------------------------------------------------- diagrams
def arrow(d, x1, y1, x2, y2, label=None, lx=0, ly=0):
    d.add(Line(x1, y1, x2, y2, strokeColor=INK, strokeWidth=1.1))
    a, L = math.atan2(y2 - y1, x2 - x1), 6
    d.add(Polygon([x2, y2, x2 - L * math.cos(a - 0.4), y2 - L * math.sin(a - 0.4),
                   x2 - L * math.cos(a + 0.4), y2 - L * math.sin(a + 0.4)], fillColor=INK, strokeColor=INK))
    if label:
        d.add(String(lx, ly, label, fontName="Body-I", fontSize=9, fillColor=ACCENT, textAnchor="middle"))


def pipeline_diagram():
    W, H = 170 * mm, 62 * mm
    d = Drawing(W, H)
    bw, bh = 44 * mm, 17 * mm
    xs = [4 * mm, 63 * mm, 122 * mm]
    y1, y2 = H - bh - 4 * mm, 6 * mm
    boxes = [("Local space", "the mesh as authored", xs[0], y1), ("World space", "where things sit", xs[1], y1),
             ("Camera (eye) space", "camera at 0, looking down -z", xs[2], y1),
             ("Clip space", "x, y, z, w (before divide)", xs[2], y2), ("NDC", "the cube [-1, 1]³", xs[1], y2),
             ("Window pixels", "+ depth in [0, 1]", xs[0], y2)]
    for t, sub, x, y in boxes:
        d.add(Rect(x, y, bw, bh, rx=6, ry=6, fillColor=colors.HexColor("#f3f5fb"), strokeColor=ACCENT, strokeWidth=1))
        d.add(String(x + bw / 2, y + bh - 7 * mm, t, fontName="Head", fontSize=9.5, fillColor=INK, textAnchor="middle"))
        d.add(String(x + bw / 2, y + 4 * mm, sub, fontName="Body", fontSize=7.5, fillColor=MUTED, textAnchor="middle"))
    ym1, ym2 = y1 + bh / 2, y2 + bh / 2
    arrow(d, xs[0] + bw, ym1, xs[1], ym1, "M", (xs[0] + bw + xs[1]) / 2, ym1 + 4)
    arrow(d, xs[1] + bw, ym1, xs[2], ym1, "V = C⁻¹", (xs[1] + bw + xs[2]) / 2, ym1 + 4)
    arrow(d, xs[2] + bw / 2, y1, xs[2] + bw / 2, y2 + bh, "P", xs[2] + bw / 2 + 8, (y1 + y2 + bh) / 2 - 3)
    arrow(d, xs[2], ym2, xs[1] + bw, ym2, "÷ w", (xs[1] + bw + xs[2]) / 2, ym2 + 4)
    arrow(d, xs[1], ym2, xs[0] + bw, ym2, "viewport", (xs[0] + bw + xs[1]) / 2, ym2 + 4)
    return d


def divide_diagram():
    """Side view: a frustum with two equal objects, then the same after the divide."""
    W, H = 170 * mm, 58 * mm
    d = Drawing(W, H)
    ox, oy, sc = 10 * mm, 29 * mm, 2.6 * mm
    n, f, th = 2.0, 22.0, math.tan(math.radians(30))
    pt = lambda dist, y: (ox + dist * sc, oy + y * sc)
    a, b, c, e = pt(n, n * th), pt(f, f * th), pt(f, -f * th), pt(n, -n * th)
    d.add(Polygon([a[0], a[1], b[0], b[1], c[0], c[1], e[0], e[1]], fillColor=colors.HexColor("#fff6db"),
                  strokeColor=colors.HexColor("#d9a400"), strokeWidth=1))
    for q in (b, c):
        d.add(Line(ox, oy, q[0], q[1], strokeColor=colors.HexColor("#e7c766"), strokeWidth=0.5, strokeDashArray=[2, 2]))
    d.add(Rect(ox - 4, oy - 3, 8, 6, fillColor=INK, strokeColor=INK))
    d.add(String(ox, oy - 12, "camera", fontName="Body", fontSize=7, fillColor=MUTED, textAnchor="middle"))
    for dist, col in [(5, "#d94a4a"), (15, "#3d5bd9")]:
        x, y = pt(dist, -1)
        d.add(Rect(x - 3, y, 6, 2 * sc, fillColor=colors.HexColor(col), strokeColor=None))
        d.add(String(x, y - 9, f"d = {dist}", fontName="Body", fontSize=7, fillColor=MUTED, textAnchor="middle"))
    d.add(String(ox + (n + f) / 2 * sc, H - 6 * mm, "Camera space: equal sizes, frustum widens",
                 fontName="Head", fontSize=8, fillColor=INK, textAnchor="middle"))
    ax = ox + f * sc + 6 * mm
    arrow(d, ax, oy, ax + 12 * mm, oy, "÷ w", ax + 6 * mm, oy + 5)
    bx, bw, bh = ax + 18 * mm, 52 * mm, 40 * mm
    by = oy - bh / 2
    d.add(Rect(bx, by, bw, bh, fillColor=colors.HexColor("#f3f5fb"), strokeColor=ACCENT, strokeWidth=1))
    zndc = lambda dist: (f + n) / (f - n) - 2 * f * n / ((f - n) * dist)
    for dist, col in [(5, "#d94a4a"), (15, "#3d5bd9")]:
        hgt = 2 * (1 / th) / dist
        zx = bx + (zndc(dist) * 0.5 + 0.5) * bw
        d.add(Rect(zx - 3, oy - hgt / 2 * bh / 2, 6, hgt * bh / 2, fillColor=colors.HexColor(col), strokeColor=None))
    for x, y, t, anchor in [(bx, by - 9, "z = -1 (near)", "start"), (bx + bw, by - 9, "+1 (far)", "end"),
                            (bx - 2, by + bh - 3, "+1", "end"), (bx - 2, by, "-1", "end")]:
        d.add(String(x, y, t, fontName="Body", fontSize=7, fillColor=MUTED, textAnchor=anchor))
    d.add(String(bx + bw / 2, H - 6 * mm, "NDC: a box, far object is 3x smaller",
                 fontName="Head", fontSize=8, fillColor=INK, textAnchor="middle"))
    return d


def on_page(c, doc):
    c.saveState()
    if doc.page > 1:
        c.setFont("Body", 7.5)
        c.setFillColor(MUTED)
        c.drawString(20 * mm, 12 * mm, "MVP Visualizer: how it works")
        c.drawRightString(190 * mm, 12 * mm, str(doc.page))
        c.setStrokeColor(RULE)
        c.setLineWidth(0.5)
        c.line(20 * mm, 15.5 * mm, 190 * mm, 15.5 * mm)
    c.restoreState()


# =================================================================== CONTENT
# ---------------------------------------------------------------- title page
story.append(Spacer(1, 12 * mm))
story.append(Paragraph("MVP Visualizer", ParagraphStyle("t", fontName="Head", fontSize=30, leading=36, textColor=INK)))
story.append(Paragraph("How to use it, and how it works: the math, the shaders and the code",
                       ParagraphStyle("st", fontName="Body", fontSize=14, leading=19, textColor=ACCENT, spaceBefore=4)))
story.append(Spacer(1, 8 * mm))
RAW(fmt("A guide to the C++ / OpenGL / Dear ImGui application in `src/`. The first part shows how to use the "
        "program. The rest explains how a vertex travels from a mesh to a pixel, why the camera never really moves, "
        "how the three views fake and then reveal the perspective divide, and where each piece lives in the code."),
    ParagraphStyle("lead", parent=body, fontSize=11.5, leading=16.5))
story.append(Spacer(1, 6 * mm))
IMG("overview.png", 158, "The app with the starter example loaded. Top, left to right: what you think happens, "
    "what actually happens, what the camera sees. Bottom: the node editor and the Settings / Matrices panels.")
story.append(Spacer(1, 2 * mm))
toc = [("1", "The idea in one page"), ("2", "Using the program"), ("3", "The math of the pipeline"),
       ("4", "How the code is organised"), ("5", "The node graph"), ("6", "Rendering infrastructure"),
       ("7", "The shaders, line by line"), ("8", "The three views"), ("9", "The user interface"),
       ("10", "Extending the app")]
tt = Table([[Paragraph(n, ParagraphStyle("tn", parent=body, fontName="Head", textColor=ACCENT, spaceAfter=0)),
             Paragraph(esc(t), ParagraphStyle("tt", parent=body, spaceAfter=0))] for n, t in toc],
           colWidths=[9 * mm, 120 * mm])
tt.setStyle(TableStyle([("TOPPADDING", (0, 0), (-1, -1), 0.5), ("BOTTOMPADDING", (0, 0), (-1, -1), 0.5)]))
story.extend([tt, PageBreak()])

# ---------------------------------------------------------------- 1
H1("1. The idea in one page")
P("Every 3D program asks the same question for each vertex of each mesh: *where on the screen does this point land?* "
  "The answer is a chain of matrix multiplications usually written as")
EQ("clip = P · V · M · vertex")
P("read right to left. **M** (model) places the mesh in the world. **V** (view) re-expresses the world relative to the camera. "
  "**P** (projection) squeezes what the camera can see into a fixed box. The GPU then divides by the fourth coordinate *w*, "
  "which is what makes distant things small, and maps the box onto the window.")
P("The trap for beginners is the word *camera*. When you move a camera in an engine, it feels like the camera moves through "
  "a still world. The GPU has no camera. There is only one fixed observer, at the origin, looking down the negative z axis. "
  "To make it look as if the camera moved right, the whole world is moved left. That is all the view matrix does: "
  "it is the **inverse** of the camera's placement in the world.")
P("The app shows this with three windows driven by the same node graph:")
TABLE(["Window", "What it shows", "Space it draws in"],
      [["1. What you think happens", "A still world and a camera model flying through it, placed by its pose `C`.", "World space"],
       ["2. What actually happens", "The camera nailed to the origin and the world moved by `V = C^-1`. A slider then applies the perspective divide, turning the frustum into the cube [-1, 1]^3.", "Camera (eye) space"],
       ["3. What the camera sees", "The real rendering: `P · V · M · v` straight to the window. You can also fly this camera.", "Clip space, then pixels"]],
      [42, 90, 38])
P("The node graph builds the matrices. **Object** chains build each model matrix, a **Camera** chain builds the pose `C`, "
  "a **Projection** node builds `P`, and the **MVP Output** node collects them. A **Matrix** node holds any 4×4 matrix and "
  "can stand in for any of them. If no projection is connected, `P` is the identity, which shows what rendering looks "
  "like with only `V · M`.")

# ---------------------------------------------------------------- 2  USING THE PROGRAM
story.append(PageBreak())
H1("2. Using the program")
H2("2.1 Building and running")
P("The build fetches GLFW, GLM, Dear ImGui (docking branch) and imnodes automatically. Only the OpenGL loader, GLAD, is a "
  "manual step: generate it at glad.dav1d.de (C/C++, OpenGL 3.3, Core profile, \"Generate a loader\") and copy it to "
  "`external/glad/`, as the README describes. Then:")
CODE("""cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/gfx_node_editor""")
P("The first configure takes a while because it clones the dependencies. The build copies two fonts into `build/fonts/`. "
  "The window layout is saved to `imgui.ini` in the folder you start the program from, and restored next time.")

H2("2.2 The screen")
story.append(KeepTogether([Annotated("overview.png", 170, [
    (1, 0.135, 0.013), (2, 0.18, 0.038), (3, 0.51, 0.038), (4, 0.84, 0.038), (5, 0.66, 0.12),
    (6, 0.43, 0.135), (7, 0.10, 0.53), (8, 0.43, 0.88), (9, 0.905, 0.53)]),
    Paragraph(fmt("The main window with the starter example. The numbers match the table below."), caption)]))
TABLE(["#", "Part", "What it is for"],
      [["1", "Menu bar", "**File** (quit), **View** (show or hide windows, text toggles, reset layout), **Graph** (starter example or empty graph), **Help** (controls)."],
       ["2", "1. What you think happens", "The world as you imagine it: a fixed scene with the camera and its viewing volume (yellow) placed in it. Drag to orbit, wheel to zoom."],
       ["3", "2. What actually happens", "The camera fixed at the origin and the world moved around it. Drag to orbit, wheel to zoom."],
       ["4", "3. What the camera sees", "The final rendered image. Has the **Play** button for flying the camera."],
       ["5", "Play controls", "Play / Stop, fly speed, and the *grid and axes* toggle for this view."],
       ["6", "Divide controls", "The perspective-divide slider, *animate*, *ghost*, the depth mode and *cut away outside [-1, 1]*."],
       ["7", "Node editor toolbar", "Buttons to add each node type, Duplicate, Delete selected, Reset, Show matrices, and (?) for help."],
       ["8", "Node canvas", "Where you build the graph. A minimap sits in the bottom-right corner. Warnings appear above the canvas."],
       ["9", "Settings / Matrices", "Settings holds every toggle. Matrices lists C, V, P and each object's M and P · V · M."]],
      [8, 40, 122])
P("Every window can be moved. Drag a tab or title bar onto the docking targets to split or tab it, or drag it outside "
  "the main window to make it a separate OS window, for example on a second monitor. **View > Reset layout** restores this arrangement.")

H2("2.3 Your first scene, step by step")
P("The program starts with an empty graph holding only the green **MVP Output** node. Its three input pins are Model, View and "
  "Projection. Build a scene like this:")
STEPS([
    "Click **Object** in the toolbar. Drag from its *out* pin to the Output's **Model** pin. A cube appears at the world origin in all three views.",
    "Click **Translate**. Connect Object → Translate → Model, replacing the direct link, and set the offset to (0, 0.5, 0). The cube now sits on the grid.",
    "Click **Camera** and connect it to the **View** pin. Change *position* and *look at*. View 1 shows the camera moving through the world. In view 2 the camera stays put and the world moves the other way.",
    "Click **Projection** and connect it to the **Projection** pin. View 3 gains perspective: things further away get smaller. Try the *fov*, *near* and *far* fields.",
    "Add a **Rotate Y** between Camera and the View pin and tick *spin*. The camera orbits the cube in view 1, while in view 2 the world turns the opposite way around the fixed camera.",
    "In view 2, drag the **perspective divide** slider from 0 to 1. The yellow frustum turns into the white cube [-1, 1]^3 and far objects shrink.",
    "Press **Play** in view 3 and fly with WASD while dragging the image to look. The Camera node's fields change as you move.",
])
NOTE("The order of nodes matters. Object → Rotate → Translate rotates the object first and then moves it. Swap the two "
     "transforms and the object is moved first and then swung around the world origin. Section 3.1 explains why.")
P("**Graph > Load starter example** builds a complete scene in one click: a rotated cube, a triangle further away, an orbiting "
  "camera and a projection. **Graph > New empty graph** starts over.")

H2("2.4 Editing the graph")
TABLE(["To…", "Do this"],
      [["Add a node", "Click its toolbar button (it appears mid-canvas), or right-click empty canvas and pick it (it appears at the mouse)."],
       ["Connect pins", "Drag from an *out* pin to an input pin. An input takes one link, so a new link replaces the old one. The Output's Model pin takes many, one per object."],
       ["Remove a link", "Ctrl+click it and drop it anywhere, or right-click it and choose Delete link, or select it and press Delete."],
       ["Remove a node", "Click the **x** in its title bar, or right-click it and choose Delete, or select it and press Delete or Backspace."],
       ["Select several", "Drag a box on empty canvas, then use the toolbar's Delete selected or Duplicate."],
       ["Duplicate", "Toolbar Duplicate or right-click > Duplicate. Settings are copied, links are not."],
       ["Pan the canvas", "Middle-mouse drag."],
       ["See matrices", "*Show matrices* prints each node's running matrix: the model, pose or projection built so far."]],
      [32, 138])

H2("2.5 The nodes")
TABLE(["Node", "Fields", "Produces"],
      [["Object", "*shape*: Cube or Triangle. For a triangle, the three corners A, B, C in the object's own space, and *Reset triangle*.", "Starts a model chain. Connect it, possibly through transforms, to Model."],
       ["Translate", "*offset* (x, y, z)", "`T(offset)`"],
       ["Rotate X / Y / Z", "*deg*; *spin* animates the angle at *deg/s*", "A rotation about that axis"],
       ["Scale", "*factor* (x, y, z)", "`S(factor)`"],
       ["Matrix", "Any 4×4 matrix, typed row by row as on paper. *Identity*, *Transpose*, *Invert* (disabled when the determinant is 0), and the determinant shown below.", "Exactly that matrix. Works anywhere: in an object chain, as or in the camera chain, as the projection, or after a Projection node."],
       ["Camera", "*position*, *look at*", "Starts a camera chain. Its pose is `C = inverse(lookAt(position, look at, up))`."],
       ["Projection", "*fov*, *near*, *far*. Aspect is taken from the camera view's window.", "A perspective matrix `P`."],
       ["MVP Output", "Pins: Model (many), View, Projection", "Collects M, C and P. Shows `V = inverse(C)`."]],
      [27, 70, 73])
P("Node title colours tell you what a chain is: blue for a model chain, orange for a camera chain, purple for a projection "
  "chain, grey for a chain with no source, and red for an invalid chain (a cycle).")

H2("2.6 Flying the camera (play mode)")
IMGROW([("play_view.png", 88), ("play_node.png", 62)],
       "Play mode after flying forward and turning right. The green border shows play is on, and the Camera node's "
       "position and look-at fields have changed to match.")
P("Press **Play** in *What the camera sees*. While playing, and while that window has focus:")
TABLE(["Input", "Effect"],
      [["W / S", "Fly forward / backward along the view direction"], ["A / D", "Strafe left / right"],
       ["Q / E", "Move down / up along the world's vertical"], ["Shift", "Three times faster"],
       ["Drag the image", "Look around (turn and tilt)"], ["Esc or Stop", "Leave play mode"],
       ["speed slider", "Base speed in units per second"]], [40, 130])
P("Every move is written back into the node graph, so views 1 and 2, the Matrices panel and the node fields all follow. The "
  "View chain must start with a **Camera** or a **Matrix** node. Transforms after it are kept, and the start node is "
  "solved so that the whole chain lands on your pose. If nothing is connected to View, a Camera node is created and linked for "
  "you. Spinning nodes pause while you fly, so they don't fight your movement.")

H2("2.7 Keyboard and mouse reference")
TABLE(["Where", "Input", "Effect"],
      [["Anywhere", "H", "Show or hide the explanations on the views"],
       ["Anywhere", "L", "Show or hide the labels drawn in the 3D scene"],
       ["Views 1 and 2", "Drag / mouse wheel", "Orbit / zoom the observer camera"],
       ["View 3, playing", "WASD, Q/E, Shift, drag, Esc", "Fly the camera (section 2.6)"],
       ["Node editor", "Delete or Backspace", "Remove the selected nodes and links"],
       ["Node editor", "Ctrl+click a link", "Detach it"],
       ["Node editor", "Right-click", "Context menu for a node, a link or the empty canvas"],
       ["Node editor", "Middle drag", "Pan"]], [32, 50, 88])

H2("2.8 Settings")
TABLE(["Setting", "Effect"],
      [["Play animations", "Turns all *spin* animations on or off."],
       ["Dim what the camera can't see", "In views 1 and 2, fades everything outside the student camera's volume."],
       ["Grid and axes in the camera view", "Hides the floor grid and axes in view 3 so only the objects remain."],
       ["Explanations / Labels", "The same switches as H and L."],
       ["Perspective divide", "The view 2 slider, its animation, the ghost of the undivided scene, *cut away outside [-1, 1]*, and the depth mode."],
       ["Show matrices on nodes", "The running matrix printed on each node."]], [52, 118])

H2("2.9 Experiments worth trying")
BUL(["**Order of operations.** Swap a Rotate and a Translate in an object chain and watch the object orbit the origin instead of spinning in place.",
     "**The inverse.** Put a Translate (5, 0, 0) after the Camera. In view 2 the world origin moves to x = −5: the overlay prints it.",
     "**No projection.** Delete the Projection link. Only a 2×2×2 box around the camera is visible, nothing shrinks, and depth runs backwards (section 3.5).",
     "**Depth precision.** Set *near* to 0.01 and choose *z: real NDC z* in view 2. Almost everything is squeezed against the far face.",
     "**A custom projection.** Feed a Matrix into the Projection pin, for example the orthographic matrix `glm::ortho(-6, 6, -4, 4, 1, 15)` shown in section 9.1: rows (0.17, 0, 0, 0), (0, 0.25, 0, 0), (0, 0, −0.14, −1.14), (0, 0, 0, 1). The yellow volume becomes a box and distance no longer changes size.",
     "**Flip the image.** Put a Matrix after the Projection node with −1 in the top-left corner. View 3 is mirrored left to right.",
     "**Fly into the frustum.** Use play mode to fly between objects and watch the frustum move in view 1 while view 2's world slides past the fixed camera."])

H2("2.10 Warnings and problems")
TABLE(["Message or symptom", "Meaning and fix"],
      [["Nothing is connected to Model", "No object reaches the Output. Connect an Object chain (or any chain) to the Model pin."],
       ["Nothing is connected to View (using a default camera)", "A default camera at (0, 2, 7) is used. Connect a Camera node to control it."],
       ["… is part of a cycle", "Links form a loop, shown as red nodes. Delete one of the links."],
       ["View pin: the camera pose has determinant 0", "The pose can't be inverted into V. Usually a Scale with a 0 factor, or an all-zero Matrix."],
       ["Projection: P has determinant 0", "It still renders, but its volume can't be drawn in views 1 and 2."],
       ["Play stops with a message", "The View chain must start with a Camera or a Matrix node."],
       ["Windows dragged outside appear in odd places", "On Linux the app uses X11 (XWayland under Wayland) for this. A pure Wayland session can't place separate windows."],
       ["Fonts look plain", "The fonts weren't found next to the executable. Rebuild, or run the program from the build folder."]],
      [58, 112])

# ---------------------------------------------------------------- 3  MATH
H1("3. The math of the pipeline")
story.append(pipeline_diagram())
story.append(Paragraph("Figure 1. The coordinate spaces a vertex passes through, and the step between each pair.", caption))
P("All points are 4-component *homogeneous* vectors (x, y, z, 1). The fourth component lets a 4×4 matrix express translation, "
  "and later carries the depth used for the perspective divide. GLM, like GLSL, stores matrices column by column, so `m[3]` is "
  "the fourth column: the translation part.")

H2("3.1 The model matrix M")
P("Each transform node produces one 4×4 matrix in `Node::RecomputeLocal` (NodeGraph.cpp). With *s* = sin θ and *c* = cos θ:")
MATROW(["T(t) =", matrix([["1", "0", "0", "t<sub>x</sub>"], ["0", "1", "0", "t<sub>y</sub>"], ["0", "0", "1", "t<sub>z</sub>"], ["0", "0", "0", "1"]], 10),
        "  R<sub>y</sub>(θ) =", matrix([["c", "0", "s", "0"], ["0", "1", "0", "0"], ["−s", "0", "c", "0"], ["0", "0", "0", "1"]], 10),
        "  S(k) =", matrix([["k<sub>x</sub>", "0", "0", "0"], ["0", "k<sub>y</sub>", "0", "0"], ["0", "0", "k<sub>z</sub>", "0"], ["0", "0", "0", "1"]], 10)])
P("`glm::translate`, `glm::rotate` and `glm::scale` build exactly these. Rotate X and Rotate Z are the same idea around the other axes. "
  "A Matrix node simply returns the matrix you typed.")
H3("Composition order")
P("Matrices apply right to left, so `T · R` rotates first and translates second. The graph reads left to right in *data-flow order*: "
  "`Object → Rotate → Translate` means *first rotate, then translate*. To make that come out right, each node multiplies its own "
  "matrix on the **left** of everything upstream:")
EQ("cumulative<sub>node</sub> = local<sub>node</sub> · cumulative<sub>upstream</sub>")
P("so the chain above yields `M = T · R`. That line is in `NodeGraph::Resolve`. Swap the two nodes and you get `R · T`: the cube is moved "
  "first and then swung around the world origin.")

H2("3.2 The camera pose C and the view matrix V")
P("The Camera node stores a *position* `e` and a *look-at target*. `glm::lookAt` builds the view matrix from three orthonormal axes:")
BUL(["forward `f = normalize(target − e)`", "right `r = normalize(f × up)`, with `up = (0, 1, 0)`", "true up `u = r × f`"])
MATROW(["V = lookAt(e, target, up) =", matrix([["r<sub>x</sub>", "r<sub>y</sub>", "r<sub>z</sub>", "−r·e"],
                                              ["u<sub>x</sub>", "u<sub>y</sub>", "u<sub>z</sub>", "−u·e"],
                                              ["−f<sub>x</sub>", "−f<sub>y</sub>", "−f<sub>z</sub>", "f·e"],
                                              ["0", "0", "0", "1"]], 13)])
P("The rows are the camera's axes and the last column subtracts the camera position. The camera's **pose**, its placement in the world, "
  "is the inverse. Its columns are the axes and the position, which is easy to read:")
MATROW(["C = V<super>-1</super> =", matrix([["r<sub>x</sub>", "u<sub>x</sub>", "−f<sub>x</sub>", "e<sub>x</sub>"],
                                         ["r<sub>y</sub>", "u<sub>y</sub>", "−f<sub>y</sub>", "e<sub>y</sub>"],
                                         ["r<sub>z</sub>", "u<sub>z</sub>", "−f<sub>z</sub>", "e<sub>z</sub>"],
                                         ["0", "0", "0", "1"]], 13)])
P("The app thinks in terms of `C`, because that is what a person means by \"where the camera is\". `Node::CameraBasePose` returns "
  "`inverse(lookAt(...))`, guarding two degenerate cases: a target equal to the position, and looking straight up or down, where `f × up` is zero. "
  "Transforms after the Camera node then move the camera exactly as they move an object. `NodeGraph::Evaluate` finally sets `V = inverse(C)`.")
H3("Why the world moves the other way")
P("Inverting a product reverses and inverts each factor: `(R · C)^-1 = C^-1 · R^-1`. So if a Rotate Y of +30° orbits the camera, the world in "
  "view 2 turns by −30°. If the camera moves +5 along x, `V` contains a translation of −5. View 2's overlay prints `V`'s translation column as "
  "\"world origin is now at\", which is exactly where the world origin ends up relative to the fixed camera.")

H2("3.3 The projection matrix P")
P("The Projection node calls `glm::perspective(fov, aspect, n, f)`, the standard OpenGL right-handed projection with depth mapped to [-1, 1]. "
  "With `g = 1 / tan(fov / 2)` and `a` the aspect ratio:")
MATROW(["P =", matrix([["g / a", "0", "0", "0"], ["0", "g", "0", "0"],
                       ["0", "0", "(f + n) / (n − f)", "2fn / (n − f)"], ["0", "0", "−1", "0"]], 22)])
P("The last row is the important one. It copies −z into *w*. Since the camera looks down −z, the distance in front of the camera is `d = −z`, so "
  "after projection **w = d**. The GPU divides x, y and z by w, the *perspective divide*, to get normalised device coordinates (NDC):")
EQ("x<sub>ndc</sub> = (g / a) · x / d        y<sub>ndc</sub> = g · y / d        z<sub>ndc</sub> = (f + n) / (f − n) − 2fn / ((f − n) · d)")
P("The `1 / d` in x and y **is** perspective: an object twice as far away covers half as much of the screen. With the default 60° field of view, "
  "`g ≈ 1.732`. A 1-unit tall object at d = 2 spans 0.87 of NDC height, and at d = 8 only 0.22, four times smaller.")
P("The depth formula also contains `1 / d`, so depth is *not* linear. With the default near = 0.5 and far = 25:")
TABLE(["distance d", "0.5 (near)", "1", "2", "5", "12.75 (halfway)", "25 (far)"],
      [["z<sub>ndc</sub>", "−1.00", "0.02", "0.53", "0.84", "0.96", "1.00"]], [26, 20, 18, 18, 18, 30, 20])
P("Half of the depth range is spent on the first half unit in front of the near plane. This is why a tiny near value ruins depth precision, and why "
  "view 2 offers two depth modes (section 7).")
story.append(divide_diagram())
story.append(Paragraph("Figure 2. Two equal objects in camera space (left) and after the divide (right). The frustum becomes a box, the far "
                       "object shrinks by 1/d, and depth crowds towards the far face.", caption))

H2("3.4 Clipping, depth and the window")
P("The GPU keeps a point only if it lies inside the clip volume, tested *before* the divide:")
EQ("−w ≤ x ≤ w,   −w ≤ y ≤ w,   −w ≤ z ≤ w,   with w > 0")
P("which is the same as requiring the NDC point to lie in the cube [-1, 1]^3. Survivors are mapped to the window: x and y from [-1, 1] to pixels, and "
  "z to a depth value `(z_ndc + 1) / 2` in [0, 1]. The depth test keeps the fragment with the smallest depth.")

H2("3.5 No projection at all")
P("If nothing is connected to the Projection pin, `Evaluate` sets `P` to the identity, so `clip = V · M · vertex` with *w* = 1. Then:")
BUL(["**Nothing shrinks.** Dividing by w = 1 changes nothing, so distance has no effect on size.",
     "**Only a 2×2×2 box is visible.** The clip test becomes −1 ≤ x, y, z ≤ 1 in camera space itself: one unit around the camera in every direction.",
     "**The image stretches.** No `1 / a` factor corrects for the window's aspect ratio.",
     "**Depth is reversed.** Depth is `(z + 1) / 2`. A point one unit *in front* (z = −1) gets depth 0, the nearest value, and a point one unit "
     "*behind* gets depth 1. Further-away objects therefore draw on top of nearer ones, and things just behind the camera are drawn too."])
IMG("noproj.png", 170, "With no Projection connected: the camera's visible volume is the small box around it (views 1 and 2), "
    "and only the triangle that sits inside it appears in view 3, stretched to the window.")

H2("3.6 Any matrix as the projection")
P("The Projection pin accepts any chain, so `P` can be a Matrix node, or a Projection node followed by more transforms "
  "(`Projection → Matrix` gives `P' = Matrix · P`). The views can't rely on fov, near and far any more, so everything they need is "
  "read back out of `P` itself, using its inverse:")
BUL(["**The visible volume** is whatever `P` maps onto the NDC cube, so its eight corners are `P^-1 · (±1, ±1, ±1, 1)`, each divided by its own w. "
     "That gives a pyramid for a perspective matrix, a box for an orthographic one, and the 2×2×2 box for the identity.",
     "**Near and far** are the distances in front of the camera of the centres of the NDC faces z = −1 and z = +1, mapped the same way. "
     "For `glm::perspective` this returns exactly the node's values.",
     "**Perspective or not:** `P` divides by distance only if its bottom row reads z into *w*. The app checks the bottom row and flags the result as `perspective`."])
IMG("ortho_views.png", 170, "A Matrix node holding an orthographic matrix as P: the viewing volume is a box, and an object's size no longer depends on its distance.")

# ---------------------------------------------------------------- 4  CODE ORGANISATION
H1("4. How the code is organised")
TABLE(["File", "Responsibility"],
      [["`main.cpp`", "Creates the window, the OpenGL context, ImGui, imnodes; the menu bar; the docking space; the frame loop."],
       ["`NodeGraph.h/.cpp`", "Pure data and math: nodes, links, chain evaluation into an `EvalResult`, animation, writing a camera pose back, the starter example."],
       ["`Renderer.h/.cpp`", "Everything OpenGL: the shader program, meshes and shape builders, offscreen framebuffers, the `Renderer`, the shared `Scene`, the orbit camera."],
       ["`UI.h/.cpp`", "Everything ImGui that is not a 3D view: options, theme and fonts, small widgets, window titles and default layout, Node Editor, Settings, Matrices."],
       ["`Views.h/.cpp`", "The three 3D windows. Each is a struct owning its framebuffer and, for views 1 and 2, its orbit camera. View 3 also owns play mode."]],
      [38, 132])
P("Only `NodeGraph` knows nothing about rendering, and only `Renderer` talks to OpenGL directly (apart from one `glPolygonMode` call in view 2). "
  "The views combine the two.")
H2("4.1 One frame")
P("ImGui is an *immediate-mode* library: the whole UI is described again every frame by plain function calls. The loop in `main.cpp` runs in this order:")
STEPS(["Poll input, compute `dt`, start a new ImGui frame.",
       "Menu bar and the H / L shortcuts.",
       "Dock space over the whole window. On the first frame, build the default layout if `imgui.ini` has none.",
       "**Node Editor** and **Settings**. These run first, so edits made this frame are visible in the views this frame.",
       "`graph.Animate(dt)` spins the Rotate nodes that have *spin* on, unless play mode is active. The divide slider is animated if requested.",
       "Projection nodes receive the camera window's aspect ratio, measured last frame.",
       "`graph.Evaluate()` produces the `EvalResult`: every object's `M`, plus `C`, `V`, `P` and warnings. The frustum mesh is rebuilt from it.",
       "The three views render into their framebuffers and show them as images. In play mode view 3 also writes a new camera pose into the graph, which the next frame's evaluation picks up. Then the Matrices panel.",
       "`ImGui::Render`, draw the UI into the main window, draw any windows dragged outside the app, swap buffers."])
NOTE("Key idea: the 3D views do not draw directly to the screen. Each renders into its own offscreen texture during step 8, and ImGui "
     "draws that texture like a picture inside a window in step 9. That is what makes the views dockable, resizable and able to leave the main window.")

# ---------------------------------------------------------------- 5  NODE GRAPH
H1("5. The node graph")
H2("5.1 Data model")
P("A `Node` holds an `id`, a `NodeType` and the parameters of every type: vector, angle and spin for transforms; a 4×4 `custom` matrix for "
  "Matrix nodes; shape and three triangle corners `tri[3]` for objects; position and target for the camera; and fov, near, far and aspect "
  "for projections. It is a flat struct rather than a class hierarchy, so duplicating a node is a single assignment. Evaluation fills three "
  "output fields: `localMatrix`, `cumulativeMatrix` and `kind`, the chain the node belongs to.")
P("A `Link` connects an output *attribute* (a pin) to an input attribute. imnodes needs a unique integer for every pin, so pin ids are derived from the node id:")
TABLE(["Pin", "Id", "Used by"],
      [["output", "`id · 8 + 1`", "every node except MVP Output"], ["input / Model", "`id · 8 + 2`", "transforms; the Output's Model pin"],
       ["View", "`id · 8 + 3`", "MVP Output only"], ["Projection", "`id · 8 + 4`", "MVP Output only"]], [40, 40, 90])
P("`NodeOfAttr(attr) = attr / 8` recovers the node, and `attr % 8 == 1` identifies outputs. No lookup table is needed.")
H3("Link rules (AddLink)")
BUL(["The two pins are normalised to *output → input*. Two outputs, two inputs, or a node linked to itself are rejected.",
     "Most inputs accept one link, so a new link replaces the old one. The Output's **Model** pin accepts many, one per object.",
     "An output can feed many inputs, so two objects can share a transform."])

H2("5.2 Evaluating chains (Resolve)")
P("Evaluation walks *backwards* from each node to the source of its chain. `Resolve(n)` works as follows:")
BUL(["Recompute `n`'s local matrix from its current parameters.",
     "Sources start a chain. **Object** gets kind *Model* and cumulative = identity. **Camera** gets kind *CameraPose* and cumulative = its base pose. **Projection** gets kind *Projection*.",
     "A transform resolves its upstream node first, then inherits its kind and sets `cumulative = local · upstream.cumulative`. This works after any source, including a Projection.",
     "A transform with nothing upstream starts a free-standing chain (kind *None*). A lone **Matrix** node is the typical case."])
P("Results are memoised with a `state` array: 0 unvisited, 1 in progress, 2 done. Meeting a node that is *in progress* means the user built a "
  "cycle, and that node is marked Invalid instead of recursing forever. The editor colours nodes by `kind`, so an invalid chain turns red.")

H2("5.3 Producing the EvalResult")
P("`Evaluate` resolves every node, then reads the three pins of the MVP Output:")
BUL(["**Model:** every linked chain becomes an `ObjectInstance` holding its `M`. An Object chain copies its object's shape and triangle corners. Any other chain, such as a lone Matrix, is drawn as a cube.",
     "**View:** any chain gives the pose `C`, so a Matrix node can be the camera. A singular `C` has no inverse, so the default camera at (0, 2, 7) looking at (0, 0.5, 0) is used instead, as it is when nothing is connected. Then `V = inverse(C)`.",
     "**Projection:** any chain gives `P`. Without one, `P` = identity (section 3.5). Near, far and the visible volume are then read back out of `P` (section 3.6).",
     "Anything unexpected adds a human-readable warning, shown above the node canvas."])
P("`Animate(dt)` adds `spinSpeed · dt` degrees to every Rotate node with *spin* on. `ResetGraph` replaces the graph with the starter example or a "
  "lone MVP Output. It keeps the id counters running, so imnodes never sees an old id reused for a new node.")

H2("5.4 Writing a camera pose back (SetCameraPose)")
P("Play mode needs the opposite of evaluation: given the pose `C'` the user flew to, change the graph so that it produces `C'`. "
  "The View chain is `C = T · L`, where `L` is the matrix of its first node and `T` the product of everything after it. "
  "`T` stays as it is, so only `L` has to change:")
EQ("L' = T<super>-1</super> · C'   with   T = C · L<super>-1</super>,   so   L' = L · C<super>-1</super> · C'")
P("The last form needs only `C` to be invertible, which evaluation already guarantees. How `L'` is stored depends on the first node:")
BUL(["**Matrix node:** `custom = L'`, exactly.",
     "**Camera node:** `position` is `L'`'s translation column and the view direction is `−L'`'s third column. The look-at target is placed along that "
     "direction at the old target distance (at least 1). Roll is dropped, because a position plus a look-at point cannot express it.",
     "**Nothing connected:** a new Camera node is created, linked to the View pin and given the pose.",
     "**Anything else first** (for example a Translate) can't express a free pose, so play mode stops with a message."])
NOTE("`AddNode` appends to a `std::vector`, which can move every node in memory. The code therefore keeps the Output node's *id* across that call, "
     "never a pointer. A unit test caught exactly this bug during development.")

# ---------------------------------------------------------------- 6  RENDERING
H1("6. Rendering infrastructure")
H2("6.1 Meshes")
P("Every mesh uses the same vertex layout, `Vertex { px, py, pz, r, g, b }`: six floats, 24 bytes. `Mesh::Upload` creates a vertex array "
  "object (VAO) and vertex buffer (VBO) once, describes attribute 0 (position, offset 0) and attribute 1 (colour, offset 12), and replaces the "
  "buffer contents on every call.")
TABLE(["Builder", "Draws as", "Notes"],
      [["`CubeVerts`", "triangles", "Unit cube centred at the origin, a different colour per face so orientation is readable."],
       ["`TriangleVerts(corners)`", "triangles", "A flat orange triangle from three corners. Each triangle object has its own corners, so the shared `triangle` mesh is re-uploaded (`GL_DYNAMIC_DRAW`) just before each triangle is drawn: three vertices, so it costs nothing."],
       ["`GridVerts(12)`", "lines", "Ground grid from −12 to 12. Each line is cut into 1-unit segments so it can *bend* when the divide warps it."],
       ["`AxesVerts`", "lines", "Red X, green Y, blue Z, 1.5 units long."],
       ["`NdcBoxVerts`", "lines", "The 12 edges of the cube [-1, 1]^3, plus two centre lines on its floor."],
       ["`FrustumVerts`", "lines", "The student camera's visible volume, in camera space, rebuilt every frame (below)."]], [38, 18, 114])
H3("The frustum mesh")
P("The visible volume is whatever `P` maps onto the NDC cube, so its eight corners are the cube's corners mapped back through the inverse:")
EQ("corner = P<super>-1</super> · (±1, ±1, ±1, 1),   then divided by its own w")
P("For a perspective `P` this is the familiar pyramid: at distance *d* the rectangle has half-height `d · tan(fov / 2)` and half-width `aspect` times that. "
  "For an orthographic matrix it is a box, for the identity the 2×2×2 box around the camera. Only a singular `P` falls back to the Projection node's fov, near and far.")
P("The near and far rectangles plus four connecting edges make 12 lines, the first 24 vertices (`kFrustumWarpable`). They are drawn with the "
  "perspective-divide warp enabled, and their corners have NDC coordinates of exactly ±1, so at full divide they land on the edges of the NDC cube. "
  "The remaining lines are drawn unwarped: the eye-to-near-plane lines, only when `P` is perspective, and a green \"up\" tick.")

H2("6.2 Offscreen framebuffers")
P("`ViewportFBO` owns a framebuffer with an RGB colour **texture** and a 24-bit depth **renderbuffer**. `Resize` reallocates both only when "
  "the window size changes. The texture is what ImGui later draws. OpenGL's texture origin is the bottom-left and ImGui's is the top-left, so "
  "`ShowViewport` passes UVs (0, 1) to (1, 0) to flip the image.")

H2("6.3 The Renderer")
P("There is a single shader program. `Renderer` caches its uniform locations in `Init`, then each view follows the same pattern:")
CODE("""renderer.Begin(fbo, viewSetup);     // bind + clear the target, upload per-view uniforms
renderer.Draw(mesh, model, opts);   // per draw: model matrix, tint, warp/dim switches
...
renderer.End();                     // unbind""")
TABLE(["ViewSetup field", "Meaning"],
      [["`obsView`, `obsProj`", "The camera *you* look through in this window: the orbit camera in views 1 and 2, the student's camera in view 3."],
       ["`toEye`", "Takes this view's scene space to the student's camera space. `V` when the scene is the world, identity when the scene already is camera space."],
       ["`camProj`", "The student's `P`, used for the warp and for the inside/outside test."],
       ["`warp`, `depthMode`, `boxHalf`, `boxCenter`", "Perspective-divide visualisation in view 2 (section 7)."],
       ["`dimOutside`, `clipOutside`", "Fade, or remove, what the student's camera cannot see."],
       ["`nearPlane`, `farPlane`, `bg`", "Used by the depth mode, the clip-behind test and the fade colour."]], [52, 118])
TABLE(["DrawOpts field", "Meaning"],
      [["`tint`, `tintAmount`", "Blend the mesh colour towards a flat colour: grey camera body, blue floor, fading NDC box."],
       ["`warp`", "Whether this draw follows the divide. Off for helpers such as the camera body and the floor."],
       ["`dim`", "Whether this draw is dimmed or clipped by the student's frustum. Off for axes, camera and frustum."]], [52, 118])
H2("6.4 Scene helpers")
P("`Scene` owns the shared meshes. `DrawWorld(renderer, scene, ev, worldToScene)` draws the grid, the axes and every object with model matrix "
  "`worldToScene · M`. Passing identity draws the world as-is (views 1 and 3); passing `V` draws it moved into camera space (view 2). That one "
  "parameter is the whole difference between \"what you think\" and \"what actually happens\". Two flags let view 2 skip the world grid and view 3 hide "
  "grid and axes. `DrawCameraGizmo` draws the camera body, its yellow lens and the frustum at `camToScene`: `C` in view 1, identity in view 2.")
H2("6.5 The orbit camera")
P("Views 1 and 2 are seen through an `OrbitCamera` that circles a target point using spherical coordinates (yaw *ψ*, pitch *φ*, distance *r*):")
EQ("eye = target + r · ( cos φ · sin ψ,  sin φ,  cos φ · cos ψ ),     view = lookAt(eye, target, up)")
P("Dragging changes ψ and φ by 0.4° per pixel, with φ clamped to ±89° so `lookAt` never looks straight along *up*. The mouse wheel multiplies "
  "*r* by `0.9^wheel`, so zoom feels the same at any distance.")

# ---------------------------------------------------------------- 7  SHADERS
story.append(PageBreak())
H1("7. The shaders, line by line")
P("All three views use one vertex shader and one fragment shader, embedded as strings in `Renderer.cpp`. Their job is more than ordinary "
  "rendering: they can also *animate* the perspective divide and show which parts of the world the student's camera can see.")
H2("7.1 Vertex shader")
CODE("""#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

uniform mat4 uModel;     // local -> scene space (the space the observer looks at)
uniform mat4 uToEye;     // scene space -> the student's camera eye space
uniform mat4 uCamProj;   // the student's projection matrix
uniform mat4 uObsView;   // observer camera of this viewport
uniform mat4 uObsProj;

uniform float uWarp;      // 0 = plain eye space, 1 = after dividing by w
uniform int   uDepthMode; // 0 = distance remapped linearly to [-1,1], 1 = real NDC z
uniform float uNear;
uniform float uFar;
uniform float uBoxHalf;   // the NDC cube [-1,1]^3 is drawn with this half size...
uniform float uBoxCenter; // ...centered this far in front of the camera

out vec3 vColor;
out vec4 vCamClip;
out float vEyeDist;

void main() {
    vec4 scene = uModel * vec4(aPos, 1.0);
    vec4 eye = uToEye * scene;
    vec4 clip = uCamProj * eye;

    vec3 pos = scene.xyz;
    if (uWarp > 0.0) {
        float w = max(clip.w, 1e-3);
        vec3 ndc = clip.xyz / w;
        float zn = (uDepthMode == 0) ? 2.0 * (-eye.z - uNear) / (uFar - uNear) - 1.0 : ndc.z;
        vec3 projected = vec3(ndc.x, ndc.y, 0.0) * uBoxHalf
                       + vec3(0.0, 0.0, -(uBoxCenter + zn * uBoxHalf));
        projected = clamp(projected, vec3(-300.0), vec3(300.0));
        pos = mix(scene.xyz, projected, uWarp);
    }

    gl_Position = uObsProj * uObsView * vec4(pos, 1.0);
    vColor = aColor;
    vCamClip = clip;
    vEyeDist = -eye.z;
}""", "Renderer.cpp: kVertSrc (comments shortened)")
P("**Two pipelines in one shader.** Each vertex is pushed through two cameras at once:")
BUL(["**The student's camera:** `scene → eye → clip` using `uToEye` and `uCamProj`. This result is never drawn directly. It is used to decide "
     "what that camera can see, and to compute the warp.",
     "**The observer camera:** `uObsProj · uObsView` turns the (possibly warped) scene position into `gl_Position`, which the GPU rasterises."])
P("In view 3 the observer *is* the student camera (`uObsView = V`, `uObsProj = P`), so `gl_Position = P · V · M · v`, the textbook pipeline. "
  "In views 1 and 2 the observer is the orbit camera, so you see the student's camera from outside.")
H3("The warp: animating the divide")
P("Only view 2 sets `uWarp > 0`, and there the scene space *is* the camera space. The shader computes the real NDC position `ndc = clip.xyz / w`, "
  "then places the NDC cube back in front of the fixed camera so you can see it in 3D:")
EQ("projected = ( h · x<sub>ndc</sub>,  h · y<sub>ndc</sub>,  −(c + h · z<sub>n</sub>) )")
P("with `h = uBoxHalf` = 4 and `c = uBoxCenter = near + h`. NDC z = −1 lands at distance `c − h = near`, on the real near plane, and z = +1 at "
  "`near + 2h`. Because `x_ndc = (g/a) · x / d`, the drawn x is `h · (g/a) · x / d`. That `1 / d` is what visibly shrinks the far objects as the slider moves.")
P("`mix(scene, projected, uWarp)` interpolates each vertex linearly between where it was and where the divide puts it, so the slider animates "
  "smoothly. Straight lines bend while warping because each vertex moves by a different amount, which is why the grid is built from short segments.")
TABLE(["Depth mode", "z<sub>n</sub>", "Why"],
      [["0: distance, spread evenly", "`2 (d − n) / (f − n) − 1`", "Linear in distance. Easy to read: equally spaced objects stay equally spaced."],
       ["1: real NDC z", "`ndc.z`", "What the depth buffer really stores. Everything crowds towards the far face (table in 3.3)."]], [44, 46, 80])
P("Two safety measures: `w` is clamped to at least 0.001, so points behind the camera do not divide by zero. The result is clamped to ±300, so "
  "those points do not shoot off to infinity. Such points are hidden anyway by the fragment shader's clip-behind test.")
H3("Outputs")
P("`vCamClip` passes the *student's* clip coordinates to the fragment shader, and `vEyeDist` the distance in front of the student's camera.")

story.append(CondPageBreak(95 * mm))
H2("7.2 Fragment shader")
CODE("""uniform vec3  uTint;
uniform float uTintAmount;
uniform int   uDimOutside;   // fade what the student's camera cannot see
uniform int   uClipBehind;   // drop fragments in front of the near plane
uniform int   uClipOutside;  // drop everything outside the [-1,1] cube
uniform float uNear;
uniform vec3  uBg;

void main() {
    if (uClipBehind == 1 && vEyeDist < uNear) discard;
    vec3 c = mix(vColor, uTint, uTintAmount);
    bool inside = vCamClip.w > 0.0 &&
                  all(lessThanEqual(abs(vCamClip.xyz), vec3(vCamClip.w)));
    if (uClipOutside == 1 && !inside) discard;
    if (uDimOutside == 1 && !inside) c = mix(c, uBg, 0.72);
    FragColor = vec4(c, 1.0);
}""", "Renderer.cpp: kFragSrc")
BUL(["**Clip behind:** while warping, anything closer than the near plane would project to nonsense, so it is discarded.",
     "**Tint:** `mix(colour, tint, amount)` is how the camera body turns grey, the floor blue, and the NDC box fades in. Its tint is the background colour, with amount `1 − warp`.",
     "**Inside test:** exactly the GPU clipping rule from section 3.4, `|x|, |y|, |z| ≤ w` with `w > 0`, evaluated per pixel on the interpolated "
     "clip coordinates. The clip coordinates are a linear function of position, so the interpolated value is the correct value at that surface point.",
     "**Dim or cut:** outside points are blended 72% towards the background, or removed when *cut away outside [-1, 1]* is on. Removal is what the real GPU does."])

# ---------------------------------------------------------------- 8  VIEWS
H1("8. The three views")
P("Each view is a struct in `Views.cpp` with a `Draw(ctx, open)` method. `FrameContext` bundles what all of them need: the renderer, the scene, "
  "this frame's `EvalResult`, the options, the graph and `dt` (the last two for play mode). All three follow the same steps: begin the ImGui "
  "window, size the framebuffer to the free space, fill a `ViewSetup`, draw, show the texture, then draw the overlays.")
TABLE(["", "1. Think", "2. Actual", "3. Camera"],
      [["Scene space", "world", "student camera space", "world"],
       ["Objects drawn with", "`M`", "`V · M`", "`M`"],
       ["`toEye`", "`V`", "identity", "`V`"],
       ["Observer (`obsView`, `obsProj`)", "orbit camera", "orbit camera", "`V`, `P` (the real camera)"],
       ["Student camera drawn at", "`C`", "identity (origin)", "not drawn"],
       ["Floor grid", "world grid", "fixed floor under the camera", "world grid, toggle"],
       ["Warp", "never", "slider", "never"]], [46, 34, 50, 40])
H2("8.1 What you think happens")
P("The world is drawn untouched (`worldToScene` = identity) and the camera gizmo is drawn at its pose `C`. Because `toEye = V`, the shader can still "
  "tell which parts of the world the student camera sees, and dims the rest. The overlay prints `C`'s fourth column, the camera position.")
H2("8.2 What actually happens")
P("Here the scene *is* camera space. The world is drawn with `worldToScene = V`, so every object gets `V · M`, and the camera gizmo is drawn at "
  "the identity: at the origin, looking down −z, never moving. The world's own grid is not drawn. Instead a blue floor is drawn at a fixed "
  "position under the camera, so the only thing that visibly moves is the world.")
P("The NDC cube is drawn with `cubeToEye = T(0, 0, −c) · S(h, h, −h)`, which maps cube coordinates to the same place the shader sends NDC "
  "points. The −h in z makes cube z = −1 the face nearest the camera. It fades in with the slider. With *ghost* on, the world is drawn a second "
  "time unwarped in wireframe (`glPolygonMode(GL_LINE)`) so you can compare before and after.")
IMG("divide_strip.png", 170, "View 2 with the divide at 0, 0.5 and 1. The frustum (yellow) turns into the white NDC cube, and the triangle at the "
    "back shrinks far more than the cube at the front.")
P("With no projection there is nothing to divide, so the slider is disabled, `cubeToEye` is the identity, and the cube shown is the real 2×2×2 "
  "box around the camera. Text labels at 3D points use `Label3D`, which projects a point with the observer's matrices and converts NDC to pixels:")
EQ("pixel<sub>x</sub> = left + (x<sub>ndc</sub> · 0.5 + 0.5) · width,     pixel<sub>y</sub> = top + (0.5 − y<sub>ndc</sub> · 0.5) · height")
P("The y term is flipped because NDC y points up while screen y points down. Points behind the observer (*w* ≤ 0) or off-screen are skipped.")
H2("8.3 What the camera sees")
P("The observer is the student's camera itself: `obsView = V`, `obsProj = P`. Nothing is warped or dimmed. The GPU does the real divide, clipping "
  "and depth test. This window also measures its own aspect ratio, which `main.cpp` feeds back into every Projection node before the next "
  "evaluation, so the image is never stretched (one frame of lag, invisible in practice).")
IMG("think_camera.png", 150, "View 1 (left) shows the camera and its frustum in the world. View 3 (right) is the image that camera produces.")
H2("8.4 Play mode inside")
P("`CameraView::Fly` runs every frame while playing. It starts from the current pose `C` and turns it into a position and a direction:")
EQ("pos = C[3],    fwd = −normalize(C[2]),    yaw = atan2(fwd<sub>x</sub>, −fwd<sub>z</sub>),    pitch = asin(fwd<sub>y</sub>)")
BUL(["**Look:** while the image is dragged, yaw and pitch change by 0.004 radians per pixel. Pitch is clamped to ±89° so `lookAt` never looks straight up or down. "
     "The direction is rebuilt as `fwd = (cos p · sin y, sin p, −cos p · cos y)`.",
     "**Move:** `right = normalize(fwd × up)`. W/S add ±fwd, A/D ±right, Q/E ±world up. The sum is normalised, so diagonal moves aren't faster, then scaled by `speed · dt`, times 3 with Shift.",
     "**Write back:** the new pose `inverse(lookAt(pos, pos + fwd, up))` goes to `NodeGraph::SetCameraPose` (section 5.4). The next frame's evaluation turns the edited nodes back into `C`, so every view and the Matrices panel follow."])
P("Keys only act while the camera window has focus, and the Play button gives it focus. Esc stops play mode. `main.cpp` skips `Animate` while "
  "playing, otherwise a spinning node after the Camera would keep turning the view.")

# ---------------------------------------------------------------- 9  UI
H1("9. The user interface")
H2("9.1 Node editor")
P("The editor is built with **imnodes**, which, like ImGui, is immediate mode. Every frame `DrawNodeEditor` calls `BeginNodeEditor`, emits each "
  "node (title bar, input pins, widgets, matrix readout, output pin) and each link, then `EndNodeEditor`. Only after `EndNodeEditor` can it ask "
  "what the user did: `IsLinkCreated`, `IsLinkDestroyed`, hovered node or link, selection.")
BUL(["**Adding:** the toolbar buttons and the right-click menu call `AddNode` and place the node with `SetNodeScreenSpacePos`. Toolbar nodes appear in the middle of the canvas, offset in a small cascade so they don't stack exactly. Names get a counter (\"Translate 2\").",
     "**Deleting:** the x button, the Delete or Backspace key, and the menus only *queue* ids. Deletion happens after `EndNodeEditor`, so imnodes never holds stale references during the frame. Removing a node also removes its links.",
     "**Duplicating:** copies the whole `Node` struct, then gives it a new id and name. Links are not copied.",
     "**Colours:** title bars are coloured by type for sources and by chain `kind` for transforms.",
     "**Input contrast:** field backgrounds are pushed a little lighter inside the editor so they stand out on the node bodies."])
H3("The Matrix and triangle editors")
IMGROW([("matrix_node.png", 62), ("tri_node.png", 62)],
       "Left: a Matrix node holding an orthographic projection. Right: an Object node set to Triangle, with its three corners.")
P("GLM stores matrices column by column (`m[col][row]`), but people write them row by row. `DrawMatrixEditor` therefore shows four "
  "`DragFloat4` rows: row *r* is copied out as `(m[0][r], m[1][r], m[2][r], m[3][r])`, edited, and written back to the same places. "
  "*Identity*, *Transpose* and *Invert* call the GLM functions. *Invert* is disabled when `|det| < 10⁻⁶`, and the determinant is printed under the buttons.")
P("When an Object's shape is Triangle, three `DragFloat3` fields edit `tri[0..2]` directly. *Reset triangle* copies the corners from a default-constructed `Node`.")
H2("9.2 Docking and windows")
P("The app uses the *docking* branch of Dear ImGui (v1.90.9-docking). `DockSpaceOverViewport` turns the whole main window into a dock area. "
  "On the first frame, if no saved layout exists, `BuildDefaultLayout` uses the DockBuilder API to split it: 48% bottom for the editor, the top in thirds "
  "for the views, and 24% of the bottom on the right for Settings and Matrices as tabs. ImGui saves whatever the user arranges to `imgui.ini`.")
P("**Multi-viewports** (`ImGuiConfigFlags_ViewportsEnable`) let a window be dragged out of the app to become a real operating-system window. "
  "ImGui's GLFW backend creates each such window with an OpenGL context that *shares* objects with the main one, so the views' textures can be "
  "shown there with no extra work. Our own drawing happens while the main context is current. After drawing the extra windows, `main.cpp` makes "
  "the main context current again. This ImGui version cannot position windows under Wayland, so on Linux GLFW is asked for X11 (XWayland on "
  "a Wayland desktop).")
P("`ConfigWindowsMoveFromTitleBarOnly` is on, so dragging inside a 3D view orbits it (or looks around in play mode) instead of moving the window. "
  "Each view's image is covered by an `InvisibleButton` that reports dragging and hovering.")
H2("9.3 Options, theme and text")
P("`Options` (UI.h) is a plain struct of toggles shared by Settings, the node editor and the views: animation, dimming, matrix readouts, the "
  "divide controls, the camera view's grid, and the two text switches. *Explanations* (H) hides the descriptions and overlay boxes. *Labels* (L) hides the text drawn at 3D points.")
P("`ApplyDarkTheme` sets ImGui's spacing, rounding and every colour from a small palette with one accent blue (`0x6C8CFF`), and themes imnodes to "
  "match. `LoadFonts` loads Roboto for the UI and Cousine, a monospace font, for matrices, both shipped with Dear ImGui and copied next to the "
  "executable by CMake. Sizes are multiplied by the monitor's content scale for high-DPI screens.")

# ---------------------------------------------------------------- 10  EXTENDING
H1("10. Extending the app")
TABLE(["To add…", "Change"],
      [["A transform node", "Add it to `NodeType` and `IsTransform`, give it a matrix in `Node::RecomputeLocal`, add a toolbar entry in `kTransformKinds` and its widgets in the `switch` inside `DrawNodeEditor` (UI.cpp)."],
       ["An object shape", "Write a builder next to `CubeVerts` (Renderer.cpp), add a `Mesh` to `Scene` and upload it in `Scene::Init`, add a value to `ObjectShape`, pick the mesh in `DrawWorld`, and add the name to the shape combo in `DrawNodeEditor`. Per-object vertex data, like the triangle's corners, goes in `Node` and `ObjectInstance`."],
       ["A projection type", "Usually no code: type the matrix into a Matrix node. For a dedicated node, add parameters to `Node` and build the matrix in `RecomputeLocal`. The views derive the volume from P on their own."],
       ["A window", "Title and visibility flag in UI.h, a slot in `BuildDefaultLayout`, a View-menu entry and a draw call in `main.cpp`."],
       ["A shader effect", "New uniform in the shader strings, its location in `Renderer::Init`, a field in `ViewSetup` (per view) or `DrawOpts` (per draw), and upload it in `Begin` or `Draw`."],
       ["A setting", "A field in `Options`, a widget in `DrawSettingsWindow`, and read it wherever it applies."],
       ["This guide", "Edit `docs/guide/make_guide.py` and run it. Screenshots live in `docs/guide/fig/`."]], [36, 134])
H2("Glossary")
TABLE(["Term", "Meaning"],
      [["Homogeneous coordinates", "(x, y, z, w). Points use w = 1 so translation fits in a 4×4 matrix. After projection w holds the depth to divide by."],
       ["Pose C", "The camera's placement in the world, camera → world."],
       ["View matrix V", "`C^-1`: world → camera. Moves the world so the camera sits at the origin looking down −z."],
       ["Clip space", "Output of `P`, before the divide. Clipping happens here: |x|, |y|, |z| ≤ w."],
       ["NDC", "Normalised device coordinates: clip / w. The visible volume is the cube [-1, 1]^3."],
       ["Frustum", "The truncated pyramid a perspective camera sees, between the near and far planes."],
       ["Determinant", "A single number from a matrix. Zero means the matrix squashes space flat and has no inverse."],
       ["FBO", "Framebuffer object: an offscreen render target. Each view renders into one."],
       ["Immediate mode", "A UI style where the whole interface is re-declared every frame by function calls (ImGui, imnodes)."]], [44, 126])

# ---------------------------------------------------------------- build
doc = BaseDocTemplate(OUT, pagesize=A4, leftMargin=20 * mm, rightMargin=20 * mm, topMargin=18 * mm, bottomMargin=22 * mm,
                      title="MVP Visualizer: how it works", author="MVP Visualizer project", subject="User guide and code guide")
doc.addPageTemplates([PageTemplate(id="p", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")],
                                   onPage=on_page)])
doc.build(story)
print("wrote", os.path.abspath(OUT))
