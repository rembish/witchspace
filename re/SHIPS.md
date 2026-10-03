# Ships: spawning, AI frame, explosions (analysis notes)

Working notes from a full read of `77e0` and the 7700–8352 helpers (Ghidra listing, `disasm.py`,
table dumps). Everything here is to be confirmed by the subsystem tests as it is ported; items
the tests have confirmed move to NOTES.md.

## Corrections

- The explosion does not turn the ship into type 23: `7ea8` clears the ship's active bit and
  writes flags `17h` (active, type 11) into each debris particle.
- `77e0` dispatches per AI class through `ds:8720`: `83f4 83f5 8352 84e1 84fc 8645 873b 81c7`
  for classes 0–7 (0 inert: sun, planet, hulk; 1 station; 2 missile; 3 drifting junk; 4
  trader/police; 5 hostile: pirate, Thargoid; 6 loner/bounty hunter; 7 particle).

## Slots

`ds:76de + 64·i`, count `ds:76b5` (36; title 3). At system entry `64d0`: `7fde` = 20,
`7fdf` = 16, `816b` clears all. 0 sun (flags 3dh, class 0, `+1e` 6, `+3f` ff, colour `+0b` =
b6h + (seed>>8 & 3)); 1 planet (3fh, z 6e00h); 2 station (`ds:775e`, type 0 if tech ≥ 9 else
1, `+0c` 400h, z −300, `+1e` 4, class 1, `+1f` = (rng>>8 & 7) + 10, `+2b` 96h, `+31` 0; not in
witchspace); 3–19 ships (`80ae` free slot search over 3..`7fde`−1); 20–35 particles (`8183`).
Removal (`7e82`) only clears bit 0 of `+00`; several routines read stale slots (`79fa`, `80cb`,
`82ef`; spawners that leave fields: `7ad3`, `81e5`, `7a12`, `80fb`).

Fields: `+17` AI state; `+18` speed (signed); `+19/1a/1b` per-frame velocity (signed bytes,
`7e32`); `+1c` engagement range byte; `+1d` max turn per frame; `+1e` bit 0 hostile, 1 on
scanner, 2 sun/planet/station, 3 debris/canister, 4 scoopable fragment, 5 mission ship, 6
mission canister / blink phase; `+1f` children to launch; `+25` spawn tag (1 mission-4 Viper,
2 mission-6 object); `+26/27` particle spin; `+29` missile target (word, 0 = player); `+2b`
energy; `+2c` canisters on death; `+2d` debris count; `+2e` particle life; `+2f` particle
age; `+30` aggression (fires if rng_lo < `+30`); `+31` bounty (0 none, ff innocent, 200
mission); `+32` missiles; `+33` AI class; `+34` off-scanner lifetime (spawn 1; 0 never);
`+35/36/38` weave state (handlers); `+3a` word: thargon parent / police flag; `+3f` table byte
8 (no reader found).

## AI frame `77e0`

```c
memset(&ds[0x8730], 0, 9);  // 8730 non-particle count, 8731+c per class
for (i = 0; i < ds[0x76b5]; i++) {
  o = slot(i); if (!(o[0] & 1)) continue;
  if (o[0x33] != 7) ds[0x8730]++;
  ds[0x8731 + o[0x33]]++;
  handler[o[0x33]](o);       // ds:8720
}
ds[0xb138] = 0;
if (ds[0x8730] >= 10) return;
if (ds[0x7fde] < ds[0x8730]) return;
if (ds[0x83a9]) { if (ds[0x83b3] == 1) goto convoy; }
else if ((ds[0x83aa] & ds[0x83b1]) && ds[0x83ab] != 1) goto siege;
mission4_viper(); mission5_thargoids(); mission6();          // 7ad3 7a66 7b32
W(0x8897) = ds[0x832c] * 8; W(0x888f) = ds[0x76b6] * 4;
if (ds[0x83a4]) goto pirates;
thr = ds[0x886f + W888f] + (ds[0x8362] == 1);
if (ds[0x8734] < thr) { r = rng(); p = jump(W(0x8899 + W8897));
  if (r < p) { if (!(s = free_ship_slot())) return; spawn_junk(s); } }
if (ds[0x8735] < ds[0x8870 + W888f]) { r = rng(); p = jump(W(0x889b + W8897));
  if (r < p) { if (!(s = free_ship_slot())) return; spawn_trader(s); } }
if (ds[0x8737] < ds[0x8871 + W888f]) { r = rng(); p = jump(W(0x889d + W8897));
  if (r < p) { if (!(s = free_ship_slot())) return; spawn_loner(s); } }
pirates:
if (ds[0x8736] >= ds[0x8872 + W888f]) return;
if (!ds[0x83a4]) { r = rng(); p = jump(W(0x889f + W8897)); if (r >= p) return;
  if (ds[0x76b6] && r >= 0x1c2) return; }
if ((s = free_ship_slot())) spawn_pirate(s);
return;
convoy: (794e) if (ds[0x83a7] == 1) return;
  if (ds[0x8736] == 0) { leader = spawn_mission(CF=1) (type 24); +1e |= 20h; +2c = 14h; +30 = 0;
    2 x { s = free; spawn_mission(s, 0); copy_and_jitter(s, leader); } return; }
  if (ds[0x8736] >= 3) return;
  s = free; spawn_mission(s, 0); if (79fa any +1e&20h in 0..19) return; if (83a7 == 1) return;
  s: +1e |= 20h, +2c = 14h, +30 = 0, flags = 31h;
siege: (79d1) if (!safe_zone) return; if (ds[0x8736] >= 8) return; s = free; spawn_thargoid(s);
```

`jump(p)` (`79e8`): ×32 while the jump drive is on (`b0dd`). Limits `ds:886f + 4·gov(76b6)`
(junk, trader, loner, pirate): 0: 1 0 4 10; 1: 2 1 4 4; 2: 2 2 3 2; 3: 1 4 3 1; 4: 1 5 3 1; 5: 1 5
2 1; 6: 1 7 2 1; 7: 1 9 1 0 (junk +1 with the mining laser `8362`). Chances `ds:8899 + 8·gov(832c)`:
0: 50 5 70 200; 1: 40 9 65 100; 2: 35 18 55 70; 3: 30 29 43 40; 4: 23 30 37 15; 5: 10 30 30 10;
6: 8 20 8 5; 7: 5 20 5 3. `76b6` = `832c` except in witchspace (0).

Missions: `7ad3` (mission 4 stage 2: a Viper `+33` 5, `+30` c8h, `+25` 1 if none with `+25` 1,
when rng ≤ 190h; no position set: stale), `7a66` (mission 5: Thargoids), `7b32` (mission 6
in system `83a3`: rng > 1388h returns; ≤ 12ch pirate; then a type-5 object `+33` 3, `+1d` 1eh,
`+25` 2). `6a3f` safe zone = bit 0 of `ds:7680` (`6a45`: station in slot 2, fits16, magnitude
`6a72` < 32c8h).

## Spawning

`7d14` (DI slot, table entry `ds:8739 + 10·n`): `+17 +1e +30` 0, `+34` 1, flags e0·2+1, `+18`
e1, `+1d` e2, `+31` e3, `+32` e4, `+2c` e5, `+2d` e6, `+2b` e7, `+3f` e8, `+1c` e9, `+25` 0.

| e | type | spd | turn | bounty | msl | can | debris | energy | 3f | 1c |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 20 missile | 70 | 40 | 0 | 0 | 0 | 2 | 0 | 0e | 0 |
| 1 | 21 pod | 16 | 10 | ff | 0 | 0 | 4 | 2 | 08 | 0 |
| 2 | 12 rock | 24 | 0 | 0 | 0 | 0 | 0 | 2 | 05 | 0 |
| 3 | 17 canister | 23 | 0 | 0 | 0 | 0 | 3 | 2 | 0c | 0 |
| 4 | 6 boulder | 45 | 0 | 1 | 0 | 0 | 3 | 2 | 14 | 0 |
| 5 | 5 asteroid | 45 | 0 | 5 | 0 | 0 | 6 | 8 | 32 | 0 |
| 6 | 11 debris | 15 | 0 | 0 | 0 | 0 | 0 | 2 | 0c | 0 |
| 7 | 8 shuttle | 12 | 8 | ff | 0 | 0 | 5 | 4 | 16 | 0 |
| 8 | 9 transporter | 15 | 8 | ff | 0 | 0 | 5 | 4 | 10 | 0 |
| 9 | 26 Cobra III | 42 | 25 | ff | 3 | 3 | 6 | 30 | 1e | 23 |
| 10 | 29 Python | 30 | 25 | ff | 4 | 5 | 6 | 40 | 23 | 23 |
| 11 | 2 Boa | 36 | 27 | ff | 2 | 5 | 6 | 35 | 23 | 23 |
| 12 | 3 Anaconda | 21 | 28 | ff | 2 | 7 | 7 | 32 | 24 | 23 |
| 13 | 5 asteroid (armed) | 45 | 20 | ff | 4 | 0 | 9 | 32 | 32 | 23 |
| 14 | 28 Viper | 48 | 30 | ff | 2 | 0 | 7 | 34 | 17 | 05 |
| 15 | 25 Sidewinder | 46 | 28 | 50 | 3 | 0 | 6 | 20 | 14 | 29 |
| 16 | 16 Mamba | 45 | 28 | 150 | 3 | 1 | 7 | 25 | 19 | 2a |
| 17 | 27 Krait | 45 | 32 | 100 | 2 | 1 | 3 | 20 | 14 | 2a |
| 18 | 15 Adder | 36 | 32 | 40 | 4 | 0 | 4 | 22 | 14 | 2a |
| 19 | 13 Gecko | 45 | 32 | 55 | 4 | 0 | 5 | 19 | 12 | 2d |
| 20 | 4 Cobra I | 39 | 28 | 75 | 4 | 3 | 5 | 21 | 13 | 2b |
| 21 | 10 Worm | 35 | 30 | 0 | 2 | 0 | 3 | 10 | 13 | 2b |
| 22 | 26 Cobra III | 42 | 30 | 175 | 3 | 1 | 7 | 27 | 23 | 28 |
| 23 | 24 Asp II | 60 | 33 | 200 | 5 | 0 | 5 | 27 | 23 | 2a |
| 24 | 29 Python | 30 | 30 | 200 | 5 | 2 | 7 | 40 | 1e | 2b |
| 25 | 23 Fer-de-lance | 45 | 35 | 0 | 4 | 0 | 5 | 30 | 1e | 14 |
| 26 | 14 Moray | 38 | 37 | 50 | 3 | 1 | 5 | 25 | 1e | 28 |
| 27 | 22 Thargoid | 50 | 28 | ff | 8 | 0 | 6 | 50 | 37 | 2d |
| 28 | 7 Thargon | 56 | 45 | 50 | 1 | 0 | 3 | 10 | 14 | 1e |
| 29 | 18 (escort) | 54 | 30 | 0 | 4 | 0 | 7 | 40 | 1e | 28 |
| 30 | 19 (escort) | 60 | 30 | 0 | 5 | 3 | 7 | 42 | 1e | 28 |

Spawners: `7b85` missile (e0, class 2), `7bac` pod (e1, 3), `7b92` canister (e3, 3), `7b9f`
Cobra III hulk (e9, 0), `7bb9` shuttle (e7, 3), `7bc6` Krait (e17, 6), `7bd3` Thargon (e28, 5),
`7a50` Viper (e14, `+3a` 1, `+30` 64h). Random position ones: `7be0` junk (index
`((r>>1)^(r>>9))&7` → e1..e8, `7d72`, `7daf`, class 3, `+1d` 1eh, `7e32`), `7c01` trader
(`(r&ff)/43` → e9..e14, class 4; a Viper: word `+3a` = r&1, if 1 word `+30` = legal status),
`7c34` loner (`(r&ff)/37` → e15..e21, class 6, `+30` = r&1f), `7c59` pirate (witchspace: e27;
else `(r&ff)/52` → e22..e26; class 5; `+30` = r&3fh (+20h in anarchies); Thargoid `+1f` =
((r_lo^r_hi)>>3 & 3) + 2), `7ca7` mission ship (CF: type 24, else type 18/19 by rng bit 15;
e23 stats; class 5, `+30` r&7fh, `+2b` 96h, `+2c` 0, `+32` 6, `+31` c8h; no `7e32`), `7ce9`
Thargoid (e27, class 5, `+30` r&7fh, `+2b` 32h, `+2c` 0, `+32` 6, `+1f` 8), `7a12` copy +00..+0f
and jitter x, y, z by `(rng & 7ffh) − 400h` each.

`7d72` position from the current rotation slots 3, 4: `a = (r&7ff)>>1, neg if r&1; b = 10000;
rot(3,a,b); x = a; a = (r2&7ff)>>1 ...; rot(4,a,b); y = a; z = b`. `7daf`: `800c` (face the
player), `+0e` = rng.

## Helpers

`7df2` angles toward (x, y, z): all `sar 2`; `t = atan2(y, z)`; slot 5 = t; rot(5, y, z);
`u = atan2(x, z')`; returns −t, −u (not masked). `800c`: from (−x, −y, −z) → `+0a`, `+0c`.
`7e32` velocity: slots 3 = `+0a`, 4 = `+0c`; `rot(4, 0, speed)` → `+19`; `rot(3, 0, b)` → `+1a`,
`+1b`. `7e58`: 24-bit pos += signed velocity bytes, removed if not fits16. `8034` turn step
(no shortest-way wrap); `8019` steer `+0a`, `+0c` towards target angles. `8059` ship fires
(on scanner, rng_lo < `+30`, not `886d`, not `b126|ae23|b138`; `+30` −= 5 if result ≥ 14h;
within ±200 → `7610`, `7612` = 2, `7681` = `+3c`; ±70 → `7612` = 1 hit). `80cb` make room,
`80fb` player missile, `81e5` launch child (missile, pod, thargon, Krait), `829a` ship fires
missile (kills ≥ 3, hostile, missiles, rng < BX), `82d1` Thargoid launches thargon (rng <
12ch), `8183` particle slot (free, else the last oldest), `81c7` particle update.

## Explosion `7ea8`

`7fe5` convoy count; not in view → just removed. Else removed, sound 13h, `8896` = station;
`+2d` particles: `8183`, copy, `+17` 0, `r = rng`, word `+26` = r, al = (r&1f)−15, ah =
((r>>8)&1f)−15, `+19/+1a` sar 1 then + al/ah (halved again for the mining laser kill), bl =
((r>>3)&1f)−15 likewise for `+1b`, `+1e` = 8 (| 10h if mining and rng < 2000), `+2b` 0, word
`+2c` 0, `+2f` 0, `+33` 7, `+2e` = (rng&f) (+3ch mining) + 14h, flags 17h, `81c7` (×11 for a
station). Canisters: mission ship 1; else c = `+2c`: n = (rng&ff)/(255/(c+1)+1); each: free
slot, copy, `7b92`, `+1e` = 8 | (`+1e`&20h)<<1, r = rng, word `+0a` = r, word `+0c` = swap(r),
`7e32`, `7e58`.
