# ELITE.EXE (Elite Plus) reverse-engineering notes

Addresses: `XXXX` = offset in code segment `0000` (load-relative), `ds:XXXX` = data segment
`0b00`. Other segments are named by their load-relative paragraph. Symbol names live in
`ghidra/names.txt`. Items marked **[verify]** still need a check (emulator or DOSBox-X).
In Ghidra's decompiled C, unnamed data shows up as linear `ram` addresses: `ds:XXXX` is
`0x1b000 + XXXX`, e.g. `bRam00023318` = `ds:8318`.

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
| `63b0` | `[5b3e+2·(c-1)]`, codes 1–6 | text control codes (`5b4a` on is the token table); handlers return to `63d8` |
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

## Galaxy

Same scheme as the original Elite, with Elite Plus's own formulas for the system data.

- Seed: three words `ds:5503/5505/5507` (w0, w1, w2). `twist` (`5e0f`): w0, w1, w2 ←
  w1, w2, w0+w1+w2. Four twists step to the next system.
- `load_galaxy_seed` (`5e25`): galaxy number `ds:8315`, seeds at `ds:5509` (6 bytes each;
  the eight are byte-rotations of galaxy 1's, as in the original).
  `galaxy_step` (`610e`): load seed, then `cx` × 4 twists.
- `planet_name` (`6130`) → `ds:8338`, length `ds:8341`: four rounds of "take w2 high byte,
  twist", digram `ds:5585 + 2·(b & 1f)`; the fourth digram only if w0 bit 6. Index 0 is
  two spaces, so names have 0–8 letters.
- `system_data` (`5ee8`): `5fe1` finds the system nearest to the chart cursor (`ds:8318/8319`; zoomed chart if `ds:831e` = 1, centre `ds:8316/8317`),
  `6047` distance, then from `5f00` (callable on its own):

  | Field | Formula |
  |-------|---------|
  | government `8345` | `(w1lo >> 3) & 7` |
  | economy `8346` | `((gov < 2 ? 2 : 0) | (w0hi & 7)) ^ 7`, index into names at `ds:894d` (0 Poor Agricultural … 7 Rich Industrial) |
  | tech `8347` | `((eco + 3) & w1hi) + (w2lo & 1)`, shown + 1 |
  | population `8348` | `(u8)(tech·eco) / 2 + 20 + gov`, in 0.1 billion |
  | species `8349–834c` | `ff` (Human Colonials) unless w2lo bit 7; else `(w2hi>>2)&7`, `w2hi>>5`, `(w0hi^w1hi)&7`, `((w2hi&3)+that)&7`; names at `ds:8ada/8b00/8b3b/8b76` |
  | productivity `834d` | `(gov+8)² · pop · 4` (16-bit) |
  | radius `834f` | `((rol2(w0hi·0101h) ^ (swap(w2) & 3ff)) & fff) + 10e1h` |
  | description seeds `8351/8353` | `w0^w1`, `w0^w1^w2` |
- Chart position (`5e95` → `ds:8318/8319`): x = w1hi, y = w0hi / 2 (galaxy chart).
- Checked: `re/emu/galaxytest.py` compares all 8 × 256 systems with `core/ep_galaxy.c`.

## Copy protection (not ported)

"Please type in the word at the following location in the Elite+ Novella 'Imprint'" — page,
paragraph, line, word.

- Picked once at start-up at the end of `load_grf` (`31f6`, from `entry`): `32b8` makes
  **one RNG step**, mixes the new state into a count, walks the 3-byte records at
  `ds:5070` (wrapping at a 0 byte), and unpacks the chosen record into page `ds:0a83`
  (6 bits), paragraph `0a91`, line `0a99`, word `0aa1` (3 bits each) and a 9-bit expected
  hash at `ds:09d9`. All of these stores use obfuscated `[di±disp]` addresses.
- Asked in `init_screens` (`1415`): digits converted in place, prompt at `ds:09db`, word
  typed by `0d9d`. Hash of the typed letters: `h = (2h + (c − 'A')) & 1ff`.
- Effect: a `ret` (`c3 00`) is written over the first instruction of the menu routine at
  `03ad` (reached from `032a 0332 0363 0367 039b`). In this copy the store is
  unconditional (`mov bp,[09d9]` is immediately overwritten by `mov bp,00c3`, then four
  `nop`s where the comparison presumably was), so any answer passes. **[verify]** what `03ad`
  does when it is not patched, i.e. what a wrong answer costs.
- The port leaves the protection out but keeps the RNG step, and treats `03ad` as `ret`.

## System descriptions

- `describe_system` (`632b`, from `data_on_system` at `8a55`): clears `ds:5a3e` (256 bytes),
  expands the template `ds:5a35` = `06 92 "is " 93 04 "."` with `print_text` (`6396`), then
  word-wraps it on screen. `data_on_system` calls `system_data` again afterwards.
- `print_text`: byte 0 ends; 1–6 control codes through `ds:5b3e`; ≥ 80h token: alternatives
  table `ds:5b4a + 2·(t − 80h)` → 5 string pointers, choice = `(low byte of desc_random) / 52`;
  other bytes are copied, a space after a space is dropped, and with capitals on
  (`ds:5a34` = 1) a byte `60h..78h` after a space is upper-cased (so `y`/`z` are not; no
  table text makes that visible).
- `desc_random` (`6496`): `8351, 8353 ← 8353, 8351 + 8353`; returns the new `8353`.
  Seeded by `system_data` (`w0^w1`, `w0^w1^w2`).
- Codes: 1 system name (`6401`), 2 name + "ian" with a final vowel dropped (`6414`), 3
  random name (`6446`: seed `8351, 8353, 8351^8353` → `planet_name`, which also clobbers
  the system seed at `ds:5503`), 4 back one character, 5/6 capitals on/off. Names are
  printed from `ds:63f2` as "Xxxx " (`64a5`: first letter as is, the rest `| 20h`).
- Original bug, kept: code 3 saves and restores the 8 name bytes but not the length byte
  `ds:8341`, and `64a5` terminates the name at that length, so a system name after a shorter
  random name is cut (Biqurala: "the Biquralian black Sotezaoid and the Biqurian yellow
  stripey walking eviloid").
- 39 token tables (80h–a6h; 98h and 99h are not reachable from the template). Spellings
  such as "wierd" are the original's.
- Checked: `re/emu/desctest.py` compares all 2048 systems and 20 000 random seeds with
  `core/ep_desc.c`.

## Market

Elite Plus's market is not the original Elite's: prices are fixed per system type, only the
quantities are random.

- Arrival (`72f0`, after a hyperspace jump): copies the selected system record (`ds:8338`,
  `[82d9]` bytes) to the current system record `ds:831f` (so government `832c`, economy
  `832d`, tech `832e`), clears `ds:839d` (market drawn).
- `market_prices` (`97d8`), 17 commodities: `v = 100h`, then `v = (v·f) >> 8` (16-bit
  middle word of the `mul`) with f = economy factor (`ds:905f`, row of 8 words per
  commodity), government factor (`ds:916f`), `100h`, base price (`ds:927f`), and
  `100h + a + b·min(tech, 9)` from `ds:92a1` (3 bytes per commodity: a, b signed, third byte
  1 for Slaves, Narcotics, Firearms, presumably illegal). a and b are 0 in the shipped data,
  so tech does not change prices **[verify]** that nothing writes them. Result in tenths of a
  credit at `ds:8d0a + 4k`, selling price at `+2`.
- `sell_price` (`8e6b`): `s = price >> 5`, halved while ≥ 100; `price − (s + 1)`.
- Quantities: the table drawer (`8ea2`) draws them while `839d` = 0 (`market` at `9048`
  then sets it): `r = market_random()` (`9880`, the twist on `ds:92e0..92e4`, returns old
  `w0 + w1`), `q = (r & 1f) − 7`, 0 if negative, else `q ^ ((r >> 8) & 3)`. Stored as the
  second byte of the cargo pairs at `ds:8379` (commander block, name at `ds:8370`). The
  generator starts at `007b 01c8 0315` and is never reseeded **[verify]** (save files?).
- Equipment (`9161`): 14 records at `ds:8bef` {min tech, name, i8 gov factor, i8 eco
  factor, word base}; listed while `(u8)(tech + 1) ≥ min tech`, up to 14 (count `ds:acb0`);
  price `base + gf·gov + ef·eco` (signed byte multiplies). Selling price only for items with
  a nonzero count at `ds:8356 + k` (k ≥ 1; `8357` missiles, …), else 0.
- Checked: `re/emu/markettest.py`: commodity and equipment prices for every government ×
  economy × tech byte, 200 arrivals, and the selling price of all 65536 buying prices.

## ELITE.GRF (bitmaps) and palettes

- `load_grf` (`31f6`): 32-byte header, one 8-byte entry per video class (`ds:10bc` >> 1):
  paragraphs to allocate, 32-bit file offset, image count. Class 0 (EGA, VGA 16 colours):
  offset `20h`; class 1 (MCGA): offset `1976bh`; 139 images each, same order and sizes.
  Pointers to the images go to the table at `[ds:2650]` (far pointers, 4 bytes each).
- Images (`3362` header, `33a6` body, byte reader `33d4` with a 256-byte buffer): 3-byte
  header, then PackBits-style RLE (n ≥ 0: n + 1 literals, n < 0: next byte 1 − n times).
  16 colours: width in bytes, plane mask (bit 7 = an extra plane, the transparency mask
  **[verify]**), height; one plane after the other. MCGA: width (15 bits, bit 15 a flag,
  probably transparency), height, a byte per pixel.
- Image 138 is the full-screen "Hanger 18" picture; most others are icons, digits and
  dashboard parts (80×61, 64×64, 24×16 …).
- Video mode (`384f`, keys E/V/M → `ds:10bc` 0/1/2): EGA mode 0Dh with attribute registers
  `ds:1122` (index 6 is `06h`, dark yellow, not the usual brown); VGA mode 0Dh with
  attributes `ds:1133` and DAC `ds:1144`; MCGA mode 13h with the full 256-entry DAC
  `ds:1144`. Game colour → pixel value table `ds:1ee9` (256 words) is copied from `ds:1b3f`
  (16 colours) or `ds:1cf3` (MCGA); MCGA also patches code words listed at `ds:1b1a`.
  `3921` cycles one entry (flashing colours), `3c50` loads DAC ranges.
- `re/tools/grf.py dump` writes all images as PNG with these palettes. The 16-colour set
  looks right; the MCGA title picture has wrong colours in places, so that screen must load
  its own palette **[verify]**.

## Objects and ship rendering

- Object slots at `ds:76de`, 64 bytes each, count `ds:76b5`. `+00` flags: bit 0 active,
  bits 1–5 type, bit 6 drawn-this-frame candidate, bit 7 in view; `+04/06/08` position
  relative to the player, `+0a/0c/0e` angles, `+10/12/14` camera-space position, `+1e`
  flags (`60h` both set = not drawn), `+3c/3e` distance keys. Types 30/31 are planet and sun.
- `update_objects` (`4154`): player angles `ds:76d8/da/dc` → sin/cos slots (`6d0a…`, table
  `ds:6410`: 2048 words, angle & 7ffh, cosine 512 entries on); per object `6e01` rotates the
  position by the player's axes (`6d83`: rotate a pair with rounding), the in-view test is
  `|x|·2 ≤ z`, `|y|·2 ≤ z`, z ≥ 100. Then painter's order: planet/sun farthest first
  (`44c7`), ships farthest first by camera z (`draw_ship` `43ce`).
- `draw_ship` (`43ce`): angles a = −(obj a + player a)·32, b = −obj b·32, c = obj c·32,
  player b, c and `ds:b0de` likewise; single-axis matrices `3f4d` (X), `3f99` (Y), `3fe5`
  (Z) from the sine table `ds:2cc0` (1024 words, index angle >> 6, cosine = angle + 4000h;
  1.0 = 7ffeh); product (`4031`, row-major `out = a·b`) `Ry(b0de)·Rz(−pc)·Ry(−pb)·Rx(a)·Ry(b)
  ·Rz(c)`; camera position doubled (overflow → not drawn); `draw_model` (`3c90`).
- Q15 multiply everywhere: high word of the `imul` product shifted left once (the top bit of
  the low word is lost); sums wrap at 16 bits.
- Models (`draw_model`): table of 32 words at **ss:65bc** (the stack segment `1c0c`, read
  bp-relative), models at `ss:4010…`. Vertex count, vertices (3 × i16); transformed (+
  position) into a 10-byte record each at `ds:28e6` (X, Y, Z, screen x, y): `x = 98h +
  (X·256 + (X & ff)) / Z`, `y = 3eh + (Y' · 256 + (Y' & ff)) / Z` with `Y' = Y − Y >> 3`
  (the low byte appears twice because `al` is not cleared). A divide error (via `ds:01f8`
  → `3d63`) sets X and screen y to 400 and leaves screen x as it was, so the buffer keeps
  state between ships. Face groups: `01`, reference vertex (byte offset = 10·index), normal
  (3 × i16). The group is drawn if the rotated normal · the reference vertex ≤ 0, where the
  last addition is compared exactly (`jg` after `add`) and the rest wraps. Primitives:
  `00` triangle (3 vertices, colour → `172c`), `02` quad (4, → `1a7a`), `04` line (2, →
  `261b`); `03` ends the model. 30 models (types 0–29), 12–37 vertices.
- Checked: `re/emu/rendertest.py` (random types, angles, positions from inside the ship to
  far away, player angles) compares the primitives with `core/ep_render.c`.
