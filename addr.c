/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * The functions here are based (mainly) on RFC 822 / 2822
 */

#include <stdlib.h>
#include "config.h"
#include "addr.h"

static const char ver_ctrl_id[] = "$Id: addr.c 43 2004-04-28 23:16:48Z patrik $";

/*
int addr_addrspec (char *buf)
{
   char *p;
   int len;
   
   p = buf;
   len = addr_localpart(p);
   if (!len)
      return 0;
   p += len;
   if ('@' != *p)
      return 0;
   p++;
   len = addr_domain(p);
   if (!len)
      return 0;
   p += len;
   return (p - buf);
}
*/

int addr_idleft (char *buf);
int addr_idright (char *buf);

/*
 * Checks for a valid Message-ID
 */

int addr_msgid (char *buf)
{
   char *p;
   int len;
   
   p = buf;
   if (*p != '<')
      return 0;
   p++;
   len = addr_idleft(p);
   if (!len)
      return 0;
   p += len;
   if (*p != '@')
      return 0;
   p++;
   len = addr_idright(p);
   if (!len)
      return 0;
   p += len;
   if (*p != '>')
      return 0;
   p++;
   return (p - buf);
}

#define C(x) case x:

int addr_domain (char *buf)
{
   char *p;
   bool lb;
   
   p = buf;
   if ((lb = ('[' == *p)))
      p++;
   
   for (; ; p++)
      switch (*p)
      {
         C('-')
            if (lb) 
               return 0; /* else fall through */
         C('.')
            if (p == buf || p[-1] == '.' || !p[1])
                return 0;
            else
                continue; /* Bugfix: no fall through */
         C('a') C('b') C('c') C('d') C('e') C('f') C('g') C('h') C('i')
         C('j') C('k') C('l') C('m') C('n') C('o') C('p') C('q') C('r')
         C('s') C('t') C('u') C('v') C('w') C('x') C('y') C('z')
         C('A') C('B') C('C') C('D') C('E') C('F') C('G') C('H') C('I')
         C('J') C('K') C('L') C('M') C('N') C('O') C('P') C('Q') C('R')
         C('S') C('T') C('U') C('V') C('W') C('X') C('Y') C('Z')
         C('_') /* permit underscore.. why not */
            if (lb)
               return 0; /* else fall through */
         C('0') C('1') C('2') C('3') C('4') C('5') C('6') C('7') C('8') C('9')
            continue;
         C(']')
            if (lb)
               p++; /* fall through */
            else
               return 0;
         default:
            if (buf != p)
               return (p - buf);
            else
               return 0;
      }
}

int addr_localpart (char *buf)
{
   char *p;
   bool dq, bs;

   p = buf;
   dq = bs = FALSE;

   for (p = buf; *p; p++)
      if (!bs)
         switch (*p)
         {
            case '"':
               dq = (!dq);
               break;
            case '\\':
               bs = TRUE;
               break;
            case '@':
               if (dq)
                  return 0;
               return (p - buf);
            case '(': case ')': case '<': case '>':
            case ',': case ';': case ':': case '[': case ']':
               if (!dq)
                  return 0;
               break;
            default:
               if (!dq && *p <= ' ')
                  return 0;
         }
      else
         bs = FALSE;
   /* end of string */
   return 0;
}

/*
 * Like strchr() but ignores comments and quoted strings
 */

char *addr_qstrchr (char *str, int find)
{
   bool bs, dq, cm;

   for (bs = dq = cm = FALSE; *str; str++)
      if (!bs)
         if (!cm)
            switch (*str)
            {
               case '"':
                  dq = (!dq);
                  break;
               case '\\':
                  bs = TRUE;
                  break;
               case '(':
                  if (!dq)
                     cm = TRUE;
                  break;
               default:
                  if (dq || cm || (char) find != *str)
                     break;
                  return str;
            }
         else
         {
            if (!dq && ')' == *str)
               cm = FALSE;
         }
      else
         bs = FALSE;
   return NULL;
}

int addr_unescape (char *from, char *to, int len)
{
   char *dst;
   bool bs, dq;

   bs = dq = FALSE;
   dst = to;
   for (; len > 0; from++, len--)
   {
      if (!bs)
         switch (*from)
         {
            case '"':
               dq = (!dq);
               continue;
            case '\\':
               bs = TRUE;
               continue;
            default:
               ;
         }
      else
         bs = FALSE;
      if (dst != from)
         *dst = *from;
      dst++;
   }
   *dst = '\0';
   return (dst - to);
}

/*
 * Used by addr_msgid to check the part left of the '@'
 */

int addr_idleft (char *buf)
{
   char *p;
   bool dq, bs = FALSE;
   
   p = buf;
   if ((dq = (*p == '"')))
      p++;
   
   for (; *p; p++)
      if (!bs)
         switch (*p)
         {
            case '"':
               if (dq && *++p == '@')
                  return (p - buf);
               else
                  return 0;
            case '\\':
               bs = TRUE;
               break;
            case '>':
               return 0;
            case '@':
               if (!dq)
                  return (p - buf);
               else
                  return 0;
            default:
               if (*p < 33 || *p > 126)
                  return 0;
         }
      else
         bs = FALSE;
   
   return 0;
}

/*
 * Used by addr_msgid() to check the part right of the '@'
 */

int addr_idright (char *buf)
{
   char *p;
   bool lb, bs = FALSE;

   p = buf;
   if ((lb = (*p == '[')))
      p++;

   for (; *p; p++)
      if (!bs)
         switch (*p)
         {
            case '[':
               return 0;
            case ']':
               if (lb && *++p == '>')
                  return (p - buf);
               else
                  return 0;
            case '\\':
               bs = TRUE;
               break;
            case '>':
               if (!lb)
                  return (p - buf);
               else
                  return 0;
            default:
               if (*p < 33 || *p > 126)
                  return 0;
         }
      else
         bs = FALSE;

   return 0;
}
