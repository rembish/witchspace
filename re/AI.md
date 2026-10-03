# AI class handlers of `77e0` (analysis notes)

Static read of the handlers `83f4..873b`, ported as `core/ep_ships.c` and confirmed by the
`ai` subsystem difftest (two corrections from the test are marked below). Only the station's
paths leave a DL that the next handler can read stale (`c2`, or a launch's velocity); every
other handler ends with `7e58`. `o[+xx]` is a byte of the object unless "word". `r()` is one flight-generator step
(`4f20`, `ep_flight_random`). Compares are unsigned unless signed is said. None of the
handlers steps the main generator `ds:0205`, and none calls a sound or drawing routine
directly (only via `7ea8` explosion → sound 13h, `67ab` damage → `6cfa` crash, `ad4f` rewards).

## Shared helpers

- `6b4b`/`6b54` in_box(d): ax, bx, cx = low words `o+04/06/08` (`6b4b`); CF iff |ax| < d and
  |bx| < d and |cx| < d (unsigned; |8000h| = 8000h never inside; short-circuits). The 24-bit
  high bytes +01..+03 are ignored.
- `7dde`: (x, y, z) low words (away from the player); `7de8`: their negations (towards).
- `8019` steer(A, B): `8034(t, cur)`: d = (t & 7ff) − (cur & 7ff) (signed, no wrap); step = d
  if |d| < turn (`o+1d`) else ±turn; word `o+0a` += step(A), word `o+0c` += step(B) (not
  masked). Returns ax = |dA|, bx = |dB|, dx = B.
- `8059` fire laser (right after `8019`):
  ```
  if (!(o[+1e]&2)) return;            // not on scanner: no rng
  r = r(); if ((u8)r >= o[+30]) return;
  if (886d()) return;                 // safe zone
  if (b126|ae23|b138) return;
  al = o[+30] - 5 (8-bit); if (al >= 0x14) o[+30] = al;   // +30 in 1..4 wraps and is stored
  if (!(|dB| < 200 && |dA| < 200)) return;
  7610 = o; 7612 = 2; 7681 = o[+3c] (stale if not in range);
  if (o[+3c] < 70 && |dB| < 70 && |dA| < 70) 7612 = 1;   // ax = +3c from the mov al above
  ```
- `886d` safe-zone block (CF = blocked): 83aa == 1 → no; police (`827e`) → no; else bit 0
  of 7680.
- `829a(p)` missile: needs byte 836c ≥ 3 (low byte of kills), `o+1e` bit 0, not `886d`,
  `o+32` ≠ 0, b126|ae23|b138 == 0; then r() < p (16-bit) → `81e5(14h)`, on CF `o+32--`.
- `82d1` thargon: type 22 and `o+1f` ≠ 0: r() < 12ch → `81e5(7)`, on CF `o+1f--`.
- `81e5(dl)` launch a child: `80ae` free slot (none: CF 0), `81b7` copy 64 bytes parent →
  child, then 14h missile `7b85` (e0, class 2), `7e58`×3 with the copied +19..+1b, word +29 = 0;
  15h pod `7bac`, `7e1f` (3 rng: +0a, +0c, +0e), `7e32`, `7e58`×3; 7 thargon `7bd3` (e28,
  class 5), `7e32`, `7e58`×2, word +3a = parent; 5 Krait `7bc6` (e17, class 6), `7e32`,
  `7e58`×2. CF 1; other dl: CF 0. Children keep what `7d14` does not reset (+35..+3a, +29,
  +26..+28).
- Type tests: `825e` ∈ {12, 6, 5, 11}; `8273` 28 (Viper); `827e` police = 28 and word +3a == 1;
  `8288` 22; `8291` 7; `43b2` station (type 0 or 1).
- `8314(t, d)`: deltas (t.x sar 2) − (o.x sar 2) on low words (16-bit wrap), box d shr 2.
- `7e86` ECM sweep: slots 0..7fde−1 active and type 20 → clear bit 0. Also the player's ECM.
- `6d83` rotation leaves dx = R(2b·sin), the last rounded product. Only stale slot read:
  `7d72` (trader launch from the station) uses rotation slots 3/4 as last left.

## Stale DL

Class 4 state ≥ 4, class 5 state 3, class 6 state 3 use `mov dh,[di+1c]; call 6b4b`: d =
(`o+1c` << 8) | DL, DL left from the previous handler. DL at handler exit:

| Handler | DL at exit |
|---|---|
| Classes 3–6 | `7e58` last (`cbw; cwd`): ff if (s8)`o+1b` < 0 after the move, else 0 |
| Class 7 | as above if alive; unchanged if it just died |
| Class 0 | unchanged |
| Class 1 reaching 840e | c2 (dx = 01c2) |
| Class 1 launch | low byte of R(2·B·sin(slot 3)) from the last `7e32` |
| Class 1 early exits | unchanged |
| Class 2 steering | sign(`o+1b`) |
| Class 2 hit | dx = 00c8 (837f), then `7ea8`: not in view unchanged, in view the sign of the last particle's/canister's +1b |

## Class 0 `83f4`: `ret` (sun, planet, Cobra hulk)

## Class 1 station `83f5`
```
word o[+0e] += 10;
if (76b6 >= 1 && (o[+1e]&1) && fits16(o) /*4217*/) {
  dx = 0x1c2;
  if (!in_box(o, 450) && 836b >= 10) {
    ok = (836b >= 40) ? (r() < 0x7d0) : (r() < 0x5a);
    if (ok && (s = free_ship_slot())) {                   // 80ae, slots 3..7fde-1
      copy64(s, o);
      r2 = r();
      if (r2 >= 10000)      spawn_viper(s);   // 7a50: e14, class 4, word +3a=1, +30=64h
      else if (r2 >= 5000)  spawn_shuttle(s); // 7bb9: e7, class 3
      else                  spawn_trader(s);  // 7c01
      s 24-bit z += 0xf0; word s[+0c] += 0x400; word s[+0e] = -s[+0e];
      velocity(s);           // 7e32
    }
  }
}
if (8891 == 0) {                                          // 8471 missile watch
  for each active type-20 m in 0..7fde-1 with word m[+29] = t != 0:
    if (t == o) { add = 40; goto hit; }
    if (police(t) && (7680 & 1)) { add = 10; goto hit; }
  return;
hit: 836b = min(836b + add, 0xff);
  if (b139 == 1) return;
  8891 = 20;
}
if (83aa == 1) { 8891 = 0; return; }
54c0 = 1; ecm_sweep(); 8891--;
```
`7c01` in a station launch: r → e9..e14 = (r & ff)/43, `7d72` (2 rng, stale slots 3/4), `7daf`
(`800c` + 1 rng → +0e), class 4, `7e32`; type 28: r → word +3a = r & 1, if 1 word +30 =
legal. RNG order: gate, r2, trader draws. 8891 is set by fleeing traders (14h) and Thargoids
(1eh), consumed only here.

## Class 2 missile `8352`
```
8892 = 0; word o[+0e] += 0x28; t = word o[+29];
if (t) { if (!(t[0]&1)) { explode(o); return; } d = t.pos_lo - o.pos_lo (16-bit) }
else   { d = -o.pos_lo; 8892 = 1; }
if (!(|d| < 200 per axis)) { aim(d); steer; velocity; move; return; }
explode(o);
if (!t) { damage(800); return; }
if (station(t)) {
  if (83aa == 1) { t[+2b] -= 10; if (borrow) { explode(t); 83ab = 1; 83aa = 0; } }
  else 836b = min(836b + 5, 0xff);
  return; }
rewards(t); /*ad4f*/ if (!(t[+1e] & 4)) explode(t);
```

## Class 3 junk `84e1`
`move` first; then rocks: odd types (5, 11) word +0a += 37h, +0e += ffdfh; 12, 6 the other way.

## Class 4 trader / police `84fc`
```
if (rock type) { word +0e += 20;
  if ((o[+1e]&1) && !(o[+1e]&0x10)) if (launch_child(o, 5)) o[+1e] |= 0x10;
  move; return; }
switch (o[+17]) {
case 0: o[+17] = 1; move; return;
case 1: if (police(o) && 836b >= 5) o[+17] = 2;
        else if (o[+1e]&1) o[+17] = (r() < 0x53fc) ? 3 : 2;
        move; return;
case 2: if (!police(o) && o[+2b] < 8) { o[+17] = 3; move; return; }
        if (in_box(o, 800)) { o[+17] = 4; move; return; }
        aim(-pos); steer; fire_laser;
        if (police(o) && 836b) launch_missile(836b < 10 ? 100 : 3000);
        velocity; move; return;
case 3: if (o[+2b] < 3) launch_missile(1000);
        weave(o); aim(-pos); A += 0x400 + word o[+36]; B += word o[+38];
        steer; velocity; if (r() < 0x32) 8891 = 20; move; return;
default: if (!in_box(o, (o[+1c]<<8)|DL)) { o[+17] = 2; move; return; }
        if (o[+2b] < 5 && !police(o)) { o[+17] = 3; move; return; }
        aim(+pos); steer; velocity; move; return;
}
```
weave (`85bf`, `87f3`): if +35 == 0: r → w = 100h | ror8(r & ff, 1), negated if r & 1 → word
+36; again → word +38; +35 = 10. Then if --+35 == 0: +35 = 10, negate +36 and +38.

## Class 5 hostile `8645`
```
if (type == 22) { if (r() < 100) 8891 = 30; word +0e += 20; }
else if (type == 7) word +0e += 20;
switch (o[+17]) {
case 0: o[+17] = 1; move; return;
case 1: if (!((o[+1e]&1) && o[+30] < 10)) { o[+16] = ((r() >> 8) & 3) + 2; o[+17] = 2; }
        move; return;
case 2: if (in_box(o, 1000)) { o[+17] = 3; move; return; }
        aim(-pos); steer; fire_laser; launch_missile(1000); launch_thargon; velocity; move;
        word +0e += 15; return;
case 3: if (in_box(o, (o[+1c]<<8)|DL)) { aim(+pos); steer; velocity; launch_thargon; move; return; }
        if (--o[+16] == 0) goto give_up;
        if (type == 7 && (p = word o[+3a])) if (type(p) != 22 || !(p[+1e] & 2)) goto give_up;
        o[+17] = 2; move; return;
give_up: o[+30] = 9; o[+1e] &= ~1; o[+17] = (type == 7) ? 10 : 1; move; return;
default: if (o[+18] >= 10) { o[+18] -= 3; velocity; } move; return;
}
```

## Class 6 loner / bounty hunter `873b`
```
switch (o[+17]) {
case 0: if (!(o[+1e]&2)) { aim(-pos); steer; velocity; move; return; }
        if (o[+1e]&1) { o[+17] = 3; move; return; }
        n = 82ef();   // slots 0..7fde-1 with +33 == 6, +1e&2, != o; no active check; BP = last
        if (n >= 2) { if (836b >= 40 || r() < 0x32) o[+17] = 1; }
        else if (n == 1) { word o[+29] = BP; o[+17] = 2; }
        move; return;
case 1: if (in_box(o, 1000)) { o[+17] = 3; move; return; }
        aim(-pos); steer; fire_laser; launch_missile(1500); velocity; word +0e += 20; move; return;
case 2: t = word o[+29]; if (8314(t, 2000)) { o[+17] = 4; move; return; }
        aim(deltas); steer; velocity; move; return;
case 3: if (!in_box(o, (o[+1c]<<8)|DL)) { o[+17] = 0; move; return; }
        weave(o); aim(-pos); A += 0x400 + word +36; B += word +38; steer;
        launch_missile(1500); velocity; move; return;
default: if (in_box(o, 5000)) { o[+17] = 0; move; return; }   // (jae 8855: attack from outside)
        aim(-pos); steer; fire_laser; launch_missile(2500); velocity; move; return;
}
```

## Not reached from the AI
- `6ab2` energy bomb (a4d6): in the safe zone legal += 40 (clamped); every active on-scanner
  slot 3..7fde−1: +2c = 0, `7ea8`.
- `6aeb` launch (from a170): ae60 = 0, ae23 = 64h, free slot (`80cb` if none) zeroed (`6bbe`),
  Cobra III hulk (`7b9f`); 76d8 += 400h, b0de = 400h, b0e0 = 1; af56 = 20, af58 = 1, 835d = 0;
  `a768`, `a7b1`×12; cargo held = 0, 839c = 0, legal = 0.
