/* Commander files (NAME.CDR) in a directory, for the core's ep_io; part of the witchspace
 * frontend. The core also reads the music (*.MID) through it, from the game's own folder. */
#ifndef FILES_H
#define FILES_H

#include "ep_game.h"

/* dir: where saves go to and are listed from; data_dir: the game's own files (the music) */
void files_init(const char *dir, const char *data_dir);
/* the ep_io for g->io (its ctx unused) */
extern const ep_io files_io;

/* a whole file, malloc'd (NULL if it cannot be read) */
uint8_t *files_slurp(const char *path, size_t *len);

/* dir/name, matching the name in any case (copies of the game come as ELITE.EXE or
 * elite.exe): 1 with the path in out, 0 when there is none */
int files_find(const char *dir, const char *name, char *out, size_t n);

#endif
