/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#include <stdarg.h>
#include <unistd.h>
#include "format.h"

static const char ver_ctrl_id[] = "$Id: log.c 29 2004-04-24 23:02:38Z patrik $";

char *progname = 0;
static char logbuf[1024];

void logv (char *fmt, va_list ap)
{
   int len = 0;

   if (progname)
   {
      for (; progname[len]; len++)
         logbuf[len] = progname[len];
      logbuf[len++] = ':';
   }
   len += formatv(logbuf + len, sizeof (logbuf) - len - 2, fmt, ap);
   logbuf[len] = '\n';
   write(2, logbuf, len + 1);
}

/* Renamed log -> log_ in order to avoid the annoying warning
   "conflicting types for built-in function `log'" that seems
   to have appeared in gcc 3.3... */

void log_ (char *fmt, ...)
{
   va_list ap;

   va_start(ap, fmt);
   logv(fmt, ap);
   va_end(ap);
}

void fail (int ex, char *fmt, ...)
{
   va_list ap;

   va_start(ap, fmt);
   logv(fmt, ap);
   va_end(ap);
   _exit(ex);
}
