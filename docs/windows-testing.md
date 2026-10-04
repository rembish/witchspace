# Testing the Windows version

The Windows version is built by CI with MSYS2's UCRT64 for every change, and checked there
for what needs no game files: that it builds as released, and that it finds a game folder
whose name is not ASCII. Everything else needs your copy of the game, so it runs on your
machine. From WSL, Windows programs run natively, so the whole of it is a few commands.

## Once

```sh
sudo apt install mingw-w64   # the cross-compiler (WSL's Debian or Ubuntu)
```

The tests run in a folder on the Windows side, `%USERPROFILE%\witchspace-test` (made when
needed; programs started from WSL's own folders get a `\\wsl.localhost` path, which not all
Windows tools take). Your copy of the game is taken from `original/`, as everywhere else.

## Each time

```sh
make windows      # build-win/: the game and the test tools, cross-built
make wintest      # those run on Windows (below)
make windifftest  # every routine against the original, with the Windows build (about an hour)
```

`make wintest` (`tests/wintest.sh`) runs, on Windows:

| Check | What it shows |
|-------|---------------|
| `ep_flow`, `ep_screencheck`, `ep_edgecheck` | the core and the screen with the Windows compiler (its `long` is 32 bits) |
| `ep_badinput` | broken game files rejected cleanly |
| `ep_savecheck` | saving through `MoveFileExA`: the old commander kept when the new one cannot be put in place |
| the game, from a folder named `Jürgen tést` | it finds the files and starts (drawing offscreen, no sound, for 8 s) |

`make windifftest` runs the comparisons with `ep_subsys.exe` (through `tests/wintool.sh`,
which gives it Windows paths), so the Windows build is held to the original byte for byte,
as the Linux one is.

Differences from the released build: Debian's MinGW links the older `msvcrt.dll`, the
releases MSYS2's UCRT. Both take file names through the manifest's UTF-8 code page.

## By hand, before a release

What a script cannot judge: about five minutes with the release's own zip
(`witchspace-windows-x64.zip`), unpacked into a folder with your game files.

- [ ] It starts from Explorer (double-click), with no console window.
- [ ] Double-clicked in a folder without the game's files: a box says ELITE.EXE was not found.
- [ ] From PowerShell, `.\witchspace.exe --version` prints the version.
- [ ] The window scales cleanly when resized, and keeps 4:3; Alt+Enter goes full screen and back.
- [ ] The title music plays (AdLib); `--speaker` gives the PC speaker's.
- [ ] Keys: the function keys pick screens, the arrows steer; nothing reaches Windows (F10 does not open a menu).
- [ ] A game controller steers and fires; unplugged and plugged in again, it still does.
- [ ] Save a commander, quit, start again, load it.
- [ ] The folder's name with a letter that is not ASCII (`Jürgen`): it still finds the game.
