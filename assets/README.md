<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Sprite packs

One directory per mascot, each holding `<id>.ini` and the atlas image it names:

| Directory | Mascot | Built from | By |
|---|---|---|---|
| `konqi/` | Konqi | `Konqi.svg` | `tools/make_konqi_pack.py` |
| `katie/` | Katie | a flat PNG | `tools/make_mascot_pack.py` |
| `kori/` | Kori | a flat PNG | `tools/make_mascot_pack.py` |

Every `assets/<id>/<id>.ini` is loaded at startup and the pets are shared out between them,
so a new mascot needs no code. With none of this present DragonPerch still runs — you get a
crude green blob rather than a dragon.

To start a pack of your own from a working template:

```bash
./build/windows-x64/src/win/Debug/dragonperch.exe --export-placeholder assets/mine
```

That writes an atlas and the definition that matches it. Replace the cells in the PNG with
real artwork, keep the grid, and adjust the durations. To run one pack on its own, or a
chosen few, name them — `--pack` may be repeated:

```bash
./build/windows-x64/src/win/Debug/dragonperch.exe --pets 8 --pack assets/kori/kori.ini
```

Konqi has an SVG source, so his generator takes the drawing apart and swings his tail.
KDE publishes Katie and Kori as flat PNGs, so theirs animates the whole figure instead —
a vertical squash and a lean, which is all a single raster allows. Both write the same
format.

## Format

```ini
[pack]
id = konqi
name = Konqi
artwork-licence = CC-BY-SA-4.0
attribution = KDE community, https://konqi.kde.org/
atlas = konqi.png
frame-width = 64
frame-height = 64

[walk]
frames = 0, 1, 2, 3     ; cells, left to right then top to bottom
frames-left = 6, 7, 8, 9 ; optional; see below
duration = 130          ; milliseconds per frame
loop = true             ; the default
anchor = 32, 64         ; optional; defaults to the bottom centre of the cell
```

Every section other than `[pack]` is an animation, named by its section. The simulation
asks for `walk`, `idle`, `turn`, `fall`, `land` and `fly`; a pack missing one of those
throws the first time a pet enters that state, so define all six.

The **anchor** is the pet's feet — the point that sits on the window edge. Sprites are
authored facing right and mirrored for the other direction, and the anchor is mirrored with
them, so an off-centre anchor stays under the dragon either way.

### Both directions

`frames-left` is for artwork that cannot be mirrored. Konqi carries KDE's K, and a mirrored
K is a backwards K — and mirroring at draw time cannot avoid it, because a pet walks to the
end of a title bar and comes back, so it spends half its life facing each way.

Give a pack `frames-left` and the renderer stops mirroring that animation and uses those
cells instead. It must list exactly as many frames as `frames`: both directions run off one
clock, so that turning round does not restart the cycle and read as a stumble.

`tools/make_konqi_pack.py` builds such a sheet by rendering each frame twice — once with the
K hidden, once with nothing but the K — and laying the unmirrored K back onto the mirrored
body.

Both `#` and `;` start a comment: the first is what people type, the second is what KDE's
own tooling writes.

## Licensing

**Artwork here is not GPL.** Konqi, Katie and Kori are the work of the KDE community under
`CC-BY-SA-4.0`. Keep them under that licence, credit the original artists in each pack's
`AUTHORS.md`, and share derivatives the same way. There is no reason to relicense data that
is loaded at runtime rather than linked, and the `artwork-licence` key exists so a pack
carries its own terms with it.

Original artwork: <https://community.kde.org/Promo/Material/Mascots>
