<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
# Kori sprite pack

**This artwork is `CC-BY-SA-4.0`, not the project's GPL.** See `../README.md`.

| File | What it is | Credit |
|------|-----------|--------|
| `source/Mascot-kori-kde30logo-kori.png` | The original illustration, from KDE's 30th anniversary set | Kori designed and drawn by **Tyson Tan**. From [KDE's mascot material](https://community.kde.org/Promo/Material/Mascots) |
| `kori.png`, `kori.ini` | Generated sprite pack | Derived from the above by `tools/make_mascot_pack.py` |

## About the animation

KDE publishes Kori only as a flat PNG — there is no vector source for him the way there is
for Konqi, so there is no tail to swing separately.

The walk is therefore made by transforming the whole figure: a vertical squash and stretch
about his feet, and a lean of a couple of degrees either side of upright, a quarter-cycle
out of phase with it. The sparkles drawn beside him are dropped, being separate from the
figure, and the anchor is measured from the lowest tenth of the drawing rather than left at
the middle of the cell, because he is drawn mid-leap rather than standing.

## The K

Kori wears KDE's K on his scarf, and a mirrored K is a backwards K. Mirroring at draw time
cannot avoid it either: a pet walks to the end of a title bar and comes back, so it spends
half its life facing each way.

So his sheet carries twelve cells, both directions drawn. The right-facing half is built by
lifting the K off the artwork by colour, filling in the scarf behind it, mirroring the body,
and laying the K back on the right way round. Flipping the rectangle the K sits in — the
obvious thing to try — takes the edge of the scarf with it and leaves a visible seam.

Regenerate with:

```bash
python tools/make_mascot_pack.py --png assets/kori/source/Mascot-kori-kde30logo-kori.png --id kori --out assets/kori
```
