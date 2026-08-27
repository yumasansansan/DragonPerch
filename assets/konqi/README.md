<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Sprite packs

Drop `konqi.ini` and its atlas image in this directory and DragonPerch will use them
instead of the procedural placeholder. With nothing here it still runs — you get a crude
green blob rather than a dragon.

To get a working pack to start from:

```bash
./build/windows-x64/src/win/Debug/dragonperch.exe --export-placeholder assets/konqi
```

That writes a `konqi.png` and the `konqi.ini` that matches it. Replace the cells in the PNG
with real artwork, keep the grid, and adjust the durations.

```bash
./build/windows-x64/src/win/Debug/dragonperch.exe --pets 8 --pack path/to/konqi.ini
```

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

Both `#` and `;` start a comment: the first is what people type, the second is what KDE's
own tooling writes.

## Licensing

**Artwork here is not GPL.** Konqi and the other KDE mascots are the work of the KDE
community under `CC-BY-SA-4.0`. Keep them under that licence, credit the original artists
in `AUTHORS.md`, and share derivatives the same way. There is no reason to relicense data
that is loaded at runtime rather than linked, and the `artwork-licence` key exists so a pack
carries its own terms with it.

Original artwork: <https://konqi.kde.org/>
