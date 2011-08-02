/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef GROUP_H
#define GROUP_H

#include "config.h"

/* This doesn't do much */

struct group {
   int nr_articles;
   int first;
   int last;
   bool nopost;
};

extern int group_init (void);
extern void group_fin (void);
extern int group_info (char *groupname, struct group *gp);

/* returns the group names available.  All buffers
   returned are malloc()ed. */

extern char **group_all (int *nr);

#endif
