/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include "defs.h"
#include "format.h"

static const char ver_ctrl_id[] = "$Id: format.c 36 2004-04-27 16:52:02Z patrik $";

static char *ichars = "0123456789abcdefghijklmnopqrstuvwxyz";

char *istr (int i, int base, char *tmp)
{
   int n;
   bool negative = FALSE;

   tmp[n = 39] = '\0';
   if (i < 0)
   {
      negative = TRUE;
      i = 0 - i;
   }
   do
      tmp[--n] = ichars[i % base];
   while (i /= base);
   if (negative)
      tmp[--n] = '-';
   return (tmp + n);
}

char *uistr (unsigned int u, int base, char *tmp)
{
   int n;

   tmp[n = 39] = '\0';
   do
      tmp[--n] = ichars[u % base];
   while (u /= base);
   return (tmp + n);
}

int formatv (char *buf, int size, char *fmt, va_list ap)
{
   char *p;
   int len;
   char tmp[40];
   char *lim;
   char *start;

   lim = buf + size;
   start = buf;

   for (; *fmt; fmt++)
      if ('%' == *fmt)
      {
         fmt++;
         vachar(*fmt, ap, len);
         for (; len; len--)
         {
            *buf++ = *p++;
            if (buf >= lim)
               goto done;
         }
      }
      else
      {
         *buf++ = *fmt;
         if (buf >= lim)
            break;
      }
done:
   *buf = '\0';
   return (buf - start);
}

int formats (char *buf, int size, char *fmt, ...)
{
   va_list ap;

   va_start(ap, fmt);
   return formatv(buf, size, fmt, ap);
   va_end(ap);
}
