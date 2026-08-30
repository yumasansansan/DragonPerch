<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Translating DragonPerch

There is **one string table for the whole project**, because there are three programs in it
— the daemon, the Windows shell and the KDE settings module — and three mechanisms would
mean the same sentence translated three times and drifting three ways.

Nothing has to be installed for the program to work. Every call site carries its English,
and that English is the fallback: a build with no `lang` directory at all reads exactly as
it did before any of this existed.

## Adding a language

Copy `lang/ja.ini`, rename it to your language tag, and translate the right-hand side.

```ini
[Strings]
menu.pause = Pausa
menu.quit = Chiudi DragonPerch
```

The file goes in `lang/` beside the executable, or `share/dragonperch/lang` when installed —
the same rule the artwork follows, so an unpacked archive works without being installed.

`ja.ini` is matched for `ja`, `ja-JP`, and anything else that begins `ja-`. Name it
`pt-br.ini` when a region needs its own words and `pt.ini` when it does not; the more
specific file wins.

**Lower case, and a hyphen.** What the system reports is normalised before the file is
looked for -- `ja_JP.UTF-8` and `ja-JP` both become `ja-jp` -- so a file called `pt-BR.ini`
is found on Windows and not on Linux, which is the worse of the two ways to be wrong.

A key nobody has translated falls back to its English on its own. **A partial translation is
a useful translation** — there is no need to finish the file before it is worth having.

One limit worth knowing: a translation may not contain `;` or `#`, because both parsers read
them as the start of a comment and would cut the line there. Every language that needs one
has a fullwidth form — `；` and `＃` — which is what a Japanese sentence would use anyway.

## Why the key is an id and not the English

`menu.pause = 一時停止`, not `Pause = 一時停止`.

Two reasons, both practical. This file is INI, and an English sentence may contain `=`, `;`
or `#` — every one of which the format reads as punctuation. And rewording the English is a
thing that happens; keyed by the English, every translation of that string would silently
detach itself, and the only symptom would be a page half in one language.

The cost is that a call site names its string twice:

```cpp
tr("menu.pause", "Pause")
```

which is also what makes the fallback work without an English catalogue to install.

## Where the strings are

| | |
|---|---|
| `src/core/dragonperch/language.hpp` | The table itself, and `tr` |
| `lang/*.ini` | The catalogues |
| `src/win/tray.cpp`, `src/linux/tray.cpp` | The tray menus |
| `shell/windows/Strings.cs` | The same catalogue, read by the Windows shell |
| `shell/windows/TrayMenu.cs`, `SettingsWindow.xaml.cs` | The Fluent menu and settings window |
| `kcm/` | The KDE settings module, through `kcm.text(id, english)` in its QML |

The Windows settings window translates the sentence already in its markup rather than
carrying a second copy of the English in the code: `Strings.Get("settings.pets",
PetsTitle.Text)`. There is one place the English lives, and an id nobody has translated
leaves the element exactly as the markup wrote it.

Which language is chosen is asked of the operating system by each program, because the core
is not allowed to: `GetUserPreferredUILanguages` on Windows, `LC_ALL` / `LC_MESSAGES` /
`LANG` on Linux in that order of preference, `CultureInfo.CurrentUICulture` in the Windows
shell and `QLocale::system().uiLanguages()` in the settings module.

Not the locale, on either platform: the locale is what a number looks like and the UI
language is what the words are, and both systems let them differ. `LC_ALL=C` with
`LANG=ja_JP.UTF-8` correctly means English, and was checked. `dragonperch.log` says which catalogue was
loaded and how many strings it had.
