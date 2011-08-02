/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Make sure our BINDIR exists in $PATH
 */

#include <stdlib.h>
#include <string.h>

static const char ver_ctrl_id[] = "$Id: path.c 29 2004-04-24 23:02:38Z patrik $";

int set_path_var (void)
{
   char *p;
   char *path;

   if (!(p = getenv("PATH")))
      return putenv("PATH=" BINDIR);
   if ((path = strstr(p, BINDIR)))
      if (path == p || ':' == path[-1])
         if (!path[sizeof (BINDIR) - 1] || ':' == path[sizeof (BINDIR) - 1])
            return 0;
   path = malloc(strlen(p) + sizeof ("PATH=:" BINDIR) + 2);
   if (!path)
      return -1;
   strcpy(path, "PATH=");
   strcat(path, p);
   strcat(path, ":" BINDIR);
   return putenv(path);
}
