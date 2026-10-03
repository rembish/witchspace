# ELITE.EXE (Elite Plus) reverse-engineering notes

These are the main notes on how the original `ELITE.EXE` works: its file layout, start-up,
random numbers, galaxy and market formulas, graphics, objects, the flight loop, trading and
sound. Each section names the original routines and data, says what they do, and ends with
the test that checks the C port in `core/` against the original. Further notes on the flight
loop and the ships are in `FLIGHT.md`, `SHIPS.md` and `AI.md`.

How to read the addresses:

- `XXXX` (4 hex digits, e.g. `a040`) is an offset in code segment `0000` of the unpacked
  `ELITE.EXE` (load-relative).
- `ds:XXXX` is an address in the data segment `0b00`. Where the context is clearly data
  (a list of fields, `ds:8318/8319`), the `ds:` is often left off the later numbers.
- Other segments are named by their load-relative paragraph (e.g. `2270`).
- In Ghidra's decompiled C, unnamed data shows up as linear `ram` addresses: `ds:XXXX` is
  `0x1b000 + XXXX`, e.g. `bRam00023318` = `ds:8318`.

Symbol names live in `ghidra/names.txt`. Items marked **[verify]** still need a check
(emulator or DOSBox-X).

## The original

```
6d7e748345f31e41cd8c90fc739a2c23fed32a277fc9b3e530f5d6c6bae86ef5  ELITE.EXE
900fb787f5e7ed905e6931f1b522085a88a538b4e61396aed4ba329e05593b22  ELITE.GRF
ee0d9a9d4b388f3af3ed6ec6aadd38a27823c1eee614af4bf378227e98364081  ADBLUE.MID
0ed09d47be1de8259322733fef0a6c1136691fed867cf213aecd4e807c8b0e89  BLUTEST.MID
7219c50c8fdf7c278a3ba54b9ea7c2cca23b41fe329cc5851fc8b768e66c297c  RFX.MID
```

## Binary layout

- **Packing.** `ELITE.EXE` is packed with Microsoft EXEPACK (`RB` header at `CS:0`; the stub
  prints "Packed file is corrupt"). `tools/unexepack.py` restores a plain MZ: 103 391 →
  153 360 bytes, 53 relocations, entry `0000:0000`, stack `2571:0100` (immediately
  replaced).
- **Code style.** It is hand-written assembly with no compiler runtime. Segments are loaded
  as immediates (`mov ax,0b00 / mov ds,ax`), registers are saved with `push ax/bx/cx/dx/di`,
  and data is addressed absolutely.
- **Segments.** The relocated segment constants are:

  | Segment | Use |
  |---------|-----|
  | `0000` | code |
  | `0b00` | data (DS = ES) |
  | `16e4`, `1989` | (use not noted) |
  | `1c0c` | stack (SP `4000`) |
  | `2270` | second code segment: sound/music driver |
  | `2571` | end of image (used for the `int 21h/4Ah` resize) |

- **Sound driver.** The second code segment `2270` (Ghidra `3270`) drives the MPU-401 (ports
  `330/331`, Roland LAPC1) and the AdLib (`388/389`). It is far-called; its entry
  `2270:0000` saves all registers and loads DS = `0b00`. Most of its 12 KB is driver data
  (only ~23% decodes as code reached from the game).

## Static analysis tooling

- `tools/explore.py` does a recursive descent from the entry point and carries known
  register and pushed values along each path. That is needed because the code is partly
  obfuscated:
  - addresses are built in two steps, e.g. `mov bx,0e33 / mov al,[bx-3b0]` (= `[0a83]`) or
    `mov di,06d9 / add di,06c4 / call di` (= `call 0d9d`);
  - control passes through `push addr / ret`.

  It also resolves the jump tables and call vectors listed below. Coverage of segment
  `0000`: 93.6% (412 functions); the rest is mostly small unreferenced fragments.
- `ghidra/run.sh` unpacks, runs the explorer, imports into Ghidra with DS = ES = `0b00` over
  both code segments, creates every function the explorer found, applies `names.txt`, and
  dumps `elite_decomp.c` and `elite_disasm.txt`.
- `tools/disasm.py SEG:OFF [LEN]` is for quick looks.
- `dosbox/run.sh` runs a copy from `original/` in DOSBox-X on a private Xvfb display with
  typed keys and screenshots.

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

- `entry` (`0000`), in order:
  - sets SS:SP = `1c0c:4000` and shrinks the memory block to end at `2571`;
  - hooks INT 23h/24h;
  - requires DOS ≥ 2 (message at `ds:02c6`);
  - seeds the RNG;
  - saves the BIOS video mode (`ds:0202`);
  - calls the init routines `49b4 01bc 4ecf 384f 31f6 4f03 71b0`;
  - then `ret`s into `1415`.
- The game then asks for the sound device (P speaker / A AdLib / R Roland LAPC1) and the
  graphics mode (E EGA / V VGA 16 colours / M MCGA 256), then the copy protection: "type in
  the word at … the Elite+ Novella 'Imprint'" (page, paragraph, line, word). Checked in
  DOSBox-X with the unpacked EXE.

## Random numbers

- **State.** 64 bits at `ds:0205` (high word of A), `0207` (low word of A), `0209`, `020b`
  (B), initialised to `1234 dfab 5678 f2e7`.
- **Step.** `A' = A + (0209:020b)` as a 32-bit add (`0207`+`020b`, then
  `0205`+`0209`+carry); then `0209` = old `0207` and `020b` = old `0205`.
- **Seed.** From `int 21h/2Ch`: step count = `(1/100 s) ^ seconds ^ minutes` (0 means 256).

## Galaxy

The galaxy uses the same scheme as the original Elite, with Elite Plus's own formulas for
the system data.

- **Seed and twist.** The seed is three words `ds:5503/5505/5507` (w0, w1, w2). `twist`
  (`5e0f`) does w0, w1, w2 ← w1, w2, w0+w1+w2. Four twists step to the next system.
- **Galaxy seeds.** `load_galaxy_seed` (`5e25`) takes the galaxy number from `ds:8315` and
  the seeds from `ds:5509` (6 bytes each; the eight are byte-rotations of galaxy 1's, as in
  the original). `galaxy_step` (`610e`) loads the seed, then does `cx` × 4 twists.
- **Names.** `planet_name` (`6130`) writes to `ds:8338`, length `ds:8341`. It does four
  rounds of "take w2's high byte, twist" and appends the digram `ds:5585 + 2·(b & 1f)`; the
  fourth digram only if w0 bit 6 is set. Index 0 is two spaces, so names have 0–8 letters.
- **System data.** `system_data` (`5ee8`):
  - `5fe1` finds the system nearest to the chart cursor (`ds:8318/8319`; the zoomed chart if
    `ds:831e` = 1, centre `ds:8316/8317`);
  - `6047` computes the distance;
  - then from `5f00` (callable on its own) it computes the fields below.

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

- **Chart position.** `5e95` → `ds:8318/8319`: x = w1hi, y = w0hi / 2 (galaxy chart).
- **Checked:** `re/emu/galaxytest.py` compares all 8 × 256 systems with `core/ep_galaxy.c`.

## Copy protection (ported later as an opt-in, off by default)

The question: "Please type in the word at the following location in the Elite+ Novella
'Imprint'" — page, paragraph, line, word.

- **The pick.** It is made once at start-up, at the end of `load_grf` (`31f6`, called from
  `entry`). `32b8`:
  - makes **one RNG step** and mixes the new state into a count;
  - walks the 3-byte records at `ds:5070` (wrapping at a 0 byte);
  - unpacks the chosen record into page `ds:0a83` (6 bits), paragraph `0a91`, line `0a99`,
    word `0aa1` (3 bits each) and a 9-bit expected hash at `ds:09d9`.

  All of these stores use obfuscated `[di±disp]` addresses.
- **The question.** It is asked in `init_screens` (`1415`): digits are converted in place,
  the prompt is at `ds:09db`, and the word is typed by `0d9d`. The hash of the typed letters
  is `h = (2h + (c − 'A')) & 1ff`.
- **The effect.** A `ret` (`c3 00`) is written over the first instruction of the menu
  routine at `03ad` (reached from `032a 0332 0363 0367 039b`).
  - In this copy the store is unconditional: `mov bp,[09d9]` is immediately overwritten by
    `mov bp,00c3`, followed by four `nop`s where the comparison presumably was. So any
    answer passes.
  - Unpatched, the title's key bar (`05ac`) finds no `c3 00` at `cs:03ad` and jumps to
    `00ba`: back to DOS.
- **Port.** Ported in `core/ep_boot.c` behind `g->protection`, off by default. The pick (and
  its RNG step) always runs; the question is asked only when the option is on, and a wrong
  word sets `f.protection_failed` (the title then quits). The check is the hash comparison
  that the no-ops replaced.

## System descriptions

- **Entry.** `describe_system` (`632b`, called from `data_on_system` at `8a55`):
  - clears `ds:5a3e` (256 bytes);
  - expands the template `ds:5a35` = `06 92 "is " 93 04 "."` with `print_text` (`6396`);
  - word-wraps the result on screen.

  `data_on_system` calls `system_data` again afterwards.
- **`print_text`** handles each byte as follows:

  | Byte | Meaning |
  |------|---------|
  | 0 | end |
  | 1–6 | control code, through `ds:5b3e` (see below) |
  | ≥ 80h | token: alternatives table `ds:5b4a + 2·(t − 80h)` → 5 string pointers; choice = `(low byte of desc_random) / 52` |
  | other | copied |

  When copying, a space after a space is dropped. With capitals on (`ds:5a34` = 1) a byte
  `60h..78h` after a space is upper-cased (so `y`/`z` are not; no table text makes that
  visible).
- **`desc_random`** (`6496`): `8351, 8353 ← 8353, 8351 + 8353`; returns the new `8353`. It
  is seeded by `system_data` (`w0^w1`, `w0^w1^w2`).
- **Control codes:**

  | Code | Effect |
  |------|--------|
  | 1 | system name (`6401`) |
  | 2 | name + "ian", with a final vowel dropped (`6414`) |
  | 3 | random name (`6446`): seed `8351, 8353, 8351^8353` → `planet_name`, which also clobbers the system seed at `ds:5503` |
  | 4 | back one character |
  | 5 / 6 | capitals on / off |

  Names are printed from `ds:63f2` as "Xxxx " (`64a5`: first letter as is, the rest
  `| 20h`).
- **Original bug, kept.** Code 3 saves and restores the 8 name bytes but not the length byte
  `ds:8341`, and `64a5` terminates the name at that length. So a system name printed after
  a shorter random name is cut (Biqurala: "the Biquralian black Sotezaoid and the Biqurian
  yellow stripey walking eviloid").
- **Tables.** There are 39 token tables (80h–a6h; 98h and 99h are not reachable from the
  template). Spellings such as "wierd" are the original's.
- **Checked:** `re/emu/desctest.py` compares all 2048 systems and 20 000 random seeds with
  `core/ep_desc.c`.

## Market

Elite Plus's market is not the original Elite's: prices are fixed per system type, and only
the quantities are random.

- **Arrival** (`72f0`, after a hyperspace jump) copies the selected system record
  (`ds:8338`, `[82d9]` bytes) to the current system record `ds:831f` (so government is at
  `832c`, economy `832d`, tech `832e`) and clears `ds:839d` (market drawn).
- **Prices.** `market_prices` (`97d8`) computes 17 commodities. Start with `v = 100h`, then
  apply `v = (v·f) >> 8` (the 16-bit middle word of the `mul`) for each factor f in turn:
  1. the economy factor (`ds:905f`, a row of 8 words per commodity);
  2. the government factor (`ds:916f`);
  3. `100h`;
  4. the base price (`ds:927f`);
  5. `100h + a + b·min(tech, 9)` from `ds:92a1` (3 bytes per commodity: a and b signed;
     the third byte is 1 for Slaves, Narcotics and Firearms, presumably illegal).

  a and b are 0 in the shipped data, so tech does not change prices **[verify]** that
  nothing writes them. The result, in tenths of a credit, goes to `ds:8d0a + 4k`, and the
  selling price to `+2`.
- **Selling price.** `sell_price` (`8e6b`): `s = price >> 5`, halved while ≥ 100; the result
  is `price − (s + 1)`.
- **Quantities.** The table drawer (`8ea2`) draws them while `839d` = 0 (`market` at `9048`
  then sets it):
  - `r = market_random()` (`9880`: the twist on `ds:92e0..92e4`, returns old `w0 + w1`);
  - `q = (r & 1f) − 7`; 0 if negative, else `q ^ ((r >> 8) & 3)`.

  The quantity is stored as the second byte of the cargo pairs at `ds:8379` (commander
  block, name at `ds:8370`). The generator starts at `007b 01c8 0315` and is never reseeded
  **[verify]** (save files?).
- **Equipment** (`9161`): 14 records at `ds:8bef` {min tech, name, i8 gov factor, i8 eco
  factor, word base}.
  - An item is listed while `(u8)(tech + 1) ≥ min tech`, up to 14 (count `ds:acb0`).
  - Price: `base + gf·gov + ef·eco` (signed byte multiplies).
  - A selling price exists only for items with a nonzero count at `ds:8356 + k` (k ≥ 1;
    `8357` missiles, …); otherwise it is 0.
- **Checked:** `re/emu/markettest.py`: commodity and equipment prices for every government ×
  economy × tech byte, 200 arrivals, and the selling price of all 65536 buying prices.

## ELITE.GRF (bitmaps) and palettes

- **File layout.** `load_grf` (`31f6`) reads a 32-byte header with one 8-byte entry per
  video class (`ds:10bc` >> 1): paragraphs to allocate, 32-bit file offset, image count.
  - Class 0 (EGA, VGA 16 colours): offset `20h`.
  - Class 1 (MCGA): offset `1976bh`.
  - Each class has 139 images, in the same order and sizes.

  Pointers to the images go to the table at `[ds:2650]` (far pointers, 4 bytes each).
- **Images** (`3362` header, `33a6` body, byte reader `33d4` with a 256-byte buffer): a
  3-byte header, then PackBits-style RLE (n ≥ 0: n + 1 literals; n < 0: the next byte
  1 − n times).
  - 16 colours: width in bytes, plane mask (bit 7 = an extra plane, the transparency mask
    **[verify]**), height; then one plane after the other.
  - MCGA: width (15 bits; bit 15 a flag, probably transparency), height, a byte per pixel.
- **Contents.** Image 138 is the full-screen "Hanger 18" picture; most others are icons,
  digits and dashboard parts (80×61, 64×64, 24×16 …).
- **Video mode** (`384f`, keys E/V/M → `ds:10bc` 0/1/2):

  | Mode | Hardware set-up |
  |------|-----------------|
  | EGA | mode 0Dh, attribute registers `ds:1122` (index 6 is `06h`, dark yellow, not the usual brown) |
  | VGA | mode 0Dh, attributes `ds:1133`, DAC `ds:1144` |
  | MCGA | mode 13h, the full 256-entry DAC `ds:1144` |

  The game colour → pixel value table `ds:1ee9` (256 words) is copied from `ds:1b3f` (16
  colours) or `ds:1cf3` (MCGA). MCGA also patches the code words listed at `ds:1b1a`.
  `3921` cycles one entry (flashing colours); `3c50` loads DAC ranges.
- **Tool.** `re/tools/grf.py dump` writes all images as PNG with these palettes. The
  16-colour set looks right; the MCGA title picture has wrong colours in places, so that
  screen must load its own palette **[verify]**.

## Objects and ship rendering

- **Object slots** are at `ds:76de`, 64 bytes each; the count is at `ds:76b5`. Fields:

  | Offset | Meaning |
  |--------|---------|
  | `+00` | flags: bit 0 active, bits 1–5 type, bit 6 drawn-this-frame candidate, bit 7 in view |
  | `+01/02/03` | position high bytes |
  | `+04/06/08` | position low words, relative to the player |
  | `+0a/0c/0e` | angles |
  | `+10/12/14` | camera-space position |
  | `+1e` | flags (`60h` both set = not drawn) |
  | `+3c/3e` | distance keys |

  Positions are 24 bits (high byte + low word). Types 30/31 are planet and sun (but see the
  correction under "Flight loop subsystems": 30 is the sun, 31 the planet).
- **`update_objects`** (`4154`):
  1. The player angles `ds:76d8/da/dc` become rotation slots `ds:76be/c2/c6` (`6d26`: sine
     table `ds:6410`, 2048 words, angle & 7ffh, cosine 512 entries on).
  2. For each active object:
     - flags `&= 3f`;
     - types 30/31 go to `433c` (planet/sun);
     - the explosion timer `+34` counts up unless `+1e` bit 1 is set (wrapping to 0 →
       `7e82`);
     - `+1e &= ~2`;
     - `4264`: if in range, go on to `42c8`. The range test (`4217`): all three high bytes
       are sign extensions; each |low word| < 12000; Σ hi(c²) < 895h, checked after y and
       after z; `+3e` = Σ >> 6; flag bit 6.
  3. `42c8`, for an object in range:
     - `6e01` rotates by the player's slots in the order (y,z), (x,z), (x,y). The rotation
       (`6d83`) doubles both operands with 16-bit wrap, rounds products as
       `hi16(p << 1) + bit 14 of p`, and computes `a cos − b sin`, `b cos + a sin`;
     - `+3c` = high byte of z;
     - scanner blip (`4359`, flight only);
     - if `ds:b0de` ≠ 0, a further (x,z) rotation by −b0de;
     - scooping check (`46e2`, fuel scoops only);
     - then, if z ≥ 100: store `+10/12/14`; in view (bit 7) if `2|x| ≤ z` and `2|y| ≤ z`
       (16-bit, unsigned).
  4. Drawing: planet/sun farthest first by `+3c` (`44c7`); then ships with flags `c1`,
     farthest first by camera z (strictly greater, unsigned), clearing bit 6 of each one
     drawn (`draw_ship` `43ce`).

  `487e` (flight only) is the compass.
- **Checked:** `re/emu/objtest.py` (random tables of ships with boundary positions, player
  and extra angles; compares the slots drawn, the primitives and all slot bytes afterwards)
  against `core/ep_objects.c`. Planets, sun, scanner, scooping and explosions came later
  and are covered below and in `SHIPS.md`.
- **`draw_ship`** (`43ce`):
  - angles: a = −(obj a + player a)·32, b = −obj b·32, c = obj c·32; player b, c and
    `ds:b0de` likewise;
  - single-axis matrices `3f4d` (X), `3f99` (Y), `3fe5` (Z) from the sine table `ds:2cc0`
    (1024 words, index angle >> 6, cosine = angle + 4000h; 1.0 = 7ffeh);
  - product (`4031`, row-major `out = a·b`):
    `Ry(b0de)·Rz(−pc)·Ry(−pb)·Rx(a)·Ry(b)·Rz(c)`;
  - the camera position is doubled (overflow → not drawn);
  - then `draw_model` (`3c90`).
- **Q15 multiply** everywhere: the high word of the `imul` product shifted left once (the
  top bit of the low word is lost); sums wrap at 16 bits.
- **Models** (`draw_model`):
  - A table of 32 words at **ss:65bc** (the stack segment `1c0c`, read bp-relative); the
    models are at `ss:4010…`. 30 models (types 0–29), 12–37 vertices.
  - A model starts with the vertex count and the vertices (3 × i16). Each is transformed
    (+ position) into a 10-byte record at `ds:28e6` (X, Y, Z, screen x, y):
    `x = 98h + (X·256 + (X & ff)) / Z`, `y = 3eh + (Y' · 256 + (Y' & ff)) / Z` with
    `Y' = Y − Y >> 3` (the low byte appears twice because `al` is not cleared).
  - A divide error (via `ds:01f8` → `3d63`) sets X and screen y to 400 and leaves screen x
    as it was, so the buffer keeps state between ships.
  - Face groups: `01`, reference vertex (byte offset = 10·index), normal (3 × i16). The
    group is drawn if the rotated normal · the reference vertex ≤ 0, where the last
    addition is compared exactly (`jg` after `add`) and the rest wraps.
  - Primitives: `00` triangle (3 vertices, colour → `172c`), `02` quad (4, → `1a7a`), `04`
    line (2, → `261b`); `03` ends the model.
- **Checked:** `re/emu/rendertest.py` (random types, angles, positions from inside the ship
  to far away, player angles) compares the primitives with `core/ep_render.c`.
- **Colours.** Primitive colours are game colours; the video mode maps them through
  `ds:1ee9` (from `ds:1b3f` or `ds:1cf3`). Using `1cf3` + the DAC for previews is a guess
  **[verify]**.

## Running the whole game (`re/emu/machine.py`)

This is a test tool only, not part of the port. The original boots from `entry` under
Unicorn, with Python stand-ins for DOS (INT 21h files, memory, vectors, time), the video BIOS
and an absent mouse.

- **Deterministic time.** Timer interrupts are injected only at the game's wait loops: the
  frame wait `3027` (2 ticks per frame, `301a`), the title picture `3b18` (1000 ticks or a
  key), the delay `4e88` and the countdown `af9a`. The timer handler is `4a99`; per tick,
  `4a50`:
  - `45de`++;
  - unless paused (`45e6`): the frame clock `45e0` (32-bit)++ and the countdown `45e4`--;
  - the sound tick, unless `45e7`.
- **Keys.** The keyboard handler `0215` reads port 60h and keeps a press/release table
  `ds:020d + scancode` (0 / 80h). The key code goes via `ds:0cad` into `ds:0d2f`; the E0 and
  NumLock flags are `0d30/0d31`; left shift is ignored. Keys are delivered one interrupt per
  key poll (`0276`).
- **PIT.** Divisor 5555h (54.6 Hz), set by `49b4`; the sound driver uses 0555h.
- **Use.** Booting with P, M and any word for the protection reaches the title loop in
  about a second. `screenshot()` saves the mode 13h screen with the DAC.
- **Title loop** (`9e80`): ship type `ds:b1bb` (26 = Cobra Mk III) approaches from 5000 by
  80 per frame in slot 2 (`ds:775e`) in front of a planet. MCGA palette cycling runs once
  per retrace in `3b3e` (colours from a1h, `ds:1444`). The title picture has its own palette
  (`ds:1744`, loaded at `3b7f`), which is why `grf.py` shows it with wrong colours.

## Circles (planets, sun) and the RNG in rendering

- **`draw_circle`** (`2ab9`): bx = x, cx = y (3D view pixels, 304 × 124), dx = r.
  - Rejected if r ∉ 1..1efh or the circle is entirely off the view (`2a8f`, `2aa4`).
  - Colour via `ds:1ee9` → `ds:108a`/`108e`.
  - A midpoint circle fills a table of 2r spans on the stack (`2b1d`: four write pointers
    `109a/109c/109e/10a0` filling top, middle and bottom).
  - Rows are drawn from `y − (r − r/8)`. Off-view rows only advance. Of the others, the row
    where `(count & 7) = 3` is dropped (7/8 aspect; it does not advance). Spans are clipped
    to 0..303 and passed to `[107e]`.
- **Span jitter.** `[107e]` is `1514` (plain span) or, when the detail mask `ds:108f` ≠ 0,
  `14b0`:
  - one **main RNG step**; the old A words, masked, jitter the span (`x −= hi & m`,
    `w += (hi & m) + (lo & m)`);
  - clipped again (`1507`), then `1514` (width 0 draws one pixel).

  Planets set the mask to 1, 3 or 7 by apparent size (`4581`), so every planet span drawn
  steps the game's RNG: the core reproduces the span count exactly.
- **Outline mode** (`ds:1091`, `2c70`): the first row, then per row the left and right edge
  steps from the row above, then the last row (not if there is only one row). Each goes
  through `2cf4` (one row up, view range, no clipping: off-view segments are dropped).
- **Main RNG** (`ds:0205..020b`, `core/ep_rng.c`): A = `0205:0207`, B = `0209:020b`; step
  `A ← A + B`, `B ← swap(A_old)` (`0209` = old low, `020b` = old high).
- **Checked:** `re/emu/circletest.py` (random circles on and off the view, all masks,
  outline mode, random RNG states): spans and the RNG state afterwards.
- **MCGA.** `set_video_mode` patches 8 code words from `ds:1b1a`: the span vectors in the
  line, triangle, quad and circle code (e.g. `2aed` → `16da`, `2afa` → `1675`). The MCGA
  jitter span `1675` steps the RNG the same way but adds 1 to the width.
  `circletest.py … mcga` applies the patches and checks that path.
- **Main RNG steppers** (writes to `ds:0205..020b`):

  | Address | Use |
  |---------|-----|
  | `0047/0067` | start-up |
  | `123b 1268 12c8 1322 1341 139a` | not yet identified here; `FLIGHT.md` identifies them as the Tribbles (`1221`) |
  | `14b0` | EGA span jitter |
  | `1675` | MCGA span jitter |
  | `32b8` | protection question |
  | `4527` | planet random event |
  | `99e7 9a36 9a80` | not yet identified |

  Any of these in a frame path must be ported before frame tests.
- **Planet and sun** (`433c`):
  - `6ee8` counts the shifts until the largest |coordinate| (24-bit) fits 16 bits and is
    below 9400 (`24b8h`), kept in `+0a`;
  - `6eb9` shifts all three 24-bit coordinates (arithmetic) by it;
  - then the normal rotation (`4317`), the camera position is stored, and flags `|= c0` (no
    range or view test).
- **`apparent_size`** (`4694`, dx = 100 planet, 50 sun):
  `(size << 16 >> scale) / (√hi16(x² + y² + z²) · 256)`. The root is counted by subtracting
  odd numbers in an 8-bit register (its wrap is unreachable: the sum stays below 2³²). A
  divide error or a result ≥ 256 gives 255. Checked by `re/emu/planettest.py`.

## Title screen

- **Set-up.** `title_loop` (`9e80`, going to `9e90` unless `ds:af18`):
  - music, title pictures and credits (`3ae5`, `af73`);
  - player angles and `b0de` cleared;
  - objects for screen 2 (`763e`);
  - slot 2 (`ds:775e`) at z = 5000, count 3 (slots 0 and 1 empty);
  - type `ds:b1bb` = the first of the list `ds:b263` (24 types, ff-terminated); list
    pointer `ds:b261`; hold timer `ds:b25f` = 0.
- **Frame** (`9f21..9ffa`), in order:
  1. key map (`0299`), speaker tune (`4d8e`), clear the view (`3130`);
  2. the red disc: `draw_circle(200, 60, 25)` with mask 1 (so the title steps the RNG every
     frame);
  3. slot 2: while the hold timer is 0, move in by 80 down to the type's closest distance
     (`ds:b1bc`); then hold 120 frames; then move out by 100 to 5000 and take the next type.
     `+1e` = 2, flags = type·2 + 1, angles `+0e/+0c/+0a` += 30, 20, 25;
  4. the name (`ds:b27c` by type) and "Press spacebar to start game";
  5. flash colour step `ds:1b3e` (0..5, `3921`);
  6. `update_objects`; frame wait;
  7. keys (space sets `ds:0319` and ends the loop).
- **Timing contract** (as the harness models it, a fast CPU): the frame wait (`301a`) waits
  until the tick count reaches the last flip time (`ds:267c`) + 2, then stores the new flip
  time. So a frame lasts two ticks of 54.6 Hz unless the work takes longer.
- **Checked:** `re/emu/titletest.py` boots the original in `machine.py` (MCGA) and compares
  6000 title frames (all 24 ships, the list wrap) with `core/ep_title.c`: RNG, slot 2, title
  state, clock, disc spans and ship primitives.

## Commander (save file)

- **Block.** The commander is `ds:82db`, 226 bytes long (the length is at `ds:82d7`;
  `ds:82d9` = 25 is the system record length). Save (`0877`) is a DOS create + write of the
  block; load (`09de`) reads it, then `77c5` must match.
- **Checksum** (`77c5`): ax = 454ch; for each byte up to `83bb`:
  `add al, b / adc ah, 0 / rol ax, 1`. It is stored at `ds:83bb`. The block in the EXE has
  ffffh there (not sealed).
- **Layout** (ds addresses):

  | Address | Contents |
  |---------|----------|
  | `82db` | "ELITE Commander File" 1a |
  | `82f0` | "COMMANDER " |
  | `82fb` | cash as text |
  | `830f` | galaxy seed (3 words) |
  | `8315` | galaxy |
  | `8316/17` | chart centre |
  | `8318/19` | cursor |
  | `831a–831d` | cursor copies |
  | `831e` | zoomed chart |
  | `831f` | current system record (25 bytes) |
  | `8338` | selected system record (25 bytes) |
  | `8356` | fuel |
  | `8357–8364` | equipment counts (`8357` missiles, `835c` fuel scoops, …) |
  | `8367` | cash (32-bit tenths) |
  | `836b` | legal status |
  | `836c/836e` | rating / kills **[verify]** |
  | `8370` | name |
  | `8379` | cargo, 17 × (held, on offer) |
  | `839c–83b9` | flight and mission state (many fields, see the reference counts) |
  | `83bb` | checksum |

  A system record holds: name, `+0a` index, `+0b` distance word, `+0d` government, `+0e`
  economy, `+0f` tech, population, species, productivity, radius, description seeds.
- **Checked:** `re/emu/cmdrtest.py` (checksums of 6000 blocks; default block).

## Objects in flight (`update_objects` complete except scooping)

Scooping has since been ported too (`core/`).

- **Planet/sun pass** (`41aa`): slots of type ≥ 30 with flag bit 6, largest `+3c` first (a
  value of 0 is never drawn), `44c7` each.
- **Planet** (as first written; see the correction under "Flight loop subsystems": type 30
  is the sun):
  - Falling: `ds:83ae` counts down; at 0 the size `ds:83ad` grows by a quarter, and ≥ 256
    crashes. Otherwise `apparent_size(100)` → `ds:54c1`.
  - With Tribble sprites on screen (`ds:0aa4`), a step of the RNG; when old A_hi ≤ 1388h
    they squeak (`4e1a`, the "surface sound" by its original name).
  - Size ≥ d2h (close to the sun) kills Tribbles: `ds:83b5` (how many) loses 10h, to 0,
    and so does `ds:0aa6` (their sprites).
  - Mask 1/3/7 at sizes < 28h / < b4h / else.
  - Size ≥ c3h with fuel scoops adds 6 to fuel (on carry: full, message `ds:2bf6` for 5,
    sound 2).
  - Size ≥ fdh crashes (`6cfa`: `ds:76bd` = 1 unless `ds:ae23`).
  - The disc is at `(x·256/z + 98h, y·256/z + 3eh)` (unsigned divide of the absolute value;
    divide error = not drawn). It is skipped when it overflows or is off the view. The
    colour comes from slot byte `+0b`.
- **Sun:** `apparent_size(50)`, heat `ds:54c3 = 2·min(~size, 7fh)`, crash at ≥ fdh, plain
  disc (mask 0).
- **Scanner** (`4359`, in flight, slots 0–19, not planet/sun):
  - Blinking when `+1e` bit 5 is set: bit 6 hidden; `+34` counts 20 hidden / 25 shown
    frames; hidden objects also get bit 1.
  - The blip (`2995`) is placed from the camera position: x high byte + a0h in 60h..deh;
    `b0h − hi(z/4 + z/16)` in a0h..c0h; plus `hi(y/4 − y/16)` in a0h..c0h. Then
    `+1e |= 2` (on the scanner: explosions do not count down while on it).
  - Colour by type from `ds:265e`.
- **Explosion end** (`7e82`): the active bit is cleared.
- Sounds go through `4c98` (id in al); the core logs them as events. The compass (`487e`)
  only draws.
- **Checked:** `re/emu/subtest.py update_objects` on flight states from `corpus.py`.

## Flight loop subsystems

The flight loop (`a027`, top `a040`) calls, in order:

1. `0299` key map, `3921` flash, `a3f4`, `3130` clear view;
2. `549f`, `4fa3`, `update_objects`, `ae50`, `ac52`;
3. `fuel_leak` (`75d5`), `message` (`702a`), `4f34` crosshair;
4. `77e0`, `controls` (`a63d`), `66d6`, `1221`, frame wait;
5. then `a183` laser, `03c0` commands, `a5ee`, `a0ed`, `tunnel` (`a0cc`), `energy_drain`
   (`a52f`).

`ds:76bd` (dead) ends the loop.

- **Messages** (`702a`):
  - Data: `ds:8058` text, `805a` countdown, `805b` shown flag (the high byte of the same
    word), `8056` last drawn.
  - When the countdown is over, it shows the view's name by `ds:b0de` (0 Front, 400h Rear,
    200h Left, else Right).
  - Warnings come first (`7129`, not while `ds:b126`). While `81f4` counts, it re-posts
    `81f2`; else it runs the four checks in turn from `81f5 + 1`:

    | Warning | Condition |
    |---------|-----------|
    | INCOMING MISSILE | `8892` = 1 (cleared) |
    | ALTITUDE LOW | `54c3` < 32h |
    | TEMPERATURE HIGH | `54c1` ≥ e1h |
    | ENERGY LOW | `54c8` < 100h |

    Each shows for 20 frames, with sound 0bh except for the missile.
- **Correction:** type 30 is the **sun** (`54c1` its size → temperature; scooping, flares
  `aa4`, falling in `83ae/83ad`); type 31 is the **planet** (`54c3` = altitude).
- **Fuel leak** (`75d5`): `83a5` counts down, then 51 frames of −5 fuel with FUEL LEAK!.
- **Energy** (`54c8`, start 3ffh): −2 a frame while `b139` = 1 (`a52f`); damage `a544`.
- **Laser** (`a183`, not while `b126` or the launch tunnel). It fires when all hold:
  - the fire key;
  - a laser in this view (`4f4b`: mount bit `(b0de >> 9) & 3` → shift {1,4,2,3} into
    `8365`; type = 2 bits of `8366`);
  - temperature `54c2` < f0h;
  - `b3d3` clear.

  Firing adds 5 to the temperature. Pulse lasers (type 0) fire every other frame (`b125`).
  The fired type goes to `b0e3`, and `b0e4` = 1.
- **Launch tunnel** (`a0cc`): `ae23` counts down (no crashing meanwhile); at 0 back to the
  docked screens (`a012`), and `83b5` is set to 1 if nonzero.
- **Controls** (`a63d`):
  - Speed `af56` ±4 by the faster/slower keys (4..48); `af58` = changed.
  - Steering (`0f27` keyboard): `10ea` arrows build up to ±23 while held (`09d3–09d6`).
    Per-axis accumulators `09d1/09d2` clamp at ±23. Options: `b135` self-centre by 3 a
    frame, `b134` reversing stops; `af49` invert options `b136/b137`.
  - Roll: `76dc += 2·clamp(−roll)`.
  - Pitch: slots 3 = −2·pitch, 2..0 = −angles; rotate (0,0,10000) back (`6dd9`); angles
    from `atan2` (`6e1c`: octant by signs, ratio `a·32768/b` searched in the tangent table
    `ds:7410`, 45° on overflow) into `af4c/af4e`; and from (0,−10000,0) the new third
    angle.
  - Then (unless the docking computer `af14` flies) the velocity `af50–af54` =
    (0, speed[·32 if `b0dd`]) rotated by slots 3 (`76da + 400h`) and 4
    (`−(76d8 + 400h)`), and every slot's position −= velocity (`a7b1`).
- **Checked:** `re/emu/subtest.py NAME --fuzz N` for update_objects, message, fuel_leak,
  energy_drain, laser, tunnel, controls (corpus states plus fuzzed fields).

## Laser hits (`ac52`)

- **Flight generator.** A third generator (`4f20`, 84 callers): the twist on the
  commander's seed words `ds:830f..8313`, returning old `w0 + w1`. It is saved with the
  commander.
- **Target** (`abd1`): slots 2 .. `ds:7fde`−1 with flags `81h` and not `+1e` 60h.
  - Hit box = the byte-swapped word `ds:b0e5[type]` / camera z, + 2.
  - `|x|·256/z` and `|y|·256/z` must be below it. The dividend is built with `cwd`, so
    |x| = 8000h always overflows; divide errors skip the slot (via `ds:01f8`).
  - The nearest (smallest z) wins.
- **Hit:** `+1e |= 1`, sound 0fh.
  - Damage = laser type + 1 (`ds:ae22` = 1 for the mining laser on an asteroid).
  - Stations (types 0, 1): the docking computer goes off, and either the damage is halved
    (rounded up) when `ds:83aa` = 1 (station already hostile), or legal status +40.
  - `+30 += damage` (max ffh); `+2b` (energy) −= damage.
  - Energy below zero:
    - if `+1e` bit 2 (cannot be destroyed): energy 0 and, for a station, hostile →
      `83ab` = 1, `83aa` = 0 and destroyed; else legal +40 (on overflow ffh and docking
      computer off);
    - otherwise destroyed: `ad4f` rewards, `ad1e` mission note (`ds:54ca` = 2 and the slot
      is `ds:b0e1` → message b21bh), explosion `7ea8`, beam.
  - No kill: beam, laser sound 14h + type.
- **Rewards** (`ad4f`):
  - Mission 4, state 2 and `+25` = 1 → state 3, kills + 1, message aed8h.
  - Mission 6, state > 1, `+25` = 2 → state − 1.
  - Bounty `+31`:
    - 0 → message af97h unless one is showing;
    - ffh → a Thargoid (type 22) pays 500 (50.0 Cr); police (type 28 with `+3a` = 1) cost
      legal +4; anything near the station (`ds:7680` bit 0) +2;
    - else the bounty is paid: kills + 1 (`ds:836c`), "BOUNTY: nnn.n Cr" in `ds:805c`
      (`7092`), cash + bounty (`8e3c`; the cash text is rewritten by `6fca`: ten digits,
      leading zeros blanked, point before the last).
  - In witchspace (`ds:83a4`) Thargons (7) take 5 and Thargoids 35 off it, down to 1 →
    message aec1h.
- **Beam** (`53e2`): one flight-generator step jitters the end (96h + r&3,
  3ch + (r>>8)&3). Lines are drawn from the bottom of the view, colours cycling in
  `ds:54b9–54bb` by laser type.
- **Checked:** `subtest.py laser_hits --fuzz 10` (a random ship placed near the crosshair).

## Collisions, docking and enemy fire

- **Collisions** (`66d6`, slots 0 .. `ds:7fde`−1): a hit is inside the type's radius
  (`ds:7614`: 275 for stations, 100 else) on all three position words.
  - Ships: 450 damage, rewards (`ad4f`, `ad1e`), and the ship is removed.
  - Stations (`+0c` bit 0 marks "already touching"; cleared when outside):
    - touching again, out of view, or not lined up within 250 → crash 1500 (and the
      station slot is cleared!);
    - lined up within 100 → docked (`ds:7613` = 1, docking computer off) if the station is
      not hostile, `|x|, |y| < 90` and the station was not hit this frame; else crash.
      "Lined up" (`681f`): angle 0 ≈ 0 and angle 1 ≈ 400h, or angle 0 ≈ 400h and angle
      1 ≈ 0; and angle 2 ≈ the station's `+0e` or + 400h (11-bit signed differences,
      `67eb`);
    - lined up within 250: inside ±110 → bounce, 30 damage; else 400.
- **Damage** (`67ab`, not in the launch tunnel): ≥ 256 takes the fore shield `ds:54c4` and
  the rest from energy; else the fore shield takes it, with any overflow from energy.
  Energy below zero → crash (dead).
- **Enemy fire** (`ae50`; `ds:7612` is set by an attacker in slot `ds:7610`):
  - sound 17h;
  - when the attacker is in view: a beam from a random edge point of the view (one
    flight-generator step: top / bottom / left / right by the value) to its screen point
    (`aef7`; centre on a divide error), drawn clipped (`2576`);
  - 15 off the aft shield `54c5` if `ds:7681` bit 7, else the fore shield (sound 19h);
  - overflow from energy (sound 1); energy below zero → dead.
- **Checked:** `subtest.py collisions|enemy_fire --fuzz 10` (ships and stations placed
  close, docking approaches, attackers).

## Trading (station)

- **Buy** (`96de`, row `ds:ad2b`). It needs stock on offer, and:
  - goods rows 0–12 need room (`ds:839c` tonnes < 20, 35 with the cargo bay extension
    `8358`; else CARGO BAY FULL `ad2e`);
  - rows 13–16 (kg/g) refuse when **on offer** ≥ 250 (`ad3e`; the original tests the wrong
    byte).

  The price comes from the price table, paid by `8e23` (32-bit; refused → `ad50`). Then
  held +1, offer −1, tonnes +1.
- **Sell** (`9781`): held −1, offer +1 (stays at 255), tonnes −1, cash + selling price.
  `98d4` adds the item's illegal flag (third byte at `ds:92a1`) to the legal status.
- **Equipment** (`932f`):
  - Fuel (row 0; mission 1 refuses with `8dad`; ≥ fbh refuses with `adaa`): a full tank
    costs `((255 − fuel)·7 · price) >> 8`; otherwise it buys what the low word of the cash
    buys, `(cash_lo·256/price)/7` units, for all of that low word.
  - Missiles: at most 4 (`ad64`).
  - Other items: once only (`8d5a`).
  - Lasers (rows 4, 5, 12, 13 → type 0–3, `8df7`) need a free mount (`92e6`); the mining
    laser needs fuel scoops (`8d7a`).
  - Once paid, count + 1. A laser goes to the only free mount, or the player picks one
    (menu at `94bf`; `9524` fits the n-th free mount: a bit in `8365`, two type bits in
    `8366`).
- **Checked:** `subtest.py buy|sell|equip --fuzz 10` (random cargo, cash, equipment,
  system).

## Sound

- **Entry.** Every sound goes through `4c98` with a number. With the speaker
  (`ds:4801` = 2), `ds:45c0` maps the number to one of twelve sequences (`ds:4f7f`; bit 7
  set: the index itself). Otherwise the AdLib/Roland driver (segment `2270`, far calls)
  plays it.
- **Speaker sequencer.** It runs in the timer interrupt (`4a99` → `4a50`,
  1193182 / 5555h Hz):
  - clock `45e0` (not while paused, `45e6`), countdown `45e4`;
  - then, unless sound is off (`45e7`) or stopped (`45ea` bit 0): the next note when due
    (`4aea`) and a tick of it (`4b6b`).
- **Sequences** are notes (pattern index, pitch, length; `ff` pitch keeps the last) and
  rests (`fe n`); `ff` ends a sequence.
- **Patterns** (`ds:4fce`) bend the pitch each tick and use these codes:

  | Code | Effect |
  |------|--------|
  | `80 n` | wait |
  | `81 n` … `82` | loop (a 3-byte stack at `45f4`) |
  | `83` | end a held note |
  | `7f` / `7e` | noise on / off (LFSR `45dc`) |

  Pitches index the PIT divisors at `ds:4601`.
- **`45ea` flags:** 1 stopped, 2 next note due, 4 speaker to be turned on, 8 noise, 10h
  held note.
- **Wrappers** decide what may play:
  - the laser (`4dc9`) does not cut short a sound marked by `4deb/4df5/4dff` (`ds:4fe0`)
    that is still playing;
  - under fire (`4da4`) is `10h` (`17h` on AdLib) and does not play over another speaker
    sound;
  - the surface sound (`4e1a`) writes its pitch into sequence 9 (`ds:4f74`) on the speaker;
  - the title music restarts once its sequence ends (`4d8e`).
- **Port.** Ported in `core/ep_sound.c`; the speaker's output for the frontend is
  `g->speaker` (divisor) and `g->speaker_on`.
