/* Commander files (NAME.CDR) in a directory, for the core's ep_io. */
#ifndef FILES_H
#define FILES_H

#include "ep_game.h"

/* the directory saves go to and are listed from */
void files_init(const char *dir, const char *data_dir); /* saves, and the game's own files */
extern const ep_io files_io;

#endif
