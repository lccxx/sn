/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef ARGS_H
#define ARGS_H

#include "lib/readln.h"

extern char *args[20];
extern char args_outbuf[1024];
extern char args_inbuf[1024];
extern int args_write (int fd, char *fmt, ...);
extern int args_read (struct readln *rp);
extern void args_report (char *tag);
extern void args_flushtodot (struct readln *rp);

#endif
