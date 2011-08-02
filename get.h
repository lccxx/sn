/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef GET_H
#define GET_H

#define MAX_CONCURRENCY 8

extern char *optdebug;
extern char *optpipelining;
extern char *optmax;
extern bool optlogpid;
extern bool optnocache;
extern int throttlerate;
extern int concurrency;

#include <sys/types.h>

extern int throttle_setfds (fd_set * rs);
extern void throttle (fd_set * rs);
extern int reap (void);
extern int sow (void);

extern bool sigchld;
extern bool sigusr;

extern void init (void);
extern int add (char *group);
extern int jobs_not_done (void);
extern void quit (void);

#endif
