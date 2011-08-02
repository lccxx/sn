/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Return values:
 * -2 on format error
 * -1 on system error
 * 0 on success
 * 1 if decider short-circuited.
 */

#include <b.h>
#include <readln.h>

static const char ver_ctrl_id[] = "$Id: unfold.c 29 2004-04-24 23:02:38Z patrik $";

static struct b tmp = { 0, };
static char *line;
static int len;
static int bytes_consumed;
static int eof;

/*
 * Not supposed to get EOF here.  I think it is permitted to have an
 * empty body, but without the blank line separator?  I shan't permit
 * it, because there's no way to know if indeed it is a blank
 * article, or an error upstream.
 */

static int getline (struct readln *in)
{
   len = readln(in, &line, '\n');
   if (0 == len)
      return -2;
   if (len < 0)
      return -1;
   bytes_consumed += len;
   if (--len > 0)
      if ('\r' == line[len - 1])
         len--;
   return 0;
}

#define PUTLINE if (putline(tmp.buf, tmp.used)) return -3;

int unfold (struct readln *in, int (*putline) (char *, int))
{
   eof = tmp.used = bytes_consumed = 0;
   switch (getline(in))
   {
      case -2:
         return 0;
      case -1:
         return -1;
   }
   if (-1 == b_appendl(&tmp, line, len))
      return -1;

   for (;;)
   {
      int ret;

      if ((ret = getline(in)))
         return ret;
      if (0 == len)
      {
         if (tmp.used)
            PUTLINE
               break;
      }

      if (' ' == *line || '\t' == *line)
      {
         do
         {
            len--;
            line++;
         }
         while (' ' == *line || '\t' == *line);
         if (-1 == b_appendl(&tmp, " ", 1))
            return -1;
      }
      else
      {
         PUTLINE
            tmp.used = 0;
      }
      if (-1 == b_appendl(&tmp, line, len))
         return -1;
   }
   return bytes_consumed;
}
