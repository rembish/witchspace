# Flight loop: star dust, dashboard, Tribbles, timers, commands (analysis notes)

Working notes from a read of the remaining flight-loop routines; confirmed items move to
NOTES.md as the subsystem tests pass.

## Corrections

- The six main-RNG steppers `123b 1268 12c8 1322 1341 139a` are all in `1221`: **Tribbles**.
  `ds:0aa4` = Tribble sprites shown, `0aa6` = sprite count, `83b5` = Tribbles aboard (bought
  for 5000.0 Cr at `8b63`); the sun's heat (`4555`) kills them.
- Flight uses key-map row 0 (F1 dock/back, F2 market, F3 status, F4 chart, F5 view, F6 ECM,
  F7 arm missile, F8 fire missile / masking device, F9 energy bomb, F10 escape capsule, F11
  jump drive, F12 anti-ECM; 1..9 0 - = the same); row 1 docked, 2 info screens, 3/4 pause, 5
  title. Command table indices 10h–15h: `5dc2 6189 5d9f 924a 07aa 08ab`, 16h–1fh options.
- `763e` draws the flight frame (when `8711` ≠ 0); objects are set up by `64d0`.

## Star dust `4fa3`

30 particles at `ds:5314` (7 bytes: x, y i16, life u8, unused, colour u8), shadow copy at
+d2 (old x, y, life, "respawned" flag). In range (`5307`): signed hi(x) in −20h..1fh, hi(y)
in −10h..0fh; inner box (`531f`): hi(x) −6..6, hi(y) −3..3. Shift `54b8` (`51ac`) =
(34h − speed)/12 + 4 (−1 in jump mode). Inputs: last frame's steering word `09d7` (after the
invert options, `af49/af5e`), speed. Per view (`b0de`: 600h right, 200h left, 400h rear,
else front): vertical shift (`5265`, dx from pitch or roll), horizontal shift (`51f0`, side
views, from speed), rotation (`6d19` slot 3 + `524c` all particles, no range test), then
the loop: front: out of range skipped; moved by (x >> s, y >> s); respawn (`52ce`) if out
of range; rear: moved back, respawn if inside the inner box or the life runs out; side views
only draw. Respawns take two flight-generator steps (`52ce`: life (r1&1f)+28h, colour
(r1>>8)&f, x = r2 sar 2, y = bswap(r2) sar 3; `528e`, `5217` for the shifts). Jump mode
draws streaks from the shadow. `5374` reset: for each particle x, y = r sar 1 retried until
hi in ±23h (wider than visible: such particles stay frozen until a rotation), life, colour.
Pixel `2973`: x = dl + dl/4 − 8; MCGA colours `ds:2656[c & 7]`.

Ported as `core/ep_dust.c` (difftested: `dust`, `dust_reset`). Quirks kept: a particle
re-entering at the top or bottom (`528e`) takes its colour from the high byte of its
out-of-range y, not from the generator; the rear view decrements the life of a freshly
respawned particle too; `5374` clears the message timer when its low byte is 0.

## Dashboard `549f`

Cached gauges (`54cc..54e1`, reset to 80h by `5490`). State it writes: status `54cb`
(`585c`: 0 red (E < 100h or S ≥ e0h or A < 20h), 2 yellow, 3, 1 green), safe zone `7680`
(`6a45`: slot 2 station, fits16, `6a72` magnitude < 32c8h → bit 0; the other bits keep the
low byte shifted), `579d` recharge: laser temp −2; energy 3ffh → shields +1 each, else
energy += (`835f`·2 + 1) (extra energy unit) up to 3ffh; energy < 100h: `r < 50` then
`r2/20` picks an equipment byte `8357 + k` to lose one (message "Equipment Loss due to Low
Energy" 54e2h, 28h frames). Clears `54c0` (ECM shown).

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
step() = main generator, returning the old A (hi, lo).

## Timers and commands

- `a3f4` missile lock (`54ca` = 1 armed): `abd1` target not a mission ship → `b0e1` = slot,
  `54ca` = 2, message (`70b9`), sound 4.
- `a5ee` jump drive (`b0dd`): speed must be 48 ("Velocity-Locked" b04ah), mass lock `afa9`
  (safe zone, sun or planet within 16 bits, any ship on the scanner but rocks) → off ("Mass-
  Locked" b030h); else "Engaged" b01dh; `af58` = 1 every frame.
- `a0ed`: `ae25` countdown; hyperspace countdown `ae60/ae61` (10 frames a step, `ae25` text);
  at 0 sound 1bh/1ah and `72d8` → `72f0` arrival; escape capsule countdown `b3d5`.
- `72f0` arrival: fuel −= `82d6`, legal −5, copy the target record (`8611` or galactic
  `8338`), `839d` = 0, `610e`; misjump when `4f20() < 366h` and no mission (or `8610`):
  witchspace `83a4` = 64h between the systems (`7500`); chart cursors; tunnel rings `74e3`
  (50 frames; rings `7499` step the main RNG through the outline circle when `108f` ≠ 0);
  `666b` new system (`64d0`, then 4 flight-generator steps for the arrival offsets and roll);
  `753c` mission counters (`839e` jumps → missions 1–6 at 20h 38h 50h 6eh 8ch a0h).
  Galactic jump: galaxy + 1 (8 → 0; 7 → 8 only when `4f20() < 12ch`), cursor random.
- Ported (`core/ep_travel.c`, `core/ep_chart.c`; difftested `arrive`, `rings`, `witchspace`,
  `jump_missions`, `flight_start`, `new_system`, `select_system`). `ds:108f` is 0 after every
  planet or sun draw (`468d`, also the divide-error resume), so the rings normally do not step
  the RNG. Galaxy 8 has its own seed (`1234 5678 9abc`).
- `64d0` flight start: dust reset (`5374`), slots cleared, sun (3 steps for x, y, z), planet
  at z 6e00h, station (type 0 if tech ≥ 9 else 1, `+1f` = (r>>8&7)+10), `76b6` = government.
- `6864` tunnel animation (launch, docking): 20 frames; on launch dust, `update_objects` and
  `a7b1` run each frame.
- Commands: `a22a` dock/launch (docking computer toggle `a557`: costs 50.0 Cr without the
  docking computer fitted), `a1cf` view cycle (0 → 400h → 200h → 600h → 0) + dust reset,
  `a4a0` ECM (removes all missiles, −20 energy), `a3b4` arm/disarm missile, `a41f` fire
  (`4f20() < 1f4h` jams, else `80fb`), `a4da` masking device (−120 energy, everyone calms
  down), `a4c4` energy bomb (`6ab2`: every ship on the scanner explodes, +40 legal near the
  station), `a464` escape capsule (`b3d5` countdown or `6aeb`), `a5de` jump drive toggle,
  `a510` anti-ECM (`b139`), `a314` hyperspace (needs fuel: `(fuel·10)/36` ≥ distance; cost
  `82d6` = distance·36/10), `a293` galactic hyperdrive. Return to flight (`03c0` handlers)
  skips the rest of that frame.

## Ported: the whole flight loop

`core/ep_frame.c` (`ep_flight_frame`, a040..a0c9), `core/ep_commands.c` (key bar 0299,
commands 03c0, countdowns a0ed, escape capsule 6aeb, player missile 80fb, energy bomb 6ab2),
the docking computer (a7de, in `ep_controls`) and the death (6bc9). Checked: `loop` (one
frame from a040 back to a040, or out to docking, a screen or the title), `frame`,
`key_bar`, `commands`, `countdowns`, `controls`. Findings:
- `ds:8711` is "a screen other than the space view is up", not "docked".
- `ds:b126` is the death countdown (60 frames, then the title).
- The escape capsule sets `ae23` = 100: after 100 frames the tunnel ends at the station.
- A bar slot can only hold `ff` (redraw marker) before 0299 runs, never at 03c0.
- The player's missile copies the 64 bytes at DI (stale): slot 20 where the collision loop
  stops, unless Tribble sprites moved DI (not reproduced).
- DL at the AI is what the drawing last left (`render.dl`): a sprite the low byte of its
  width (3777, from ELITE.GRF: `ep_sprite_width`), text its last glyph's last row address,
  `y·320 + x + 8·320` (2e52). Nothing else between them and 77e0 writes DL. The harness runs
  the original's drawing (observed, not replaced), or its DL would be wrong.
- The docking computer divides by zero closer than one step to its docking point (a969:
  m < speed gives BX = 0). The divide error resumes at the stale `ds:01f8`, usually the
  compass's (48c9/48e7): the compass's tail runs with stray registers, draws a sprite and
  returns past the rest of the step. The core divides by 1 instead. Its roll match stores the
  11-bit sign-extended angle.
- `find_nearest` (5fe1) keeps the caller's BP when no system lies in the zoomed chart's
  window (which a galaxy's spread of systems seems never to allow). BP is then what the
  drawing last left: 140h after text (2e3f), 140h minus the width after a sprite (377f), or
  the line rasterizer's step flag (26da, 0 or 1). The core picks system 0.

