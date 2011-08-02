/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef ARTFILE_H
#define ARTFILE_H

/*
 * This is the structure of an article file.  The ?len fields refer to
 * the total length of the entity (head or body), including leading
 * \0 and trailing \0. These \0's are just a simple check to see if
 * the entity is valid.

 * When an article is returned, the ?len fields in struct article are
 * decremented by 2, so it refers to the real length.
 */

/*
 * struct info is sometimes used to refer to a mmap()ed file, then
 * VOLATILE really is volatile.  Other times its used to refer to just
 * some memory buffer.  Then VOLATILE is not volatile.
 */

#ifndef VOLATILE
#define VOLATILE volatile
#endif

struct info {
   VOLATILE int hoffset; /* Offset from the START of the FILE */
   VOLATILE int hlen;    /* Total length of the buffer,
                            including leading and trailing '\0' */
   VOLATILE int boffset;
   VOLATILE int blen;
};

/* Each article file begins with this header. */

#define FILE_MAGIC 0xface0

struct file {
   int magic;
   struct info info[ARTSPERFILE];
};

struct fileobj {
   char *path;
   char *map;
   int size;
};

#endif
