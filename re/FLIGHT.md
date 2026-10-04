# Flight loop: star dust, dashboard, Tribbles, timers, commands (analysis notes)

Working notes from a read of the remaining flight-loop routines: the star dust, the
dashboard state, the Tribbles, the timers and countdowns, hyperspace and arrival, and the
flight commands. Confirmed items move to NOTES.md as the subsystem tests pass; the last
section lists what is ported and what the tests found.

Addresses as in NOTES.md: a 4-digit hex value such as `a040` is a code offset in segment
`0000` of the unpacked `ELITE.EXE`; `ds:xxxx` is a data-segment address. Bare numbers that
name variables (`54c8`, `b0de`) are data addresses too. `+xx` is a byte offset into a
64-byte object slot. "Flight generator" is `4f20` (see NOTES.md, "Laser hits"); "main
generator" is the RNG at `ds:0205`.

## Corrections

- **The six "unidentified" main-RNG steppers are the Tribbles.** `123b 1268 12c8 1322 1341
  139a` are all in `1221`.
  - `ds:0aa4` = Tribble sprites shown;
  - `0aa6` = sprite count;
  - `83b5` = Tribbles aboard (bought for 5000.0 Cr at `8b63`).

  The sun's heat (`4555`) kills them.
- **Key-map rows.** Flight uses key-map row 0:

  | Key | Command |
  |-----|---------|
  | F1 | dock / back |
  | F2 | market |
  | F3 | status |
  | F4 | chart |
  | F5 | view |
  | F6 | ECM |
  | F7 | arm missile |
  | F8 | fire missile / masking device |
  | F9 | energy bomb |
  | F10 | escape capsule |
  | F11 | jump drive |
  | F12 | anti-ECM |

  The keys 1..9 0 - = do the same. The other rows: 1 docked, 2 info screens, 3/4 pause, 5
  title. Command table indices 10h–15h are `5dc2 6189 5d9f 924a 07aa 08ab`; 16h–1fh are
  options.
- `763e` draws the flight frame (when `8711` ≠ 0); the objects are set up by `64d0`.

## Star dust `4fa3`

- **Particles.** 30 particles at `ds:5314`, 7 bytes each: x, y (i16), life (u8), unused,
  colour (u8). A shadow copy at +d2 holds old x, y, life and a "respawned" flag.
- **Ranges.**
  - In range (`5307`): signed hi(x) in −20h..1fh, hi(y) in −10h..0fh.
  - Inner box (`531f`): hi(x) −6..6, hi(y) −3..3.
- **Shift.** `54b8` (`51ac`) = (34h − speed)/12 + 4 (−1 in jump mode).
- **Inputs.** Last frame's steering word `09d7` (after the invert options, `af49/af5e`) and
  the speed.
- **Per view** (`b0de`: 600h right, 200h left, 400h rear, else front):
  1. vertical shift (`5265`, dx from pitch or roll);
  2. horizontal shift (`51f0`, side views, from speed);
  3. rotation (`6d19` slot 3 + `524c` all particles, no range test);
  4. then the loop:
     - front: out-of-range particles are skipped; the others move by (x >> s, y >> s);
       respawn (`52ce`) if now out of range;
     - rear: moved back; respawn if inside the inner box or the life runs out;
     - side views: only draw.
- **Respawns** take two flight-generator steps (`52ce`): life (r1&1f)+28h, colour
  (r1>>8)&f, x = r2 sar 2, y = bswap(r2) sar 3. `528e` and `5217` respawn for the shifts.
- **Jump mode** draws streaks from the shadow.
- **Reset** (`5374`): for each particle, x, y = r sar 1, retried until hi is in ±23h (wider
  than visible: such particles stay frozen until a rotation); then life and colour.
- **Pixel** (`2973`): x = dl + dl/4 − 8; MCGA colours `ds:2656[c & 7]`.

Ported as `core/ep_dust.c` (difftested: `dust`, `dust_reset`). Quirks kept:

- a particle re-entering at the top or bottom (`528e`) takes its colour from the high byte
  of its out-of-range y, not from the generator;
- the rear view decrements the life of a freshly respawned particle too;
- `5374` clears the message timer when its low byte is 0.

## Dashboard `549f`

The gauges are cached in `54cc..54e1` (reset to 80h by `5490`). The routine also writes
this state:

- **Status** `54cb` (`585c`): 0 red (E < 100h or S ≥ e0h or A < 20h), 2 yellow, 3, 1 green.
- **Safe zone** `7680` (`6a45`): slot 2 is the station, fits16, and the `6a72` magnitude
  < 32c8h → bit 0. The other bits keep the low byte shifted.
- **Recharge** (`579d`):
  - laser temperature −2;
  - energy 3ffh → shields +1 each; else energy += (`835f`·2 + 1) (extra energy unit) up to
    3ffh;
  - energy < 100h: `r < 50`, then `r2/20` picks an equipment byte `8357 + k` to lose one
    (message "Equipment Loss due to Low Energy" 54e2h, 28h frames).
- Clears `54c0` (ECM shown).

## Tribbles `1221`

```
if (T == 0) return
if (T == 1) { (h,_) = step(); if (h > 0fh) return; T++; [0aa4] = 0 }
(h,_) = step(); lim = T<15 ? 250 : T<30 ? 500 : T<80 ? 750 : T<125 ? 2000 : 10000
if (h <= lim && T <= 98c9h) T++
if (T >= 5fh) { (h,l) = step(); if (h <= 0fa0h) { b = l & 1eh;
    if (cargo[b]) { cargo[b]--; T++; if (b < 1ah) tonnes-- } } }   // they eat the cargo
if (T <= 2abh) return
if (![0aa4]) { [0aa4] = 1; [0aa6] = 0 }
(h1,_) = step(); (h2,_) = step()
if ((h1 >> 8) < 0ah) { if ([0aa6] == 40h) [0aa6]--;
  x = (h1 & ff) + ((h2 >> 8) & 3f); y = h2 & ff;
  if (x >= 8 && x < 128h && y < 0bdh) { v = 0; if (y >= 9 && y < 7ah) { (h3,_) = step();
    v = h3 & 7; if (v >= 3) v -= 5 } append (x, y, v); [0aa6]++ } }
sprites at 0aa8 (8 bytes: x, y, dx): walk, bounce at 8 / 128h
```

T is the Tribble count `83b5`. `step()` is the main generator, returning the old A (hi,
lo).

## Timers and commands

- **Missile lock** (`a3f4`; `54ca` = 1 means armed): if `abd1` finds a target that is not a
  mission ship → `b0e1` = slot, `54ca` = 2, message (`70b9`), sound 4.
- **Jump drive** (`a5ee`; `b0dd`):
  - speed must be 48 ("Velocity-Locked" b04ah);
  - mass lock `afa9` (safe zone, sun or planet within 16 bits, any ship on the scanner but
    rocks) → off ("Mass-Locked" b030h);
  - else "Engaged" b01dh; `af58` = 1 every frame.
- **Countdowns** (`a0ed`): `ae25` countdown; hyperspace countdown `ae60/ae61` (10 frames a
  step, `ae25` text); at 0, sound 1bh/1ah and `72d8` → `72f0` arrival; escape capsule
  countdown `b3d5`.
- **Arrival** (`72f0`), in order:
  - fuel −= `82d6`, legal −5;
  - copy the target record (`8611`, or galactic `8338`); `839d` = 0; `610e`;
  - misjump when `4f20() < 366h` and no mission (or `8610`): witchspace `83a4` = 64h
    between the systems (`7500`);
  - chart cursors;
  - tunnel rings `74e3` (50 frames; the rings `7499` step the main RNG through the outline
    circle when `108f` ≠ 0);
  - `666b` new system (`64d0`, then 4 flight-generator steps for the arrival offsets and
    roll);
  - `753c` mission counters (`839e` jumps → missions 1–6 at 20h 38h 50h 6eh 8ch a0h).
- **Galactic jump:** galaxy + 1 (8 → 0; 7 → 8 only when `4f20() < 12ch`); the cursor is
  random.
- **Port.** Ported in `core/ep_travel.c` and `core/ep_chart.c` (difftested `arrive`,
  `rings`, `witchspace`, `jump_missions`, `flight_start`, `new_system`, `select_system`).
  - `ds:108f` is 0 after every planet or sun draw (`468d`, also the divide-error resume),
    so the rings normally do not step the RNG.
  - Galaxy 8 has its own seed (`1234 5678 9abc`).
- **Flight start** (`64d0`):
  - dust reset (`5374`), slots cleared;
  - sun (3 steps for x, y, z);
  - planet at z 6e00h;
  - station (type 0 if tech ≥ 9 else 1, `+1f` = (r>>8&7)+10);
  - `76b6` = government.
- **Tunnel animation** (`6864`, launch and docking): 20 frames; on launch, dust,
  `update_objects` and `a7b1` run each frame.
- **Commands:**

  | Routine | Command | Notes |
  |---------|---------|-------|
  | `a22a` | dock / launch | docking computer toggle `a557`: costs 50.0 Cr without the docking computer fitted |
  | `a1cf` | view cycle | 0 → 400h → 200h → 600h → 0, plus dust reset |
  | `a4a0` | ECM | removes all missiles, −20 energy |
  | `a3b4` | arm / disarm missile | |
  | `a41f` | fire missile | `4f20() < 1f4h` jams, else `80fb` |
  | `a4da` | masking device | −120 energy, everyone calms down |
  | `a4c4` | energy bomb | `6ab2`: every ship on the scanner explodes, +40 legal near the station |
  | `a464` | escape capsule | `b3d5` countdown or `6aeb` |
  | `a5de` | jump drive toggle | |
  | `a510` | anti-ECM | `b139` |
  | `a314` | hyperspace | needs fuel: `(fuel·10)/36` ≥ distance; cost `82d6` = distance·36/10 |
  | `a293` | galactic hyperdrive | |

  Returning to flight (the `03c0` handlers) skips the rest of that frame.

## Ported: the whole flight loop

The whole flight loop is ported:

- `core/ep_frame.c`: `ep_flight_frame`, a040..a0c9;
- `core/ep_commands.c`: key bar 0299, commands 03c0, countdowns a0ed, escape capsule 6aeb,
  player missile 80fb, energy bomb 6ab2;
- the docking computer (a7de, in `ep_controls`) and the death (6bc9).

Checked by the difftests `loop` (one frame from a040 back to a040, or out to docking, a
screen or the title), `frame`, `key_bar`, `commands`, `countdowns` and `controls`.

Findings:

- `ds:8711` means "a screen other than the space view is up", not "docked".
- `ds:b126` is the death countdown (60 frames, then the title).
- The escape capsule sets `ae23` = 100: after 100 frames the tunnel ends at the station.
- A bar slot can only hold `ff` (redraw marker) before 0299 runs, never at 03c0.
- The player's missile slot first gets the 64 bytes at DI as the code before the commands
  left it (8103, 81b7). On MCGA that is the frame's flip (30c2): `ds:d828`, memory nothing
  writes, so the slot starts from zeros. On EGA/VGA the flip leaves DI alone: slot 20, where
  the collision loop stops, or the Tribbles' table when their sprites are on screen (that
  case is not reproduced; the frontend uses MCGA). A slot taken at random (80e2) is copied
  onto itself. The harness lets the flip run (all but its wait) so DI is the real one.
- **DL at the AI** is what the drawing last left (`render.dl`):
  - after a sprite, the low byte of its width (3777, from ELITE.GRF: `ep_sprite_width`);
  - after text, its last glyph's last row address, `y·320 + x + 8·320` (2e52).

  Nothing else between them and 77e0 writes DL. The harness runs the original's drawing
  (observed, not replaced), or its DL would be wrong.
- **Docking computer divide by zero, a bug of the original.** Flying to the point before
  the slot, it divides the distance by the speed for the steps left (a963) and then the
  offset by that (a973). It handles one step left (`dec ax; je`), not none: closer than
  one step, BX = 0 and the division faults. The divide error resumes at the stale
  `ds:01f8`, usually the compass's (48c9/48e7): the compass's tail runs with the docking
  computer's registers, draws a sprite and returns past the rest of the step.
  - Reaching it needs the approach (step 4) to start within 4 units of the point, an
    invisible spot 2000 units before the slot. 3,900 simulated dockings never did; 41 of
    2,000,000 random direct approaches did, every one starting that close. Switching the
    computer off and on at the point does not get there either: the pitch toward a point
    the ship is on never ends (step 3), in the original and the core alike.
  - Once there, the original faults every frame and never leaves step 4. The core divides
    by 1 instead: the ship moves onto the point, and then stays in step 4 the same way,
    without the stray sprite.
  - Its roll match stores the 11-bit sign-extended angle.
- **The docking computer flies through the station, a bug of the original.** Step 4 flies
  a straight line to the point 2000 before the slot (the slot faces +z, the planet's side)
  with no thought for what is in the way. From behind the station, within about 9° of its
  rear axis at the safe zone's edge (wider closer in), that line crosses the station's box
  (±275, `ds:7614`), and the collision (`66d6`) finds the ship not lined up: 1500 damage,
  dead.
  - Arrivals come in near the station's equator (|x|, |y| ≥ 20000h, |z| < 8000h) and
    launches leave the ship in front of the slot, so only a ship that has gone round the
    back gets there. George Hooper's guide blamed engaging it "immediately after entering
    protected station space"; the timing does not matter, the side does, and both his
    remedies (fly in, line up first) move the ship to the slot's side.
  - In the core, 27 of 3000 random directions at 13000 crash (55 of 3000 at 3000), all
    near −z. The original, run in the emulator frame by frame with the station 12000 away:
    from (0,0,−1) it flies into the station and dies; from the side or the front it docks.
    The core does the same.
- **`find_nearest`** (5fe1) would keep the caller's BP if no system qualified. That cannot
  happen in play:
  - Zoomed, a system must lie in the window around the chart's centre (±13h, ±10h). The
    centre is only set on arrival: to the system's own position, or after a misjump to the
    midpoint between the target and the old centre. A jump is refused at 7.0 light years or
    more from the centre, so the target is at most 9 from the midpoint on either axis
    (checked for every centre and every allowed target). `ds:8610`, which would force a
    misjump on a galactic jump, is only ever cleared.
  - Unzoomed, the nearest system must be under ffffh squared away. The farthest any cursor
    is from a system is about 4e00h squared (all nine galaxies checked).

  Only a hand-edited commander file (a chart centre far from every system) gets there; BP is
  then what the drawing last left (140h after text, 140h minus a sprite's width, or the line
  rasterizer's 0 or 1). The core picks system 0.
