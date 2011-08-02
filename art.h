/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef FILE_H
#define FILE_H

/* These functions return values that may disappear when control
   leaves the calling function, because caching has no garbage
   collection.  So save the results if you want it to be permanent.  */

struct article {
   char *head;
   int hlen;
   char *body;
   int blen;
};

/* Simple check to ensure body of article is sane */

#define art_bodyiscorrupt(body,len) (body[-1] || body[len])

/* How to identify that an article's body has been compressed */

#define COMPRESS_MAGIC "c\03c\03"
#define COMPRESS_MAGIC_LEN (sizeof(COMPRESS_MAGIC)-1)

/* Returns 0 on success and *ap is filled with constant buffers.
   Returns -1 on error.  Fields not found are left empty. */

extern int art_gimme (char *group, int serial, struct article *ap);
extern int art_gimmenoderef (char *group, int serial, struct article *ap);

struct field {
   char *pointer;
   int len;
};

struct xover {
   struct field subject;
   struct field from;
   struct field date;
   struct field messageid;
   struct field references;
   struct field bytes;
   struct field lines;
   #if 0
   struct field xref;
   #endif
};

extern int art_makexover (struct article *ap, struct xover *xp);
extern char *art_findfield (char *head, char *fieldname);
extern void art_filecachestat (int *hit, int *miss);

#endif
