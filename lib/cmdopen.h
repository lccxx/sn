/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef CMDOPEN_H
#define CMDOPEN_H

/*
 * Simple wrapper to fork/exec, without any fancy stuff.
 */

/* command must be NULL terminated, like execv().  If read/write
is not NULL, the command is opend for reading/writing and the
descriptor is placed in *read / *write.  Returns the pid */

extern int cmdopen (char **command, int *read, int *write);

/* Use sh -c, otherwise as above */

extern int cmdopensh (char *cmd, int *read, int *write);

/* Returns same value that sh would: <= 255 means the exit code; >
255 means signal number + 255 */

extern int cmdwait (int pid);

#endif
