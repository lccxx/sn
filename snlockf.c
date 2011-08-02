/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * lockf() a file, for shell scripts.
 */

#include <unistd.h>
#include <string.h>
#include <out.h>
#include <format.h>

static const char ver_ctrl_id[] = "$Id: snlockf.c 29 2004-04-24 23:02:38Z patrik $";

int main (int argc, char **argv)
{
   char *p;
   char buffer[40];

   progname = ((p = strrchr(argv[0], '/')) ? p + 1 : argv[0]);
   if (argc != 1)   /* FIXME: Usage string is never printed... */
      fail(1, "Usage: %s (no arguments or options)\n"
           "%s always locks (using lockf()) descriptor 3, which should be open for writing.",
           progname, progname);

   if (lockf(3, F_TLOCK, 0) == -1)
      _exit(2);

   formats(buffer, 40, "%d\n", getpid());   /* Tell the script what our pid is, so it can kill us */
   write(1, buffer, strlen(buffer));
   fsync(1);
   
   sleep(60 * 60);
   _exit(0);
}
