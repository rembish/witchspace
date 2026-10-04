# The core's interface

This page explains how a frontend drives `core/` (plain C, no I/O) and shows what it does.
`src/` is one such frontend: it shows the original's 320 x 200 MCGA screen, plays the PC
speaker and opens a window. A modern renderer can read the same output stream and draw it
its own way.

Hex numbers refer to the original program. A 4-digit hex value such as `4c98` is a code
offset in segment `0000` of the unpacked `ELITE.EXE`; `ds:xxxx` is an address in its data
segment. See `re/NOTES.md` for the details.

## State

Everything lives in one `ep_game` (`core/ep_game.h`). `ep_boot` fills it the way the
original's start-up does; nothing else is global. The frontend sets three things on it:

- `io`: access to commander files (`exists`, `read`, `write`, and `list` of `*.CDR`), and
  `read` of the game's music (`ADBLUE.MID`, at start-up with an AdLib). NULL means no files.
- `wait(g, until, show)`: called wherever the original busy-waits on the timer, that is
  before a frame is shown (`show` = 1) and while a sound plays out. The frontend:
  - shows the output so far;
  - calls `ep_pit_tick` once per tick of the timer (1193182 / `g->pit` Hz: 5555h, about
    55 Hz, unless the AdLib's music has set its own) until `g->clock >= until`;
  - may empty the output (`ep_output_begin`).

  Multi-frame sequences (the launch tunnel, the hyperspace rings) call it once per frame.
- `protection`: 1 asks the copy protection's question (off by default).

Input is the original's hardware, as the frontend has it:

- `ep_key_event(g, byte)`: one byte from the keyboard port (PC set-1 scancodes, 80h set on
  release, E0h prefixes for the extended keys).
- Joystick:
  - `in.joy_present`;
  - `in.joy_x`, `in.joy_y`: counts as the original's timing loop measures them, about 1000
    at the centre;
  - `in.joy_buttons`: port 201h, bits 4 and 5, 0 when pressed.
- Mouse:
  - `in.mouse_present`;
  - `in.mouse_dx`, `in.mouse_dy`: mickeys, added up until the game reads them;
  - `in.mouse_buttons`: bit 0 left, bit 1 right.

## Output

Each call appends to the output, which has four parts:

- `g->render.prim[]`: the primitives, with their text in `g->render.text`;
- `g->circles.span[]`: the circles' spans;
- `g->event[]`: the events.

An event's `at` is the primitive it comes before, so the frontend can handle primitives and
events in one order. `ep_output_begin` empties all four.

Primitives (`core/ep_render.h`) are what the original draws, never pixels:

| kind | points | in |
|------|--------|----|
| `TRI`, `QUAD` | 3 or 4 corners, filled, game colour | the 3D view (304 x 124 at 8, 9) |
| `LINE`, `CLIPPED_LINE` | end point first | the 3D view |
| `PIXEL` | one point | the 3D view |
| `SPANS` | the first span and how many, in `g->circles` (a sun, planet, ring or chart circle) | the 3D view |
| `SPRITE` | the picture (`colour`), x, y | the screen |
| `TEXT` | x, y, colour, shadowed; the bytes: 1 then a colour, 2 then x and y words | the screen |
| `RECT` | x, y, w, h | the screen |
| `BLIP` | the object's type (`colour`), x, the foot's row, the stick's height | the screen |

Colours, glyphs and pictures:

- Game colours go through the video mode's table (`ep_mcga_colour`). Colour 16h flashes
  through 86h + `f.flash`.
- Text colours go through `ds:20e9`, their shadows through `ds:20fe`.
- Glyphs are at `ds:0d40` (`ep_ds_initial`): 8 rows of bits and a width each.
- Pictures are in your own `ELITE.GRF`.

Events (`core/ep_game.h`):

| event | meaning |
|-------|---------|
| `SOUND` | a sound effect: the number given to `4c98` |
| `SURFACE_SOUND` | the surface sound (`4e1a` in the original) |
| `MUSIC` | 2 the title music, 1 stop or sound off, 0 sound on |
| `ICON` | a key bar slot's picture |
| `WAIT` | ticks a sound plays out (the core also calls `wait`) |
| `KEEP` / `PUT_BACK` | the screen under a box, or the top line, kept and put back |
| `FLIP` | a frame is complete |
| `PALETTE` | MCGA's colours from `ds:1144`; `1444` the intro picture's, `1744` the Elite picture's |
| `UNPORTED` | should not happen |

With the PC speaker (`f.sound_device` = 2) the core plays the music and effects itself:
after each tick, `g->speaker` is the PIT divisor and `g->speaker_on` says whether it sounds.

With an AdLib (`f.sound_device` = 1) the core runs the original's music driver: after each
tick, `g->opl[0 .. g->nopl)` are the writes it made to the OPL2 chip (register, value), in
order. The frontend plays them on an emulated chip and sets `g->nopl` to 0. While the music
plays, the timer runs at the song's rate; in flight, the effects' interrupt runs at 555h
(about 874 Hz). Either way the driver keeps the game's clock at about 55 Hz.

## Loops

The original is a set of loops. The core runs one pass of a loop per call and says where to
go next.

| call | result |
|------|--------|
| `ep_boot`, then `ep_title_open` | `EP_WAIT_TIME`: feed `ep_station_key(g, key or ffh)` until `EP_WAIT_NONE` |
| `ep_title_frame` | `EP_CMD_START` (then `ep_start_game`), `SCREEN`, `RESTART` (flight), `PAUSE`, `QUIT`; `f.leave` = 2: quit |
| `ep_station_idle` | `EP_CMD_*`: `SCREEN` (a new screen, or a dialogue when `f.station_step`), `RESTART` (flight), `PAUSE`, `TITLE`, `QUIT` |
| `ep_flight_frame` | `EP_FRAME_NEXT`, `DOCKED` (then `ep_enter_station`, `ep_status_screen`), `SCREEN`, `OVER` (the title), `PAUSED` |
| `ep_pause_idle` | `EP_CMD_RESUME`: `ep_flight_resume` or `ep_station_resume` as `f.resume` says |
| a dialogue | `EP_WAIT_KEY`, `YN`, `LIST`, `TEXT`, `TIME`, `SCAN`: feed `ep_station_key`; at `EP_WAIT_NONE` see `f.leave` (1 title, 2 quit, 3 a commander loaded: the station) |

Keys reach the core through `ep_key_event`. The original's key code then waits in
`g->in.last_key` (ffh: none). A dialogue's key is that code: take it (set `last_key` to ffh)
and pass it to `ep_station_key`. `TEXT`, `TIME`, `LIST` and `SCAN` also want passes with ffh
while no key comes.
