/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef READLN_H
#define READLN_H

/*
 * Read a line from the given descriptor.  A line is considered as
 * terminated by the ch character.  Different ch terminators may
 * be used in different calls.  The length of the line is returned,
 * INCLUDING the terminator, but no null is appended.  The pointer
 * in *line refers to an internal data structure that will be
 * overwritten on the next call.  Data is not copied like fgets(3)!
 * readln_ready() must be called to initialize the structure.  tmo,
 * if > 0, is the timeout for reading, else no timeout.  readln()
 * will never return less than a complete line.  readln_done() frees
 * up the structure.
 */

struct readln {
   int fd;
   char *buf;
   char bf[104];
   int size;
   int used;
   int eaten;
   int tmo;
};

/* readln.c */

extern int readln_ready (int fd, int tmo, struct readln *rp);
extern void readln_done (struct readln *rp);
extern int readln (register struct readln *rp, char **line, int ch);

#endif
