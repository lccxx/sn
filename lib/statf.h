/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef STATF_H
#define STATF_H

struct stat;

extern int statf (struct stat *stp, char *fmt, ...);

#endif
