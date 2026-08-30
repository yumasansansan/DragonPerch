<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Packages and downloads

#
Every build of `main` publishes a rolling **`nightly`** pre-release, and every CI run
attaches the same files as artifacts for fourteen days. Both come from the one set of
`install()` rules, so what is tested is what is shipped.

| File | What to do with it |
|---|---|
| `dragonperch_0.1.0~20260828.1830.g462431a_amd64.deb` | `sudo apt install ./dragonperch_*.deb` |
| `dragonperch_0.1.0~…_x86_64.tar.gz` | unpack anywhere and run `usr/bin/dragonperch-wl` |
| `dragonperch_0.1.0~…_x64.zip` | unpack and run `dragonperch.exe` |

Every nightly carries its build time and commit in the version, so `apt` upgrades one to
the next rather than refusing the newer file as a downgrade. The **tilde is what makes that
work**: Debian sorts `~` before everything, including nothing at all, so

```
0.1.0~20260828.1830.g462431a  <  0.1.0~20260829.0300.gdeadbee  <  0.1.0
```

— nightlies ascend, and a real `0.1.0` supersedes every nightly of it. `dragonperch
--version` prints exactly what is installed.

**The downloaded file's name will have a `.` where that `~` should be.** GitHub rewrites
anything outside `[A-Za-z0-9._-]` in a release asset's name, so `0.1.0~2026…` arrives as
`0.1.0.2026…`. It makes no difference: `dpkg` and `apt` read the version from the
package's control field, and the file name is a convention, not data. Check for yourself —

```bash
dpkg-deb -f dragonperch_*.deb Version
```

— and CI prints the same field on every run, alongside a `dpkg --compare-versions` that
fails the build if it ever stops sorting before the release version.

The artwork is found relative to the executable, so an unpacked tarball works without being
installed and without an environment variable. Installing the package does **not** enable
the KWin script and does **not** start anything at login — a program that puts dragons on
somebody's screen because a dependency pulled it in is a program that gets uninstalled.

### Windows Defender flags the download

`Trojan:Win32/Wacatac.B!ml` — the `!ml` is the tell: a machine-learning guess, not a
signature match. It is a false positive, and a predictable one. The binary is unsigned,
freshly built, downloaded by nobody yet, and what it does for a living is enumerate other
applications' windows, install a system-wide event hook, and keep a transparent
always-on-top window over the whole screen. That is also roughly what a screen-scraper does.

There is no trick that makes this go away honestly, and anything that did would be a trick
worth being suspicious of. The two real answers are:

- **report it** at <https://www.microsoft.com/en-us/wdsi/filesubmission> as a false
  positive. This works, and it is worth doing — it is how the file stops being flagged for
  everybody rather than just for you.
- **sign it.** A code-signing certificate is what gives a binary an identity and lets
  reputation accumulate against it. Until then every new build starts from zero, and
  SmartScreen will warn about it whether or not Defender does.

Build it yourself and the problem does not arise: a locally compiled binary is not a
download.

To build them yourself:

```bash
cmake --build --preset linux-x64-release && cd build/linux-x64 && cpack -C Release
```
