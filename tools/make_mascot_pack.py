#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a DragonPerch sprite pack from one of KDE's mascot illustrations.

Konqi has an SVG source, so `make_konqi_pack.py` can take him apart and swing his tail.
Katie and Kori do not: KDE publishes them as flat PNGs.
https://community.kde.org/Promo/Material/Mascots

So this builds the animation the only way a single raster allows -- by transforming the
whole figure:

  - it squashes and stretches vertically about the feet, which is what reads as a bouncy
    step at this size
  - it leans a degree or two either side of upright, which stands in for the tail swing
    that the SVG pipeline gets for free

Two details the flat source forces:

  - the sparkles drawn around each mascot are separate from the figure, and a sparkle
    hanging in the air next to a window is a rendering bug rather than a flourish, so
    everything not connected to the body is dropped
  - Kori wears KDE's K on his scarf. A mirrored K is a backwards K, so his K is lifted off
    the artwork by colour, the hole behind it filled in, and the K laid back on unmirrored.
    Katie carries no lettering and so is mirrored at draw time like any other sprite.

Usage:
    python tools/make_mascot_pack.py --png Mascot-kori-kde30logo-kori.png \\
        --id kori --out assets/kori
"""

from __future__ import annotations

import argparse
import sys
from math import ceil, radians, tan
from pathlib import Path

import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

# One entry per cell: (vertical scale about the feet, lean in degrees).
#
# The same four-frame walk cycle as Konqi, so the two packs move alike, then the two poses
# the simulation needs that are not walking. The lean leads the bounce by a quarter cycle,
# which is what keeps the two motions from looking like one.
FRAMES = [
    (1.00, -2.5),
    (0.96, 0.0),
    (1.00, 2.5),
    (0.96, 0.0),
    (1.00, 0.0),   # idle
    (1.03, 6.0),   # fall: stretched and leaning, which reads as being pulled downward
]

# Where each mascot's lettering is, as a fraction of the trimmed figure, and how white it
# is. The box only has to be tight enough to exclude the eye highlight, which is the other
# near-white thing in the drawing.
LETTERING = {
    "kori": {"region": (0.20, 0.35, 0.60, 0.75), "minimum": 195, "spread": 32},
}

MASCOTS = {
    "katie": "Katie",
    "kori": "Kori",
    "konqi": "Konqi",
}

WORKING_HEIGHT = 1024


def largest_component(mask: np.ndarray) -> np.ndarray:
    """The biggest 4-connected blob of `mask`.

    Labels by propagating each pixel's index to its neighbours until nothing changes. That
    is more passes than a union-find would need, but it is a dozen lines of array code
    rather than a dependency, and the images here are a megapixel at most.
    """
    labels = np.where(mask, np.arange(mask.size).reshape(mask.shape), 0)

    while True:
        merged = labels.copy()
        merged[1:, :] = np.maximum(merged[1:, :], labels[:-1, :])
        merged[:-1, :] = np.maximum(merged[:-1, :], labels[1:, :])
        merged[:, 1:] = np.maximum(merged[:, 1:], labels[:, :-1])
        merged[:, :-1] = np.maximum(merged[:, :-1], labels[:, 1:])
        merged *= mask

        if np.array_equal(merged, labels):
            break
        labels = merged

    found = labels[mask]
    if found.size == 0:
        return mask
    values, counts = np.unique(found, return_counts=True)
    return labels == values[counts.argmax()]


def drop_loose_parts(image: Image.Image) -> tuple[Image.Image, int]:
    """Keeps only what is joined to the figure, which drops the sparkles."""
    pixels = np.asarray(image).copy()
    solid = pixels[..., 3] > 24

    body = largest_component(solid)
    removed = int(solid.sum() - body.sum())

    pixels[..., 3] = np.where(body, pixels[..., 3], 0)
    return Image.fromarray(pixels), removed


def letter_mask(image: Image.Image, rule: dict) -> np.ndarray:
    """The lettering, lifted off the artwork by colour."""
    pixels = np.asarray(image).astype(int)
    rgb, alpha = pixels[..., :3], pixels[..., 3]

    white = (alpha > 200) & (rgb.min(2) > rule["minimum"]) & (np.ptp(rgb, 2) < rule["spread"])

    left, top, right, bottom = rule["region"]
    inside = np.zeros_like(white)
    inside[int(top * image.height):int(bottom * image.height),
           int(left * image.width):int(right * image.width)] = True

    found = largest_component(white & inside)
    if found.sum() < 64:
        sys.exit("could not find the lettering; the region or the colour rule is wrong")
    return found


def fill_behind(image: Image.Image, hole: np.ndarray) -> Image.Image:
    """Paints over `hole` with the colours around it, nearest first.

    The lettering is not symmetric, so the pixels it covered when mirrored are not the same
    pixels it covers when laid back on unmirrored. Whatever is left over shows, and it has
    to look like the scarf rather than like a hole.
    """
    pixels = np.asarray(image).astype(float).copy()
    unknown = hole.copy()

    while unknown.any():
        known = ~unknown
        total = np.zeros_like(pixels)
        count = np.zeros(pixels.shape[:2])

        for axis, shift in ((0, 1), (0, -1), (1, 1), (1, -1)):
            total += np.roll(np.where(known[..., None], pixels, 0.0), shift, axis)
            count += np.roll(known.astype(float), shift, axis)

        edge = unknown & (count > 0)
        if not edge.any():
            break

        with np.errstate(invalid="ignore"):
            averaged = total / np.maximum(count, 1)[..., None]
        pixels[edge] = averaged[edge]
        unknown &= ~edge

    return Image.fromarray(pixels.round().clip(0, 255).astype(np.uint8))


def foot_centre(image: Image.Image) -> int:
    """The middle of the lowest tenth of the figure: where it touches the ground.

    These are leaping poses, so the middle of the whole drawing can be well away from the
    part that actually meets the surface -- and the anchor is the feet.
    """
    solid = np.asarray(image)[..., 3] > 24
    rows = np.nonzero(solid.any(1))[0]
    if rows.size == 0:
        return image.width // 2

    band = solid[max(rows[0], rows[-1] - max(1, int(0.10 * image.height))):rows[-1] + 1]
    columns = np.nonzero(band.any(0))[0]
    return int(round((columns[0] + columns[-1]) / 2.0)) if columns.size else image.width // 2


def pose(cell: Image.Image, scale_y: float, degrees: float, feet: tuple[float, float]) -> Image.Image:
    """One frame: leaned, then squashed, both about where the mascot meets the surface.

    About the feet rather than the middle of the cell, which is what a lean does in life --
    pivot anywhere else and the character slides sideways through the thing it is standing
    on.
    """
    posed = cell

    if degrees:
        posed = posed.rotate(degrees, resample=Image.BICUBIC, center=feet)

    if scale_y != 1.0:
        posed = posed.transform(
            posed.size, Image.AFFINE,
            (1.0, 0.0, 0.0, 0.0, 1.0 / scale_y, feet[1] - feet[1] / scale_y),
            resample=Image.BICUBIC)
    return posed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--png", type=Path, required=True)
    parser.add_argument("--id", required=True, choices=sorted(MASCOTS))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--height", type=int, default=52,
                        help="cell height in pixels, including the padding above")
    parser.add_argument("--source-faces", choices=("left", "right"), default="left",
                        help="which way the mascot faces in the artwork; KDE draws all "
                             "three of them facing left, and a pack's cells face right")
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)

    source = Image.open(args.png).convert("RGBA")
    source = source.crop(source.getbbox())
    source, removed = drop_loose_parts(
        source.resize((max(1, round(source.width * WORKING_HEIGHT / source.height)),
                       WORKING_HEIGHT), Image.LANCZOS))
    source = source.crop(source.getbbox())
    print(f"{args.id}: {source.width}x{source.height} working, {removed} loose pixel(s) dropped")

    # From here on the artwork faces left, which is how KDE draws all three mascots and
    # what the rest of this reads as the un-mirrored direction.
    if args.source_faces == "right":
        source = source.transpose(Image.FLIP_LEFT_RIGHT)

    rule = LETTERING.get(args.id)
    if rule is None:
        # Cells face right; the renderer mirrors them to walk the other way. Nothing here
        # carries lettering, so mirroring costs nothing and halves the sheet.
        facings = [source.transpose(Image.FLIP_LEFT_RIGHT)]
        print("  no lettering; the renderer mirrors this pack")
    else:
        mask = letter_mask(source, rule)
        rows, columns = np.nonzero(mask)
        bounds = (columns.min(), rows.min(), columns.max() + 1, rows.max() + 1)

        letter = Image.fromarray(
            np.where(mask[..., None], np.asarray(source), 0).astype(np.uint8)).crop(bounds)
        body = fill_behind(source, mask)

        facing_left = body.copy()
        facing_left.alpha_composite(letter, (bounds[0], bounds[1]))

        facing_right = body.transpose(Image.FLIP_LEFT_RIGHT)
        facing_right.alpha_composite(letter, (facing_right.width - bounds[2], bounds[1]))

        facings = [facing_right, facing_left]
        print(f"  lettering at {bounds[0]},{bounds[1]} {bounds[2] - bounds[0]}x"
              f"{bounds[3] - bounds[1]}, drawn both ways round")

    # Centred on the feet rather than on the drawing, so that the default anchor -- the
    # bottom middle of the cell -- lands where the mascot meets the surface. A pack that
    # draws both directions gets no chance to mirror its anchor, since nothing is mirrored,
    # so the two facings have to agree on where the feet are and the middle is the one
    # place they can.
    feet = [foot_centre(facing) for facing in facings]
    span = max(max(f, facing.width - f) for f, facing in zip(feet, facings))

    # Padded on every side by as far as the steepest lean carries the figure -- above as
    # well, since a stretched frame reaches higher than the drawing does. Leaning inside a
    # canvas that only just fits the upright pose crops an arm off, or a foot.
    lean = max(abs(degrees) for _, degrees in FRAMES)
    margin = ceil(facings[0].height * 1.05 * tan(radians(lean))) + 2
    frame = Image.new("RGBA", (span * 2 + margin * 2,
                               round(facings[0].height * 1.05) + margin), (0, 0, 0, 0))
    ground = (float(margin + span), float(frame.height - margin))

    cells = []
    for facing, f in zip(facings, feet):
        base = frame.copy()
        base.alpha_composite(facing, (margin + span - f, round(ground[1]) - facing.height))
        for scale_y, degrees in FRAMES:
            # Mirrored artwork leans the mirrored way, so that both directions play the
            # same walk rather than one that limps when the pet turns round.
            tilt = -degrees if len(facings) > 1 and facing is facings[0] else degrees
            cells.append(pose(base, scale_y, tilt, ground))

    # Trim the margin back off. Symmetrically in x about the feet, so that the middle of
    # the cell stays the point that meets the surface; in y down to whatever the leaning
    # frames actually reach, with the anchor naming the row the feet are on rather than the
    # bottom of the cell -- which is now a few pixels lower.
    used = [cell.getbbox() for cell in cells]
    half = max(max(ground[0] - box[0], box[2] - ground[0]) for box in used)
    top = min(box[1] for box in used)
    bottom = max(box[3] for box in used)
    cells = [cell.crop((round(ground[0] - half), top, round(ground[0] + half), bottom))
             for cell in cells]

    # An even width, so that the anchor sits exactly at the middle: a pack that is mirrored
    # at draw time has its anchor mirrored with it, and on an odd width that lands a pixel
    # off and the pet hops sideways every time it turns round.
    width = max(2, round(cells[0].width * args.height / cells[0].height))
    width += width % 2
    anchor = (width // 2, round((ground[1] - top) * args.height / cells[0].height))
    cells = [cell.resize((width, args.height), Image.LANCZOS) for cell in cells]

    atlas = Image.new("RGBA", (width * len(cells), args.height), (0, 0, 0, 0))
    for index, cell in enumerate(cells):
        atlas.paste(cell, (index * width, 0))

    atlas_path = args.out / f"{args.id}.png"
    atlas.save(atlas_path)
    print(f"wrote {atlas_path} ({atlas.width}x{atlas.height})")

    both = len(facings) > 1

    def animation(name: str, frames: str, left: str, duration: int, loop: bool = True) -> str:
        lines = [f"[{name}]", f"frames = {frames}"]
        if both:
            lines.append(f"frames-left = {left}")
        lines += [f"duration = {duration}", f"anchor = {anchor[0]}, {anchor[1]}"]
        if not loop:
            lines.append("loop = false")
        return "\n" + "\n".join(lines) + "\n"

    layout = ("; Cells 0-5 face right, 6-11 face left. Both are drawn rather than mirrored\n"
              "; at draw time, because Kori wears KDE's K and a mirrored K is a backwards K.\n"
              if both else
              "; Six cells, facing right. The renderer mirrors them for the other direction.\n")

    ini = f"""; Generated by tools/make_mascot_pack.py from {args.png.name}.
; Do not edit by hand -- rerun the script instead.
;
; Artwork: {MASCOTS[args.id]} by Tyson Tan, CC-BY-SA-4.0.
; https://community.kde.org/Promo/Material/Mascots

[pack]
id = {args.id}
name = {MASCOTS[args.id]}
artwork-licence = CC-BY-SA-4.0
attribution = {MASCOTS[args.id]} by Tyson Tan (CC BY-SA 4.0)
atlas = {args.id}.png
frame-width = {width}
frame-height = {args.height}

{layout}
; The anchor is named rather than left to default. These are leaping poses, so the middle
; of the drawing is nowhere near the part that meets the ground, and the leaning frames
; reach a little below the row the feet stand on.
"""
    ini += animation("walk", "0, 1, 2, 3", "6, 7, 8, 9", 140)
    ini += animation("idle", "4", "10", 400)
    ini += animation("turn", "4", "10", 350, loop=False)
    ini += animation("land", "1", "7", 200, loop=False)
    ini += animation("fall", "5", "11", 200)
    ini += animation("fly", "5", "11", 200)

    ini_path = args.out / f"{args.id}.ini"
    ini_path.write_text(ini, encoding="utf-8")
    print(f"wrote {ini_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
