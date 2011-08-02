/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>

static const char ver_ctrl_id[] = "$Id: nap.c 29 2004-04-24 23:02:38Z patrik $";

void nap (int sec, int msec)
{
   struct timeval tv; /* Counting on Linux to update tv */

   tv.tv_sec = sec;
   tv.tv_usec = msec * 1000;
   while (-1 == select(0, 0, 0, 0, &tv) && EINTR == errno)
      ;
}
