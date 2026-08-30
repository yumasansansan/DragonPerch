#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render the application icon from KDE's Konqi.svg.

Two outputs, because the two platforms ask for different things:

  packaging/dragonperch.ico   Windows. Several sizes in one file, because the shell picks
                              between them -- the tray asks for a small one, Alt-Tab and
                              the task manager for larger ones, and letting Windows scale
                              a single 256 down to 16 produces a smear.
  packaging/dragonperch.png   Linux. StatusNotifierItem can name a themed icon, but a
                              themed name only resolves once the icon is installed, and
                              the tarball is meant to run unpacked -- so the pixels travel
                              with the program and go over D-Bus as IconPixmap.

The head and shoulders rather than the whole figure: at sixteen pixels a full one is
four green smudges, and what makes Konqi recognisable that small is the head.

Usage:
    python tools/make_icons.py --svg assets/konqi/source/Konqi.svg --out packaging
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from make_konqi_pack import DROP_LABELS, INKSCAPE_NS, inkscape_binary, strip  # noqa: E402

import xml.etree.ElementTree as ET  # noqa: E402

# Windows picks from these by size. 256 is what Explorer wants for a large view; 16 and 20
# are what the tray and the title bar actually use, and are the ones worth looking at.
ICO_SIZES = [16, 20, 24, 32, 48, 64, 128, 256]

LINUX_SIZE = 64

# The head, as a fraction of the drawing's bounding box: left, top, right, bottom. Measured
# by eye off a grid over the rendered figure. It does not need to be exact, only to hold the
# head and both ears -- the horns are distinctive but reach far enough right that including
# them would shrink the face, and the face is what survives sixteen pixels.
HEAD_BOX = (0.03, 0.08, 0.62, 0.50)


def render(inkscape: str, svg: Path, box, size: int, out_png: Path) -> None:
    x, y, w, h = box
    area = f"{x:.3f}:{y:.3f}:{x + w:.3f}:{y + h:.3f}"

    subprocess.run([inkscape, "--export-type=png", f"--export-filename={out_png}",
                    f"--export-area={area}", f"--export-width={size}",
                    f"--export-height={size}", "--export-background-opacity=0", str(svg)],
                   capture_output=True, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--svg", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    inkscape = inkscape_binary()
    args.out.mkdir(parents=True, exist_ok=True)

    source = ET.parse(args.svg)
    strip(source.getroot(), DROP_LABELS)

    with tempfile.NamedTemporaryFile("wb", suffix=".svg", delete=False) as handle:
        source.write(handle, encoding="utf-8", xml_declaration=True)
        stripped = Path(handle.name)

    try:
        out = subprocess.run([inkscape, "-X", "-Y", "-W", "-H", str(stripped)],
                             capture_output=True, text=True, check=True).stdout.split()
        x, y, width, height = (float(v) for v in out[:4])

        left, top, right, bottom = HEAD_BOX
        head = (x + width * left, y + height * top,
                width * (right - left), height * (bottom - top))

        # Squared off around the head's centre, so the icon is not stretched.
        side = max(head[2], head[3])
        box = (head[0] + head[2] / 2 - side / 2, head[1] + head[3] / 2 - side / 2, side, side)
        print(f"head box {box[0]:.0f},{box[1]:.0f} {box[2]:.0f}x{box[3]:.0f}")

        with tempfile.TemporaryDirectory() as scratch:
            largest = Path(scratch) / "icon.png"
            render(inkscape, stripped, box, max(ICO_SIZES), largest)

            master = Image.open(largest).convert("RGBA")

            ico = args.out / "dragonperch.ico"
            master.save(ico, sizes=[(n, n) for n in ICO_SIZES])
            print(f"wrote {ico} ({', '.join(str(n) for n in ICO_SIZES)})")

            png = args.out / "dragonperch.png"
            master.resize((LINUX_SIZE, LINUX_SIZE), Image.LANCZOS).save(png)
            print(f"wrote {png} ({LINUX_SIZE}x{LINUX_SIZE})")
    finally:
        stripped.unlink(missing_ok=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
