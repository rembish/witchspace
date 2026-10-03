/* Elite Plus game state, reconstructed from ELITE.EXE: everything the core keeps between
 * frames. Grows as subsystems are reconstructed; tests/statemap.c lists where each field
 * lives in the original's data segment, so tests can load the original's state and compare. */
#ifndef EP_GAME_H
#define EP_GAME_H

#include "ep_circle.h"
#include "ep_commander.h"
#include "ep_galaxy.h"
#include "ep_objects.h"
#include "ep_render.h"
#include "ep_rng.h"

#include <stdint.h>

/* Things the original does that the core reports instead of doing: sounds, and code paths
 * not reconstructed yet (so tests notice when a state reaches them). */
/* EP_EV_ICON: function-key bar slot (high byte) gets icon sprite (low byte) */
/* EP_EV_WAIT: the original stops for this many timer ticks (a sound playing out) */
/* EP_EV_MUSIC: the music driver switched (arg: ds:45e7, 1 = sound off or the music stops (4d55);
 * 2 = the title music starts (4d21))
 * EP_EV_KEEP / EP_EV_PUT_BACK: the screen under a box (arg 1, 397c at 18,c 112x75) or the top
 * line (arg 2, at 8,0 130x9) is kept, then put back (3981) */
enum {
    EP_EV_SOUND = 1,
    EP_EV_SURFACE_SOUND,
    EP_EV_UNPORTED,
    EP_EV_ICON,
    EP_EV_WAIT,
    EP_EV_MUSIC,
    EP_EV_KEEP,
    EP_EV_PUT_BACK
};

typedef struct {
    uint8_t kind;
    uint16_t arg; /* sound id (4c98), size (4e1a), or the original's address */
} ep_event;

#define EP_MAX_EVENTS 1024

/* Flight variables (data segment addresses) */
typedef struct {
    uint8_t hyperspace;         /* ds:83a4: witchspace (Thargoids left): no sun or planet */
    uint16_t approach;          /* ds:83ae: frames left falling into the sun */
    uint8_t approach_size;      /* ds:83ad: sun size while falling */
    uint8_t sun_size;           /* ds:54c1: apparent size of the sun last frame (temperature) */
    uint8_t altitude;           /* ds:54c3: 2 x (127 - apparent size of the planet), 254 when far */
    uint16_t tribbles_shown;    /* ds:0aa4: Tribble sprites on screen (each frame they may squeak) */
    uint16_t tribble_sprites;   /* ds:0aa6: how many (up to 64) */
    uint8_t tribble[64][8];     /* ds:0aa8: Tribble sprites: x, y, dx words */
    uint16_t tribbles;          /* ds:83b5: Tribbles aboard (the sun's heat kills them) */
    uint16_t message;           /* ds:8058: message shown (data address of the text) */
    uint16_t message_time;      /* ds:805a */
    uint8_t dead;               /* ds:76bd */
    uint8_t no_crash;           /* ds:ae23: launch tunnel frames left (no crashing meanwhile) */
    uint16_t message_shown;     /* ds:8056: last message drawn */
    uint8_t leak_countdown;     /* ds:83a5: frames until a fuel leak starts */
    uint8_t leak;               /* ds:83a6: frames of fuel leak left */
    uint16_t energy;            /* ds:54c8 */
    uint8_t energy_drain;       /* ds:b139 */
    uint8_t laser_temp;         /* ds:54c2 */
    uint8_t laser_hold;         /* ds:b3d3: fire held, waiting for release */
    uint8_t pulse_phase;        /* ds:b125 */
    uint8_t laser_fired;        /* ds:b0e3: type of laser fired this frame */
    uint8_t firing;             /* ds:b0e4 */
    uint8_t warn_time;          /* ds:81f4: frames the current warning still shows */
    uint8_t warn_index;         /* ds:81f5: 0 missile, 1 altitude, 2 temperature, 3 energy */
    uint16_t warn_message;      /* ds:81f2 */
    uint8_t missile_alert;      /* ds:8892 */
    uint16_t speed;             /* ds:af56: 4..48 */
    uint8_t moved;              /* ds:af58: speed or attitude changed: recompute the velocity */
    uint8_t jump_new;           /* ds:ae20: the jump drive was just switched on (sound once) */
    uint8_t autopilot;          /* ds:af14: docking computer flying */
    uint16_t autopilot_in;      /* ds:af15 */
    int8_t roll, pitch;         /* ds:09d1, 09d2: steering, -23..23 */
    int8_t last_x, last_y;      /* ds:09d3, 09d4: arrow keys last frame */
    int8_t accel_x, accel_y;    /* ds:09d5, 09d6: held arrow keys build up */
    uint16_t steer;             /* ds:09d7: the steering word of this frame */
    uint8_t opt_reverse_stop;   /* ds:b134: reversing stops the turn */
    uint8_t opt_self_centre;    /* ds:b135: steering returns to centre */
    uint8_t opt_invert_pitch;   /* ds:b136 */
    uint8_t opt_invert_both;    /* ds:b137 */
    uint16_t pitch_angle[2];    /* ds:af4c, af4e: angles found while pitching */
    int16_t velocity[3];        /* ds:af50, af52, af54 */
    uint8_t jump_speed;         /* ds:b0dd: speed x 32 */
    uint8_t mission;            /* ds:83a0 */
    uint8_t mission_state;      /* ds:83a2 */
    uint8_t station_angry;      /* ds:83aa */
    uint8_t station_hit;        /* ds:83ab */
    uint8_t mining;             /* ds:ae22: mining laser on an asteroid */
    uint8_t target_note;        /* ds:54ca */
    uint16_t target_slot;       /* ds:b0e1: data address of a slot the mission cares about */
    uint8_t beam_flip;          /* ds:54b9 */
    uint8_t beam_pair;          /* ds:54ba */
    uint8_t beam_colour;        /* ds:54bb */
    uint8_t safe_zone;          /* ds:7680: bit 0, near the station */
    uint8_t ap_flag;            /* ds:b0e0 */
    char bounty_text[16];       /* ds:805c: "BOUNTY: ... Cr", shown as a message */
    uint8_t docked;             /* ds:7613: docking succeeded */
    uint8_t under_fire;         /* ds:7612: an enemy laser hit us this frame */
    uint16_t attacker;          /* ds:7610: data address of its slot */
    uint8_t hit_from_behind;    /* ds:7681: bit 7, the aft shield takes it */
    uint8_t fore_shield;        /* ds:54c4 */
    uint8_t dust[210];          /* ds:5314: 30 particles of 7 bytes (see ep_dust.h) */
    uint8_t dust_old[210];      /* ds:53e6: their shadow */
    uint8_t dust_shift;         /* ds:54b8 */
    uint8_t status;             /* ds:54cb: condition 0 red, 1 green, 2 yellow, 3 (between) */
    uint8_t ecm_shown;          /* ds:54c0: the ECM was just used (icon) */
    uint8_t class_count[9];     /* ds:8730: objects but debris, then per AI class 0..7 */
    uint8_t lock_text[0x28];    /* ds:8081: "<type> (<role>)" after "Missile locked onto " */
    uint8_t sound_device;       /* ds:4801: 2 picks other sound numbers */
    uint8_t hyper_countdown;    /* ds:ae60: hyperspace countdown, seconds (0 = none) */
    uint8_t hyper_tick;         /* ds:ae61: frames to the next second */
    uint8_t missile_block;      /* ds:b1f8: 1 = missiles cannot be fired */
    uint8_t flash;              /* ds:1b3e: flashing colour step 0..5 (3921) */
    uint16_t circle_mask;       /* ds:108f: circle jitter mask, 0 outside a planet draw */
    uint8_t rings[30];          /* ds:85dc: hyperspace rings: delay, radius, colour */
    uint8_t galactic_jump;      /* ds:ae24: 1 = the jump under way is galactic */
    uint8_t force_misjump;      /* ds:8610: 1 = the next jump lands in witchspace */
    uint8_t hyper_target[0x19]; /* ds:8611: the target system's record */
    uint8_t jump_fuel;          /* ds:82d6: fuel the jump costs */
    uint8_t galaxy_digit;       /* ds:829b: '1' + galaxy, in the galactic jump message */
    uint8_t other_screen;       /* ds:8711: a screen other than the space view is up (1, 2) */
    uint8_t screen;             /* ds:02f9: 0 flight, 1/2 docked screens, 3/4 Esc menu, 5 */
    uint8_t screen_shown;       /* ds:02fa: the screen the key bar is set up for */
    uint8_t bar_colour;         /* ds:02fe */
    uint8_t bar_redraw;         /* ds:0300: the bar was set up anew this frame */
    uint8_t bar_active[12];     /* ds:0301: command id of each function key, as drawn */
    uint8_t bar_wanted[12];     /* ds:030d: what the screen wants there */
    uint8_t space_pressed;      /* ds:0319 */
    uint8_t screen_flag;        /* ds:031d */
    uint8_t screen_bits;        /* ds:031e */
    uint8_t hyper_text_time;    /* ds:ae25: frames the countdown message stays */
    uint8_t hyper_digits[2];    /* ds:ae5d: the countdown as text */
    uint8_t escape_countdown;   /* ds:b3d5: frames to the escape capsule launch */
    uint8_t escape_digit;       /* ds:b0c1: its count as shown */
    uint8_t autopilot_step;     /* ds:af17 */
    int16_t death_vel[3];       /* ds:76b7: the way the ship was flying when it blew up, x 40 */
    uint16_t ap_roll;           /* ds:af59: the roll the docking computer turns to */
    uint8_t ap_passes;          /* ds:af5b: lining-up passes done (two each) */
    uint8_t sound_off;          /* ds:45e7 */
    uint8_t launching;          /* ds:ae21: 1 from the launch on, 0 at the station */
    uint8_t fuel_text[0x1e];    /* ds:8e14: "Fuel: x.y Light Years" with its digits (8e2d) */
    uint8_t tribble_text[0x0f]; /* ds:8de6: the Tribble count and its plural */
    uint8_t screen_redraw;      /* ds:88e0: a command asks the screen to be drawn again */
    uint8_t reward_digit;       /* ds:9972: the thousands of mission 1's reward, in its text */
    uint8_t station_step;       /* where a screen waits for a key (the original's place in the code) */
    uint8_t station_rating;     /* the promotion shown: Elite gets the ending picture */
    uint8_t menu[0x11];         /* ds:0980: the list on screen: rows, cursor, texts, x, y, width, colours */
    uint8_t last_cmd_key;       /* ds:03f1: the last key the commands saw (the list reads arrows there) */
    uint16_t prices[34];        /* ds:8d0a: buying and selling price of each commodity */
    uint8_t rows[0x3f0];        /* ds:a410: the texts of the list's rows */
    uint8_t list_row;           /* ds:ad2b: the row picked (ff none) */
    uint8_t list_busy;          /* ds:ad2d: a command acted on the list */
    uint16_t note_ticks;        /* ds:45e4: timer ticks a note (an error) stays up */
    uint8_t paused;             /* ds:45e6: the timer stands still */
    uint8_t idle;               /* which screen's idle loop runs (EP_IDLE_*), and where in it */
    uint8_t list_keep;          /* ds:ad2c: the row to come back to when the list is rebuilt */
    uint8_t laser_kind;         /* ds:92f9: the laser type of the row (8df7) */
    uint8_t list_count;         /* ds:acb0: rows of the equipment list */
    uint8_t chart[0x407];       /* ds:5604: the short-range chart's symbols and labels */
    uint8_t chart_kind;         /* ds:6404: 0 short-range, else galactic */
    uint8_t chart_digit;        /* ds:55e6: the galaxy's number in its chart's title */
    uint8_t dist_shown[0x12];   /* ds:5550: the distance as the chart shows it ("xx.x") */
    uint8_t data_text[0x300];   /* ds:8900: the Data screen's texts, with its numbers written in */
    uint8_t description[0x100]; /* ds:5a3e: the system's description, words cut at the spaces */
    uint8_t species_icon;       /* ds:ae1b */
    uint8_t desc_save[8];       /* ds:5a2b */
    uint8_t desc_caps;          /* ds:5a34 */
    uint8_t entry[0x2b];        /* ds:09a2: a text being typed: max, count, text, x, y, end, colours, blink */
    uint16_t find_text[2];      /* ds:5567: the typed name and where it ends */
    uint8_t sound_mode;         /* ds:45ea */
    uint8_t pause_screen;       /* the screen the pause menu came from (on the original's stack) */
    uint8_t resume;             /* what the pause interrupted (EP_RESUME_*) */
    uint8_t
        leave; /* 1 back to the title (abandon), 2 to DOS (exit), 3 a commander loaded: the station (9e80, a012) */
    uint8_t bar_quiet;          /* ds:b3d4: the bar's marks are not drawn */
    uint8_t menu_kept[0x11];    /* ds:0991: the list as it was while files are listed */
    uint8_t files[0x16d];       /* ds:0088: the commander files' names */
    uint8_t file_count;         /* ds:01f5 */
    uint8_t file_top;           /* ds:01f6: the first one shown */
    uint8_t title_ship;         /* ds:b1bb: the type on the title */
    uint16_t title_hold;        /* ds:b25f: frames at the closest point */
    uint16_t title_list;        /* ds:b261: where in the title list (ds:b263, ffh at the end) */
    uint8_t title_step;         /* the title coming up: 1 the intro picture, 2 the credits */
    uint32_t intro_until;       /* 3ae5: the intro picture's time (in registers) */
    uint8_t protection_failed;  /* the copy protection was asked and answered wrong (off: never) */
    uint8_t prot_page[2];       /* ds:0a83: the protection's page (a number, then two digits) */
    uint8_t prot_paragraph;     /* ds:0a91 */
    uint8_t prot_line;          /* ds:0a99 */
    uint8_t prot_word;          /* ds:0aa1 */
    uint16_t prot_hash;         /* ds:09d9: the word's, 9 bits */
    uint8_t station_ecm;        /* ds:8891: the station's ECM runs this many frames (0 = watching) */
    uint8_t reg_dl;             /* DL as the last routine left it: some AI handlers read it stale */
    uint8_t ai_hold;            /* ds:b138: ships may not fire this frame */
    uint8_t danger_gov;         /* ds:76b6: the government for spawning (0 in witchspace) */
    uint16_t spawn_gov8;        /* ds:8897: government x 8 */
    uint16_t spawn_row;         /* ds:888f: danger government x 4 */
    uint16_t convoy_leader;     /* ds:8893: data address of its slot */
    uint8_t exploding_station;  /* ds:8896 */
    uint8_t convoy_left;        /* ds:83a9 */
    uint8_t convoy_leader_dead; /* ds:83a7 */
    uint8_t convoy_countdown;   /* ds:83b3 */
    uint8_t siege;              /* ds:83b1 */
    uint8_t mission_system;     /* ds:83a3 */
    uint8_t mission5_phase;     /* ds:83b0 */
    uint8_t mission5_count;     /* ds:839e */
    uint8_t mission5_flag;      /* ds:839f */
    uint8_t aft_shield;         /* ds:54c5 */
    uint8_t scoop_lock;         /* ds:b126: frames left of the death sequence (nothing works) */
    uint8_t video;              /* ds:10bc: 0 EGA, 1 VGA, 2 MCGA */
} ep_flight;

/* Input as the keyboard handler keeps it (ds:020d: per scancode 0 down, 80h up) and the
 * flight key bindings (ds:b251.., pointers into that table in the original; scancodes here) */
typedef struct {
    uint8_t key[128];                                    /* ds:020d */
    uint8_t faster, slower, up, down, left, right, fire; /* ds:b251 .. b25d */
    uint8_t last_key;                                    /* ds:0d2f: key code of the last press, ff none */
    uint8_t control;                                     /* ds:8f2c: 0 keyboard, 1 joystick, 2 mouse */
} ep_input;

/* Commander files, the frontend's (DOS 8.3 names in capitals: "JAMESON.CDR") */
typedef struct {
    void *ctx;
    int (*exists)(void *ctx, const char *name); /* 1 if it opens for reading (3d00), not merely is there */
    int (*read)(void *ctx, const char *name, uint8_t *data, int max);        /* bytes read, -1 none */
    int (*write)(void *ctx, const char *name, const uint8_t *data, int len); /* bytes written, -1 */
    int (*list)(void *ctx, char names[][13], int max);                       /* the *.CDR there */
} ep_io;

typedef struct {
    const ep_io *io;         /* NULL: no files */
    uint8_t protection;      /* 1: the copy protection is asked (ep_boot.h); off by default */
    ep_commander cmdr;       /* ds:82db */
    ep_commander cmdr_saved; /* ds:83be: a second copy (the commander as last saved or docked) */
    ep_space space;          /* ds:76de objects, 76b5 count, 76be rotation slots, 76d8 angles ... */
    ep_rng rng;              /* ds:0205 */
    ep_seed seed;            /* ds:5503: the current system's seed (galaxy generator) */
    uint16_t market_rng[3];  /* ds:92e0: the quantities' generator */
    uint8_t dist_text[5];    /* ds:5562: the selected system's distance as digits */
    uint32_t clock;          /* ds:45e0: timer ticks */
    uint32_t flip;           /* ds:267c: tick count at the last frame flip */
    ep_render render;        /* its vertex buffer (ds:28e6) carries over between ships */
    ep_flight f;
    ep_input in;
    /* tests: DL at the AI from the original (the core only approximates it) */
    uint8_t test_dl_force, test_dl;
    /* output of the last update */
    ep_circle_buf circles; /* planet and sun spans */
    ep_event event[EP_MAX_EVENTS];
    int nevents;
} ep_game;

void ep_event_add(ep_game *g, uint8_t kind, uint16_t arg);

#endif
