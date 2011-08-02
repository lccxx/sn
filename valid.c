/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Check if a newsgroup name is valid.
 * Thanks pradman.
 */

#include <sys/stat.h>
#include <string.h>
#include "config.h"
#include "parameters.h"

static const char ver_ctrl_id[] = "$Id: valid.c 29 2004-04-24 23:02:38Z patrik $";

#define C(x) case x:

bool is_valid_name (char *name)
{
   register char *p;

   if (!*name)
      return FALSE;
   for (p = name; *p; p++)
      switch (*p)
      {
         C('-') C('.') if (p == name || *p == p[1] || !p[1])
            return FALSE;
         C('a') C('b') C('c') C('d') C('e') C('f') C('g') C('h') C('i')
         C('j') C('k') C('l') C('m') C('n') C('o') C('p') C('q') C('r')
         C('s') C('t') C('u') C('v') C('w') C('x') C('y') C('z')
         C('A') C('B') C('C') C('D') C('E') C('F') C('G') C('H') C('I')
         C('J') C('K') C('L') C('M') C('N') C('O') C('P') C('Q') C('R')
         C('S') C('T') C('U') C('V') C('W') C('X') C('Y') C('Z')
         C('_') C('+')
         C('0') C('1') C('2') C('3') C('4') C('5') C('6') C('7') C('8') C('9')
	    continue;
         default:
            return FALSE;
      }
   if (p - name > GROUPNAMELEN)
      return FALSE;
   return TRUE;
}

bool is_valid_group (char *name)
{
   char path[GROUPNAMELEN + sizeof ("/.created")];
   struct stat st;

   if (!is_valid_name(name))
      return FALSE;
   strcat(strcpy(path, name), "/.created");
   if (0 == stat(path, &st))
      if (S_ISREG(st.st_mode))
         if (0 == stat(name, &st))
            if (snuid == st.st_uid)
               if (sngid == st.st_gid)
                  return TRUE;
   return FALSE;
}
