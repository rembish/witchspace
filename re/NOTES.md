# ELITE.EXE (Elite Plus) reverse-engineering notes

Addresses: `XXXX` = offset in code segment `0000` (load-relative), `ds:XXXX` = data segment
`0b00`. Other segments are named by their load-relative paragraph. Symbol names live in
`ghidra/names.txt`. Items marked **[verify]** still need a check (emulator or DOSBox-X).

## The original

```
6d7e748345f31e41cd8c90fc739a2c23fed32a277fc9b3e530f5d6c6bae86ef5  ELITE.EXE
900fb787f5e7ed905e6931f1b522085a88a538b4e61396aed4ba329e05593b22  ELITE.GRF
ee0d9a9d4b388f3af3ed6ec6aadd38a27823c1eee614af4bf378227e98364081  ADBLUE.MID
0ed09d47be1de8259322733fef0a6c1136691fed867cf213aecd4e807c8b0e89  BLUTEST.MID
7219c50c8fdf7c278a3ba54b9ea7c2cca23b41fe329cc5851fc8b768e66c297c  RFX.MID
```

## Binary layout

- `ELITE.EXE` is packed with Microsoft EXEPACK (`RB` header at `CS:0`, stub prints
  "Packed file is corrupt"). `tools/unexepack.py` restores a plain MZ: 103 391 → 153 360
  bytes, 53 relocations, entry `0000:0000`, stack `2571:0100` (immediately replaced).
- Hand-written assembly, no compiler runtime: segments are loaded as immediates
  (`mov ax,0b00 / mov ds,ax`), registers saved with `push ax/bx/cx/dx/di`, data addressed
  absolutely.
- Relocated segment constants: `0000` code, `0b00` data (DS = ES), `16e4`, `1989`,
  `1c0c` stack (SP `4000`), `2270`, `2571` end of image (used for the `int 21h/4Ah` resize).
- Second code segment `2270` (Ghidra `3270`) is the sound/music driver: MPU-401 (ports
  `330/331`, Roland LAPC1), AdLib (`388/389`), far-called; its entry `2270:0000` saves all
  registers and loads DS = `0b00`. Most of its 12 KB is driver data (only ~23% decodes as code
  reached from the game).

## Static analysis tooling

- `tools/explore.py`: recursive descent from the entry point that carries known register
  and pushed values along each path. That is needed because the code is partly obfuscated:
  addresses are built with `mov bx,0e33 / mov al,[bx-3b0]` (= `[0a83]`) or `mov di,06d9 /
  add di,06c4 / call di` (= `call 0d9d`), and control passes through `push addr / ret`.
  It also resolves the jump tables and call vectors listed below. Coverage of segment
  `0000`: 93.6% (412 functions); the rest is mostly small unreferenced fragments.
- `ghidra/run.sh`: unpack, run the explorer, import into Ghidra with DS = ES = `0b00` over
  both code segments, create every function the explorer found, apply `names.txt`, dump
  `elite_decomp.c` and `elite_disasm.txt`.
- `tools/disasm.py SEG:OFF [LEN]` for quick looks; `dosbox/run.sh` runs a copy from
  `original/` in DOSBox-X on a private Xvfb display with typed keys and screenshots.

### Indirect control flow

| Site | Through | Targets |
|------|---------|---------|
| `0000:00b2` | `push 00b3 / push 1415 / … / ret` | init at `1415`, which returns to `00b3` |
| `00b3` | `mov [0000],sp / jmp 9e80` | main loop; `ds:0000` is the SP to restore on abort **[verify]** |
| `02f0` | `[bx+0368]`, 6 entries | `04b2 0534 0580 05aa 05ab 05ac` |
| `041f` | `[bx+0399]`, 32 entries | command handlers; index = per-screen key map `ds:030d` (12 bytes copied by `02e8` from rows at `ds:031f`) |
| `3dfb` | `[bx+2b6c]`, 3 entries | `3e19 3e4b 3e8e` |
| `63b0` | `[5b3e+2·(c-1)]`, codes 1–31 | text control codes; handlers return to `63d8` |
| `7155` | `[bx+81ea]`, 4 entries | `7178 7189 7195 71a1` |
| `7813` | `[bx+8720]`, 8 entries | `83f4 83f5 8352 84e1 84fc 8645 873b 81c7` |
| `call [107c]` ×24 | span routine | `14e0 14fc 1507 1514` (stored by `1a2f 1aa9 263c 264c`) |
| `[107e]` | | `14b0 1514` |
| `[107a]` / `[1b3c]` | | `2e12` / `16da` (stored at `3883`/`3889`) |

## Start-up

- `entry` (`0000`): SS:SP = `1c0c:4000`, shrink the memory block to end at `2571`, hook
  INT 23h/24h, require DOS ≥ 2 (message at `ds:02c6`), seed the RNG, save the BIOS video
  mode (`ds:0202`), init calls `49b4 01bc 4ecf 384f 31f6 4f03 71b0`, then `ret` into `1415`.
- Asks for the sound device (P speaker / A AdLib / R Roland LAPC1) and the graphics mode
  (E EGA / V VGA 16 colours / M MCGA 256), then the copy protection: "type in the word at
  … the Elite+ Novella 'Imprint'" (page, paragraph, line, word). Checked in DOSBox-X with
  the unpacked EXE.

## Random numbers

- 64-bit state at `ds:0205` (hi word of A), `0207` (lo A), `0209`, `020b` (B), initialised
  to `1234 dfab 5678 f2e7`. Step: `A' = A + (0209:020b)` (32-bit, `0207`+`020b`, then
  `0205`+`0209`+carry), `0209 = old 0207`, `020b = old 0205`.
- Seed: `int 21h/2Ch`, step count = `(1/100 s) ^ seconds ^ minutes` (0 means 256).
