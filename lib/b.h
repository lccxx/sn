/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef B_H
#define B_H

/*
 * Append strings to a dynamically allocated buffer.  Returns -1 on
 * failure.  Calls malloc(3) implicitly.
 */

struct b {
   char *buf;
   int size;
   int used;
};

#define b_append(bp,str) b_appendl(bp,str,strlen(str))
extern int b_appendl (struct b *bp, char *str, int len);

#endif
