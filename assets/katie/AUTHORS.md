<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
# Katie sprite pack

**This artwork is `CC-BY-SA-4.0`, not the project's GPL.** See `../README.md`.

| File | What it is | Credit |
|------|-----------|--------|
| `source/Mascot-katie-kde30logo-katie.png` | The original illustration, from KDE's 30th anniversary set | Katie designed and drawn by **Tyson Tan**. From [KDE's mascot material](https://community.kde.org/Promo/Material/Mascots) |
| `katie.png`, `katie.ini` | Generated sprite pack | Derived from the above by `tools/make_mascot_pack.py` |

## About the animation

KDE publishes Katie only as a flat PNG — there is no vector source for her the way there is
for Konqi, so there is no tail to swing separately and no cast shadow to drop.

The walk is therefore made by transforming the whole figure: a vertical squash and stretch
about her feet, and a lean of a couple of degrees either side of upright, a quarter-cycle
out of phase with it. Those are the parts of the motion that survive being drawn 52 pixels
tall.

Two things the flat source forced:

- the sparkles drawn beside her are separate from the figure, and a sparkle hanging in the
  air next to a window reads as a rendering bug, so everything not joined to her is dropped
- she is drawn mid-leap rather than standing, so the anchor is measured from the lowest
  tenth of the drawing rather than left at the middle of the cell — the middle is nowhere
  near the foot she lands on

Katie carries no lettering, so her sheet is six cells facing right and the renderer mirrors
them to walk the other way. Kori, who wears KDE's K, needs both directions drawn.

Regenerate with:

```bash
python tools/make_mascot_pack.py --png assets/katie/source/Mascot-katie-kde30logo-katie.png --id katie --out assets/katie
```
