/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef TIMES_H
#define TIMES_H

#include <time.h>

struct times {
   int serial;
   time_t stored;
};

extern int times_init (void);
extern void times_fin (void);

/* Return the earliest article serial number after given date */

extern int times_since (char *group, time_t earliest);
extern int times_append (char *group, int serial);
extern int times_expire (char *group, int until);

#ifdef TIMES_DEBUG
extern int times_show (char *group);
#endif /* TIMES_DEBUG */

#endif
