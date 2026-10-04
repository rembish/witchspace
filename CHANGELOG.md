# Changelog

All notable changes to Witchspace are recorded here, newest first. The version follows
[semantic versioning](https://semver.org/); until 1.0 the options and formats may still change.
Each release's section is also its GitHub release's notes.

## [0.1.3] — 2026-10-04

- **Windows: game folders with non-ASCII names.** A copy of the game under, say,
  `C:\Users\Jürgen\Games` was never found: the program now uses UTF-8 file names (Windows 10
  1903 or later). CI builds the Windows version on every change and checks such a folder.
- **Sound after a stall.** When the sound device stopped asking for samples for a while (the
  system suspending it), the AdLib's writes could be lost and a note left sounding; they are
  all kept now.

## [0.1.2] — 2026-10-04

Fixes from a code audit for edge cases: the game's own bugs, not the original's.

- **Lasers.** Buying a second laser when more than one mount was free took the money but never
  asked where to fit it (and selling one left it mounted). The mount dialogue opens again.
- **No hang without the music.** With the AdLib (the default), a missing `ADBLUE.MID`, or a
  MIDI file whose first track has no time in it, froze the title. The title runs without the
  music now.
- **Edited commander files.** A commander's checksum is easy to remake, so a save can say
  anything. A name filling all its 9 bytes, a government or tech level out of range, 65535
  kills, or a text with no end made the game read or write outside its memory, or hang. Each
  now stays inside the game's memory, doing what the original does wherever that is safe, and
  a new test (`ep_edgecheck`) checks every one, under the sanitizers too.
- **Drawing.** A model's line starting far outside the view was not drawn at all, and a large
  polygon could be filled wrongly on Windows and in the browser.
- **In the browser** a warning (no sound, damaged pictures) no longer hides the game: it shows
  as a note while you play.
- **Smaller things.** A `--data` or `--saves` folder name too long to keep is refused rather
  than cut short; a game controller unplugged and plugged in again works again.

## [0.1.1] — 2026-10-04

Fixes from an outside review (Codex): safer file handling, a fidelity fix, and stricter checks.

- **Dust in the original's colours.** Space dust was drawn through the general game colours;
  the original's dust routine has its own table of MCGA pixels (`ds:2656`). It is a separate
  primitive now (`EP_PRIM_DUST`), and a pixel-level test checks it.
- **Broken game files are refused safely.** A crafted `ELITE.EXE` could make the EXEPACK
  decompressor read outside its buffer, and a short `ELITE.GRF` the picture loader. Both now
  check every length and offset; a new test feeds them tens of thousands of broken files under
  AddressSanitizer and UBSan, in CI.
- **Saves are whole or not at all.** A commander is written to a temporary file, checked to
  the last byte, then put in place; a failed save is reported by the game, and the old file is
  kept. In the browser, files or commanders the browser's storage will not keep are reported
  instead of silently lost.
- **No undefined arithmetic.** The dust's steering shifted negative numbers left; the core now
  builds warning-free in Debug too, and the comparisons run clean under the sanitizers.
- **Sound.** The PC speaker's state is handed to the audio thread under its lock.
- **Tests and releases.** `make difftest` fails when a comparison fails or the routines
  cannot be listed (it reported success); an empty test corpus, or a game copy that is not
  Elite Plus, is an error; CI runs a sanitizer build; releases and the web site are
  published only after CI passes.
- **Licences.** Every download and the web site carry the third-party licences (Nuked OPL3's
  LGPL, SDL2's zlib, and for the web Emscripten's and musl's MIT) and `THIRD_PARTY.md`, with how to relink with your own Nuked OPL3. The
  README and the interface notes now say precisely what the comparisons prove.

## [0.1.0] — 2026-10-04

The first release: Elite Plus (1991), rebuilt as portable C, complete and playable on the
desktop and in the browser.

- **The whole game.** Title, new game, every station screen and dialogue, trading, equipment,
  the charts, flight and combat with the ship AI, scooping, docking (manual and by computer),
  hyperspace and witchspace, missions, the pause menu and its options, saving and loading
  commanders, keyboard, joystick, mouse and game-controller steering. The copy protection is
  reconstructed but off unless asked for (`--protection`).
- **Sound.** The PC speaker's music and effects, and the AdLib's: its driver's MIDI player for
  the title music and its effects interpreter in flight, played on an emulated OPL2 (Nuked
  OPL3). The AdLib is the default; `--speaker` chooses the speaker.
- **Faithful by test.** Every reconstructed routine is checked against the original running
  in an emulator: the same memory, the same drawing, the same sounds, byte for byte, over
  real game states and random variations of them; the whole start-up and a game from the
  title into space are compared too. The original's quirks are kept; its two known bugs are
  described in `re/FLIGHT.md`. The reconstruction agrees with the original's manual.
- **No original data.** The game's tables, texts and models are read from your copy of
  `ELITE.EXE` at start-up (as released, EXEPACK-compressed, or unpacked), with its pictures
  from `ELITE.GRF` and its music from `ADBLUE.MID`. The game looks for them beside itself, in
  any case of file name.
- **In the browser** at <https://rembish.github.io/witchspace/>: the page takes the three
  files from your copy, or from the Internet Archive's once you agree, and keeps them in the
  browser.
- **For developers.** CI (gcc and clang with warnings as errors, the web build, formatting,
  the Python tooling); typed Python tools for the reverse engineering and the differential
  tests (ruff, strict mypy); `make help`.

Not planned: the Roland LAPC-1's music and effects, and the 16-colour EGA/VGA screen modes.

[0.1.3]: https://github.com/rembish/witchspace/releases/tag/v0.1.3
[0.1.2]: https://github.com/rembish/witchspace/releases/tag/v0.1.2
[0.1.1]: https://github.com/rembish/witchspace/releases/tag/v0.1.1
[0.1.0]: https://github.com/rembish/witchspace/releases/tag/v0.1.0
