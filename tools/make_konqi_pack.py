#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a DragonPerch sprite pack from KDE's Konqi.svg.

KDE publishes Konqi as single illustrations; there is no walk cycle anywhere in the
material.  https://community.kde.org/Promo/Material/Mascots

So the animation is made here, out of the one pose, by transforming it:

  - the whole figure squashes and stretches vertically about its feet, which is what
    reads as a bouncy step at this size
  - the tail swings, which is what stops it looking like a statue being jiggled

At 48 pixels tall Konqi's legs are about three pixels, so animating them would not be
visible even if the SVG separated them, which it does not -- `body` includes the feet.
Squash and tail are the parts of the motion that survive the resolution.

Usage:
    python tools/make_konqi_pack.py --svg Konqi.svg --out assets/konqi
"""

from __future__ import annotations

import argparse
import copy
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image

SVG_NS = "http://www.w3.org/2000/svg"
INKSCAPE_NS = "http://www.inkscape.org/namespaces/inkscape"
SODIPODI_NS = "http://sodipodi.sourceforge.net/DTD/sodipodi-0.0.dtd"

ET.register_namespace("", SVG_NS)
ET.register_namespace("inkscape", INKSCAPE_NS)
ET.register_namespace("sodipodi", SODIPODI_NS)

TAIL_LABELS = {"tail", "tail-fill", "tail-spikes", "under-tail"}

# The cast shadow is a soft grey ellipse meant for a page. On a desktop it would be a smudge
# on whatever the pet is standing on, and being the lowest thing in the drawing it also put
# the sprite's bottom edge -- and so the anchor -- below Konqi's feet, leaving him hovering.
DROP_LABELS = {"cast-shadow"}

# The K banner Konqi carries.
#
# Konqi is drawn facing left, so the right-facing half of the sheet is the whole cell
# mirrored -- and that would leave a backwards K. The K is KDE's own mark rather than
# decoration, so rendering it wrong is not an option and neither is dropping it.
#
# So each frame is rendered twice: once with the K hidden, once with nothing but the K.
# The right-facing cell is the mirrored body with the unmirrored K laid back on top, which
# is what animators do with lettering on a flipped sprite.
#
# Note that this only works because the sheet carries both directions. Mirroring at draw
# time cannot be fixed this way: a pet walks to the end of a title bar and comes back, so it
# spends half its life facing each way, and whichever half the sheet was not drawn for would
# show the K backwards.
K_LABELS = {"K", "K-shadow1", "K-shadow2", "K-shadow3"}

# One entry per cell of the atlas: (vertical scale about the feet, tail angle in degrees).
#
# Four walk frames making one cycle, then the two poses the simulation needs that are not
# walking.  The tail leads the bounce by a quarter cycle, which is what keeps the two
# motions from looking like one.
FRAMES = [
    ("walk", 1.00, -5.0),
    ("walk", 0.96, 0.0),
    ("walk", 1.00, 5.0),
    ("walk", 0.96, 0.0),
    ("idle", 1.00, 0.0),
    ("fall", 1.03, 12.0),  # stretched and tail up: falling reads as being pulled downward
]


def inkscape_binary() -> str:
    for candidate in (
        shutil.which("inkscape"),
        r"C:\Program Files\Inkscape\bin\inkscape.exe",
        "/usr/bin/inkscape",
    ):
        if candidate and Path(candidate).exists():
            return candidate
    sys.exit("inkscape not found; it is what renders the SVG")


def drawing_box(inkscape: str, svg: Path) -> tuple[float, float, float, float]:
    out = subprocess.run([inkscape, "-X", "-Y", "-W", "-H", str(svg)],
                         capture_output=True, text=True, check=True).stdout.split()
    x, y, w, h = (float(v) for v in out[:4])
    return x, y, w, h


def strip(root: ET.Element, labels: set[str]) -> int:
    """Removes labelled elements. ElementTree has no parent pointers, so this walks."""
    removed = 0
    for parent in root.iter():
        for child in list(parent):
            if child.get(f"{{{INKSCAPE_NS}}}label") in labels:
                parent.remove(child)
                removed += 1
    return removed


def keep_only(root: ET.Element, labels: set[str]) -> None:
    """Removes everything that is not one of the labelled elements, inside or above one."""
    parent_of = {child: parent for parent in root.iter() for child in parent}

    keep = {root}
    for element in root.iter():
        if element.get(f"{{{INKSCAPE_NS}}}label") in labels:
            keep.update(element.iter())
            node = parent_of.get(element)
            while node is not None:
                keep.add(node)
                node = parent_of.get(node)

    for parent in list(root.iter()):
        for child in list(parent):
            if child not in keep:
                parent.remove(child)


def find_layer(root: ET.Element) -> ET.Element:
    """The group everything lives in, so one transform moves the whole figure."""
    for element in root.iter(f"{{{SVG_NS}}}g"):
        return element
    sys.exit("no group found in the SVG; it is not the file this script expects")


def apply_transform(element: ET.Element, transform: str) -> None:
    existing = element.get("transform")
    element.set("transform", f"{transform} {existing}" if existing else transform)


def tail_pivot(inkscape: str, svg: Path, tail_ids: list[str]) -> tuple[float, float]:
    """Where the tail meets the body: the top of the tail's bounding box, at its centre.

    Rotating about the middle of the tail would tear it off the body; about its top it
    swings the way a tail does.
    """
    boxes = []
    for tail_id in tail_ids:
        out = subprocess.run([inkscape, "--query-id", tail_id, "-X", "-Y", "-W", "-H", str(svg)],
                             capture_output=True, text=True, check=False).stdout.split()
        if len(out) >= 4:
            boxes.append(tuple(float(v) for v in out[:4]))

    if not boxes:
        sys.exit("could not measure the tail")

    left = min(b[0] for b in boxes)
    top = min(b[1] for b in boxes)
    right = max(b[0] + b[2] for b in boxes)
    return ((left + right) / 2.0, top)


def export_area(box: tuple[float, float, float, float]) -> tuple[float, float, float, float]:
    """The same rectangle for every frame, so the cells line up. Padded above, because a
    stretched frame reaches higher than the original drawing."""
    x, y, w, h = box
    pad = h * 0.05
    return x, y - pad, x + w, y + h


def render_frame(inkscape: str, source: ET.ElementTree, tail_ids: list[str], box, pivot,
                 scale_y: float, tail_degrees: float, height: int, out_png: Path,
                 part: str) -> None:
    """Renders one frame. `part` is "body" for everything but the K, or "k" for the K alone;
    the two line up pixel for pixel, because the transforms and the export area are the same."""
    tree = copy.deepcopy(source)
    root = tree.getroot()

    if part == "k":
        keep_only(root, K_LABELS)
    else:
        strip(root, K_LABELS)


    if tail_degrees:
        for element in root.iter():
            if element.get(f"{{{INKSCAPE_NS}}}label") in TAIL_LABELS:
                apply_transform(element, f"rotate({tail_degrees},{pivot[0]},{pivot[1]})")

    if scale_y != 1.0:
        # About the feet, so the character never floats off the surface it stands on.
        bottom = box[1] + box[3]
        apply_transform(find_layer(root),
                        f"translate(0,{bottom * (1.0 - scale_y)}) scale(1,{scale_y})")

    with tempfile.NamedTemporaryFile("wb", suffix=".svg", delete=False) as handle:
        tree.write(handle, encoding="utf-8", xml_declaration=True)
        temp = Path(handle.name)

    try:
        ax0, ay0, ax1, ay1 = export_area(box)
        area = f"{ax0:.3f}:{ay0:.3f}:{ax1:.3f}:{ay1:.3f}"

        subprocess.run([inkscape, "--export-type=png", f"--export-filename={out_png}",
                        f"--export-area={area}", f"--export-height={height}",
                        "--export-background-opacity=0", str(temp)],
                       capture_output=True, check=True)
    finally:
        temp.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--svg", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--height", type=int, default=52,
                        help="cell height in pixels, including the padding above")
    args = parser.parse_args()

    inkscape = inkscape_binary()
    args.out.mkdir(parents=True, exist_ok=True)

    source = ET.parse(args.svg)
    dropped = strip(source.getroot(), DROP_LABELS)
    print(f"dropped {dropped} element(s): {', '.join(sorted(DROP_LABELS))}")

    tail_ids = [element.get("id") for element in source.getroot().iter()
                if element.get(f"{{{INKSCAPE_NS}}}label") in TAIL_LABELS and element.get("id")]

    # Measured on the stripped drawing, not the original: the bounding box has to end at
    # Konqi's feet for the sprite's bottom edge to be where he stands.
    with tempfile.NamedTemporaryFile("wb", suffix=".svg", delete=False) as handle:
        source.write(handle, encoding="utf-8", xml_declaration=True)
        stripped = Path(handle.name)

    try:
        box = drawing_box(inkscape, stripped)
        pivot = tail_pivot(inkscape, stripped, tail_ids)
    finally:
        stripped.unlink(missing_ok=True)

    print(f"drawing {box[2]:.0f}x{box[3]:.0f}, tail pivot ({pivot[0]:.1f}, {pivot[1]:.1f})")

    with tempfile.TemporaryDirectory() as scratch:
        # Both directions, right-facing cells first. The renderer never mirrors this pack.
        right, left = [], []
        for index, (name, scale_y, tail_degrees) in enumerate(FRAMES):
            parts = {}
            for part in ("body", "k"):
                png = Path(scratch) / f"{index}-{part}.png"
                render_frame(inkscape, source, tail_ids, box, pivot, scale_y, tail_degrees,
                             args.height, png, part)
                parts[part] = Image.open(png).convert("RGBA")

            body, k = parts["body"], parts["k"]

            facing_left = body.copy()
            facing_left.alpha_composite(k)
            left.append(facing_left)

            # The mirrored body, with the K put back the way round it was drawn. Pasting
            # only the K's own pixels -- rather than flipping the rectangle it sits in --
            # is what keeps the bandana underneath from being flipped along with it.
            facing_right = body.transpose(Image.FLIP_LEFT_RIGHT)
            bounds = k.getbbox()
            if bounds is None:
                sys.exit("the K rendered empty; the labels in the SVG must have changed")
            facing_right.alpha_composite(k.crop(bounds),
                                         (facing_right.width - bounds[2], bounds[1]))
            right.append(facing_right)

        cells = right + left
        print(f"  {len(right)} frames each way")

        width = max(cell.width for cell in cells)
        atlas = Image.new("RGBA", (width * len(cells), args.height), (0, 0, 0, 0))
        for index, cell in enumerate(cells):
            # Bottom aligned: the anchor is the feet, and every frame has to put them on
            # the same row or the pet bobs when it should be still.
            atlas.paste(cell, (index * width + (width - cell.width) // 2,
                               args.height - cell.height))

    atlas_path = args.out / "konqi.png"
    atlas.save(atlas_path)
    print(f"wrote {atlas_path} ({atlas.width}x{atlas.height})")

    ini = f"""; Generated by tools/make_konqi_pack.py from KDE's Konqi.svg.
; Do not edit by hand -- rerun the script instead.
;
; Artwork: Konqi by Tyson Tan, vectorised by Franco Perez, CC-BY-SA-4.0.
; https://community.kde.org/Promo/Material/Mascots

[pack]
id = konqi
name = Konqi
artwork-licence = CC-BY-SA-4.0
attribution = Konqi by Tyson Tan, vectorised by Franco Perez (CC BY-SA 4.0)
atlas = konqi.png
frame-width = {width}
frame-height = {args.height}

; Cells 0-5 face right, 6-11 face left. Both are drawn rather than mirrored at draw time,
; because Konqi carries KDE's K and a mirrored K is a backwards K.

[walk]
frames = 0, 1, 2, 3
frames-left = 6, 7, 8, 9
duration = 140

[idle]
frames = 4
frames-left = 10
duration = 400

[turn]
frames = 4
frames-left = 10
duration = 350
loop = false

[land]
frames = 1
frames-left = 7
duration = 200
loop = false

[fall]
frames = 5
frames-left = 11
duration = 200

[fly]
frames = 5
frames-left = 11
duration = 200
"""

    ini_path = args.out / "konqi.ini"
    ini_path.write_text(ini, encoding="utf-8")
    print(f"wrote {ini_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
