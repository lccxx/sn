/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Simple logging function using varargs.
 */

#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "format.h"

static const char ver_ctrl_id[] = "$Id: out.c 29 2004-04-24 23:02:38Z patrik $";

static char outbuf[1024];

int writefv (int fd, char *fmt, va_list ap)
{
   int wrote;
   int used;
   char *p;
   int len;
   char tmp[40];
   int er;

   for (used = wrote = 0; *fmt; fmt++)
      if ('%' == *fmt)
      {
         fmt++;
         if (!*fmt)
            break;
         vachar(*fmt, ap, len);
         for (; len; len--)
         {
            if (used >= sizeof (outbuf))
            {
               er = write(fd, outbuf, used);
               if (-1 == er)
                  return -1;
               wrote += er;
               used = 0;
            }
            outbuf[used++] = *p++;
         }
      }
      else
      {
         if (used >= sizeof (outbuf))
         {
            er = write(fd, outbuf, used);
            if (-1 == er)
               return -1;
            wrote += er;
            used = 0;
         }
         outbuf[used++] = *fmt;
      }
   er = write(fd, outbuf, used);
   if (-1 == er)
      return -1;
   return (wrote + er);
}

int writef (int fd, char *fmt, ...)
{
   va_list ap;
   int er;

   va_start(ap, fmt);
   er = writefv(fd, fmt, ap);
   va_end(ap);
   return er;
}
