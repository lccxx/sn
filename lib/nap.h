/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef NAP_H
#define NAP_H

/*
 * Like sleep(2), but immune to signals.
 */

extern void nap (int sec, int msec);

#endif
