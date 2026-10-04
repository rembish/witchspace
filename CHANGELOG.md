# Changelog

All notable changes to Witchspace are recorded here, newest first. The version follows
[semantic versioning](https://semver.org/); until 1.0 the options and formats may still change.
Each release's section is also its GitHub release's notes.

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

[0.1.0]: https://github.com/rembish/witchspace/releases/tag/v0.1.0
