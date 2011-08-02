/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#include <stdarg.h>
#include <unistd.h>
#include <readln.h>
#include <format.h>
#include <out.h>
#include <tokensep.h>
#include "config.h"

extern int debug;
static const char ver_ctrl_id[] = "$Id: args.c 29 2004-04-24 23:02:38Z patrik $";
char *args[20];
char args_outbuf[1024];
char args_inbuf[512];

int args_write (int fd, char *fmt, ...)
{
   va_list ap;
   int len;

   va_start(ap, fmt);
   len = formatv(args_outbuf, sizeof (args_outbuf) - 2, fmt, ap);
   va_end(ap);

   if (-1 == write(fd, args_outbuf, len))
   {
      *args_outbuf = '\0';
      return -1;
   }
   args_outbuf[len - 2] = '\0';
   if (debug >= 2)
      LOG("-> %s", args_outbuf);
   return len;
}

int args_read (struct readln *rp)
{
   char *p;
   int len;
   int i;

   len = readln(rp, &p, '\n');
   if (len <= 0)
   {
      *args_inbuf = '\0';
      return -1;
   }

   for (len--; len > 0; len--)
   {
      switch (p[len])
      {
         case '\n':
         case '\r':
            continue;
      }
      break;
   }
   p[len + 1] = '\0';

   for (i = 0; i < sizeof (args_inbuf) - 1 && i <= len; i++)
      args_inbuf[i] = p[i];
   args_inbuf[i] = '\0';
   if (debug >= 2)
      LOG("<- %s", args_inbuf);
   if (len >= 512)
      return 0;

   for (i = 0; i < 20; i++)
      if (!(args[i] = tokensep(&p, " \t\r\n")))
         break;
   return i;
}

void args_report (char *tag)
{
   LOG("%sSent \"%s\" got \"%s\"", tag ? tag : "", args_outbuf, args_inbuf);
}

void args_flushtodot (struct readln *rp)
{
   int tmo;

   tmo = rp->tmo;
   rp->tmo = 20;
   while (1)
   {
      int len;
      char *p;

      len = readln(rp, &p, '\n');
      if (len <= 0)
         break;
      if (len > 1 && '.' == *p)
      {
         if ('\r' == p[len - 2])
            p[len - 2] = '\0';
         else
            p[len - 1] = '\0';
         if (!p[1])
            break;
      }
   }
   rp->tmo = tmo;
}
