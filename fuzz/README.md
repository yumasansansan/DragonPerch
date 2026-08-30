<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Fuzz targets

Eight targets. Six sit on a boundary where input arrives from somewhere this program did
not write it; two are not boundaries at all and are the more interesting for it.

| target | input | comes from |
|---|---|---|
| `ini` | INI text | every file the program reads goes through this parser |
| `settings` | `dragonperchrc` | edited by hand, and written by two other programs |
| `sprite_pack` | `<id>.ini` | authored artwork, and the loader searches directories people can put things in |
| `language` | `lang/<tag>.ini` | a translation, which anybody may drop in |
| `kwin_report` | one line-oriented report | **another process** — the KWin script, over the session bus |
| `png` | the atlas image | the largest and least structured file the program reads |
| `world` | *not input* | the geometry every frame is built on |
| `simulation` | *not input* | the physics, driven by a world that keeps changing |

`kwin_report` is the odd one out among the boundaries and the most interesting: it is the
only input here that is not a file. The format is ours, so nobody else's parser has found
the holes in it first. `png` is the odd one out in the other direction -- see below for what
it is and is not worth.

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

CI runs each for thirty seconds on the sanitized leg -- Linux only; see below. That is not long enough to find much
by itself; it is long enough to keep finding whatever somebody broke this week, and to
prove the corpus still parses. Anything libFuzzer writes on a failure —
`crash-*`, `leak-*`, `timeout-*` — is kept as an artifact.

## What each one asserts

They are not all the same shape, and the difference is the point.

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

## The three that assert rather than watch

`ini`, `settings`, `sprite_pack`, `kwin_report` and `png` all sit on an input boundary: the
bytes come from outside and what the target watches for is a crash. Three of them do not.

`language` is a boundary -- anybody may drop a file into `lang/` -- but the interesting part
is what happens after parsing, so it states the fallback as a property: asked for one id
with two different English strings, a catalogue either answers with each of them, or answers
with the same translation twice. There is no third possibility, and stating it that way was
the second attempt. The first asserted that one particular id was absent from the file, and
libFuzzer reads the string literals out of the binary and puts them into the input; it found
`no.such.id.anywhere = ...` in under a minute, and the catalogue had done the right thing
with it.

`world` and `simulation` are not boundaries at all. Every number in them has already been
through one, so a crash is not what they are looking for: a wrong answer from `edge_below`
crashes nothing and puts a dragon on the window behind the one it was standing over, which
somebody has to notice by eye. They assert the invariants the rest of the core is written
against -- the sort order the lookups assume, that the edge found is really the highest one
below the point, that a pet claiming a perch has one that exists, that nothing becomes a NaN
and nothing runs away.

`simulation` is also the answer to a limit of the unit tests beside it: each of those sets up
one situation somebody thought of, and every hard bug this simulation has had was a situation
nobody did.

## The corpus

Real files, not invented ones: the pack definitions and atlases as shipped, a settings file
as the settings window writes it, and a report in the format `kwin/dragonperch-geometry`
sends. Starting from valid input is most of what makes a fuzzer find anything at all in a
structured format -- from random bytes it would spend its whole budget failing the PNG
signature check.

Findings worth keeping should be added here as new seeds, so the next run starts from them.
There are two so far. `kwin_report/negative-width.txt` is the first thing these targets
caught: the report parser bounded how large a number could be but not whether a *length*
could be negative, so `s DP-1 0 0 -920 1032` described a screen 920 pixels wide in the wrong
direction, and the walkable edge built from it had its right end to the left of its left
end. CI found it in twenty-two thousand executions, about two seconds in.

`language/id-that-looks-absent.ini` is the second, and it is a finding about a target rather
than about the program. See above.

The two behaviour targets are seeded with bytes that describe a plausible desktop rather
than a random one, for the same reason: from random bytes the fuzzer spends its budget
building a world at all instead of walking dragons around one.

## Where they run

The Linux job builds and runs them; the Windows job does neither, and that is a fact about
the compiler rather than a decision. libFuzzer is a Clang runtime, this project's Windows
head is built with MSVC, and `cl.exe` has no `-fsanitize=fuzzer` -- MSVC ships
AddressSanitizer and nothing else. Building the Windows head with `clang-cl` purely to fuzz
it would mean a second Windows toolchain to keep working.

Everything in `world`, `simulation`, `ini`, `settings`, `language` and `sprite_pack` is core
code, which is the same code on both platforms, so what runs on Linux covers it. What is not
covered is the Windows head's own parsing -- and `tools/hostile_ipc.ps1` is what answers for
that: it is not libFuzzer, but it does throw deliberately hostile input at the one boundary
the Windows daemon exposes, and CI runs it on the sanitized leg.
