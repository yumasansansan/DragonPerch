<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Fuzz targets

Five boundaries where input arrives from somewhere this program did not write it.

| target | input | comes from |
|---|---|---|
| `ini` | INI text | every file the program reads goes through this parser |
| `settings` | `dragonperchrc` | edited by hand, and written by two other programs |
| `sprite_pack` | `<id>.ini` | authored artwork, and the loader searches directories people can put things in |
| `kwin_report` | one line-oriented report | **another process** — the KWin script, over the session bus |
| `png` | the atlas image | the largest and least structured file the program reads |

`kwin_report` is the odd one out and the most interesting: it is the only input here that
is not a file. The format is ours, so nobody else's parser has found the holes in it first.
`png` is the odd one out in the other direction -- see below for what it is and is not
worth.

## Running them

They are built only with the sanitizers on, because that is where the instrumentation they
need already is — `cmake/CompilerOptions.cmake` compiles everything with
`-fsanitize=address,undefined,fuzzer-no-link`, and these targets add `-fsanitize=fuzzer` at
link time to supply libFuzzer's `main`. A fuzzer with no sanitizer under it would find
hangs and nothing else.

```bash
cmake --preset linux-x64 -D DRAGONPERCH_SANITIZE=ON
cmake --build build/linux-x64 --config Release --target fuzzers
```

Each binary is given its seed corpus, which is copied beside it at build time:

```bash
cd build/linux-x64
./fuzz/Release/dragonperch_fuzz_settings fuzz/Release/corpus/settings -max_total_time=60
```

CI runs each for thirty seconds on the sanitized leg. That is not long enough to find much
by itself; it is long enough to keep finding whatever somebody broke this week, and to
prove the corpus still parses. Anything libFuzzer writes on a failure —
`crash-*`, `leak-*`, `timeout-*` — is kept as an artifact.

## What each one asserts

Not all four are the same shape, and the difference is the point.

**`ini` and `sprite_pack` are allowed to throw.** A malformed file is a refusal with a line
number, not a guess, so `std::exception` is caught and ignored. What is being looked for is
everything else: a read past the end of the buffer, an unbounded allocation, a hang.
`sprite_pack` additionally checks every frame rectangle lands inside the atlas, because a
pack that parses and then produces a cell reaching off the sheet has reached the renderer.

**`settings` is not allowed to throw**, and nothing is caught. `parse_settings` promises
that a file with a stray bracket comes back as the defaults rather than stopping the pets
appearing, so an escaping exception is a finding. It also asserts the ranges the
documentation claims, and that the settings survive being written out and read back — the
settings window rewrites the whole file every time, so a value that parses but writes
unreadably would lose the lot on the next open.

**`kwin_report` is not allowed to throw either.** A report is one message among many and
the pets have to survive a bad one: the script can be an old version, a new one, or half
written.

**`png` is mostly not fuzzing our code, and that is worth saying.** The work happens inside
libpng, which OSS-Fuzz has run continuously for years; finding a bug there is not a
realistic goal. What had never been fuzzed is the seventy lines around it, and writing this
target found two holes in them straight away:

- the image size came from the header and went to `resize()` with no ceiling. libpng's own
  default limit is a million by a million, so a few dozen bytes of header could ask for four
  terabytes. There is a `png_set_user_limits` in front of it now.
- the row pointers were built from `width * 4` while libpng writes whatever
  `png_get_rowbytes` says. Every transformation in that function is meant to make those
  equal, and nothing checked that they were — an input where they differ is a heap overflow
  rather than a wrong picture. It is checked now.

So most of this target's value was collected while writing it. What it does from here is
keep those two true, and watch the premultiply loop that walks a buffer sized by one number
and filled by another.

## The corpus

Real files, not invented ones: the pack definitions and atlases as shipped, a settings file
as the settings window writes it, and a report in the format `kwin/dragonperch-geometry`
sends. Starting from valid input is most of what makes a fuzzer find anything at all in a
structured format -- from random bytes it would spend its whole budget failing the PNG
signature check.

Findings worth keeping should be added here as new seeds, so the next run starts from them.
There is one so far. `kwin_report/negative-width.txt` is the first thing these targets
caught: the report parser bounded how large a number could be but not whether a *length*
could be negative, so `s DP-1 0 0 -920 1032` described a screen 920 pixels wide in the wrong
direction, and the walkable edge built from it had its right end to the left of its left
end. CI found it in twenty-two thousand executions, about two seconds in.
