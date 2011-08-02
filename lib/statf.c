/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#include <sys/param.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "format.h"

static const char ver_ctrl_id[] = "$Id: statf.c 29 2004-04-24 23:02:38Z patrik $";

static char buf[MAXPATHLEN + 1];

int statf (struct stat *stp, char *fmt, ...)
{
   int len;
   va_list ap;

   va_start(ap, fmt);
   len = formatv(buf, sizeof (buf) - 1, fmt, ap);
   va_end(ap);

   if (len >= sizeof (buf))
   {
      errno = ENAMETOOLONG;
      return -1;
   }
   return stat(buf, stp);
}
