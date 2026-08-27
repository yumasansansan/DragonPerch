<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
# Konqi sprite pack

**This artwork is `CC-BY-SA-4.0`, not the project's GPL.** See README.md in this directory.

| File | What it is | Credit |
|------|-----------|--------|
| `source/Konqi.svg` | The original illustration | Konqi designed and drawn by **Tyson Tan**; vectorised by **Franco Perez**. From [KDE's mascot material](https://community.kde.org/Promo/Material/Mascots) |
| `konqi.png`, `konqi.ini` | Generated sprite pack | Derived from the above by `tools/make_konqi_pack.py` |

## About the animation

KDE publishes Konqi as single illustrations. There is no walk cycle in the material, and
`Konqi.svg` does not separate the legs from the body — at this size they are about three
pixels, so animating them would not be visible even if it did.

The walk is therefore made from the one pose: the figure squashes and stretches vertically
about its feet, and the tail swings a quarter-cycle out of phase with it. Those are the
parts of the motion that survive being drawn 52 pixels tall.

The cast shadow is dropped. It is a soft grey ellipse meant for a page, and on a title bar
it would be a smudge on somebody's window.

Regenerate with:

```bash
python tools/make_konqi_pack.py --svg assets/konqi/source/Konqi.svg --out assets/konqi
```
