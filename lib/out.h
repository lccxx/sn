/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef OUT_H
#define OUT_H

#include <stdarg.h>

/*
 * General functions for output.
 * progname, if set, is prepended to each message written with log()
 * or fail().  All calls end in a write, so no buffer flushing problems.
 * writef() works like fprintf(3), writefv() like vfprintf(3); not
 * all conversion specs are supported.  %m is supported, and is the
 * string obtained from strerror(errno).
 * writes() writes a null terminated string.
 */

extern char *progname;

extern void logv (char *fmt, va_list ap);
extern void log_ (char *fmt, ...);
extern void fail (int ex, char *fmt, ...);
extern int writefv (int fd, char *fmt, va_list ap);
extern int writef (int fd, char *fmt, ...);

#define writes(fd, str) \
   write(fd, str, (sizeof (str) != sizeof (char *)) ? sizeof (str) - 1 : strlen(str))

#endif
