/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Globbing function.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "defs.h"

static const char ver_ctrl_id[] = "$Id: wildmat.c 29 2004-04-24 23:02:38Z patrik $";

#if 1
#define TOLOWER(x) tolower(x) /* case insensitive */
#else
#define TOLOWER(x) x /* case sensitive */
#endif

static int match (char *candidate, char *pattern);

static int bracket (char *candidate, char *pattern)
{
   bool negate = FALSE;
   char *end;

   end = ++pattern;
   while (*end && ']' != *end) { if ('\\' == *end) end++; end++; }
   if (!*end) return -1;
   if ('^' == *pattern) { negate = TRUE; pattern++; }
   if ('-' == *pattern) goto isnormalchar;
   while (*pattern)
   {
      char c;
      int flag;

      if ('\\' == *pattern) { pattern++; goto isnormalchar; }
      if (']' == *pattern) return (1 + match(candidate + 1, end + 1));
      switch (pattern[1])
      {
         case '\0': return -1;
         case '-':
            c = TOLOWER(*candidate);
            flag = (c >= TOLOWER(*pattern) && c <= TOLOWER(pattern[2]));
            if (flag) { if (negate) return -1; else pattern += 2; }
            else { if (negate) pattern += 2; else return -1; }
            break;
isnormalchar:
         default:
            if (TOLOWER(*candidate) == TOLOWER(*pattern))
            {
               if (negate) pattern++; else return (1 + match(candidate + 1, end + 1));
            }
            else 
               if (negate) return (1 + match(candidate + 1, end + 1)); else pattern++;
      }
   }
   return -1;
}

static int match (char *candidate, char *pattern)
{
   int ret = 0;

   /* if( (!*candidate) ^ (!*pattern) )return(-1); */

   while (1)
   {
      if ('\\' == *pattern) goto default_char;
      switch (*pattern)
      {
         case '\r': case '\n': case '\0':
            switch (*candidate)
            {
               case '\0': case '\r': case '\n': return ret;
            }
            return -1;
         case '*': /* return(ret + star(candidate, pattern)); */
            {
               char *c;
               int len;
               int clen;

               while ('*' == *pattern) pattern++;
               if (!*pattern) return (ret + strcspn(candidate, "\r\n"));
               clen = strcspn(candidate, "\r\n");
               for (c = candidate; clen && *c; c++, clen--, pattern)
               {
                  switch (*c)
                  {
                     case '\r': case '\n': return -1;
                  }
                  len = match(c, pattern);
                  if (len == clen) return (len + (c - candidate));
               }
               return -1;
            }
            /* Not Reached */
         case '[':
            return (ret + bracket(candidate, pattern));
         case '?':
            pattern++;
            if (!*candidate) return -1;
            ret++; candidate++;
            continue;

default_char: pattern++; /* Fall Through */
         default:
            if (TOLOWER(*pattern) == TOLOWER(*candidate))
            {
               pattern++; candidate++; ret++;
            }
            else
               return -1;
      }
   }
}

int wildmat (char *candidate, char *pattern)
{
   if (match(candidate, pattern) == strcspn(candidate, "\r\n"))
      return 1;
   return 0;
}

#undef TOLOWER
