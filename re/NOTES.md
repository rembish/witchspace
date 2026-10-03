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
