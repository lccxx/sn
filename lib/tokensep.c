/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#include <stdlib.h>

static const char ver_ctrl_id[] = "$Id: tokensep.c 40 2004-04-28 18:00:25Z patrik $";

char *tokensep (char **p, char *delim)
{
   char map[256] = { 0, };
   char *start;
   char *end;

   while (*delim)
   {
      map[(unsigned) *delim] = 1;
      delim++;
   }
   for (start = *p; *start && map[(unsigned) *start]; start++) ;
   if (!*start)
      return NULL;
   for (end = start; *end && !map[(unsigned) *end]; end++) ;
   if (*end)
   {
      *p = end + 1;
      *end = '\0';
   }
   else
      *p = end;
   return start;
}
