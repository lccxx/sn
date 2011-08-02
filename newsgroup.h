/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef NEWSGROUP_H
#define NEWSGROUP_H

/* int functions return -1 on error.  char * functions
   return NULL on error. */

/* Free internal data structures */

extern void ng_fin (void);

/* Initialize internal data structures */

extern int ng_init (void);

/* Get newsgroupname given its ident */

extern char *ng_newsgroup (int ident);

/* Get groups ident from its name */

extern int ng_ident (char *newsgroup);

/* Add a new group, returns its new ident */

extern int ng_addgroup (char *newsgroup);

#endif
