/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Work like the script.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "config.h"
#include "path.h"
#include "parameters.h"
#include "valid.h"
#include <opt.h>
#include <out.h>
#include <cmdopen.h>
#include <format.h>

static const char ver_ctrl_id[] = "$Id: sndelgroup.c 29 2004-04-24 23:02:38Z patrik $";

int debug = 0;

void usage (void) { fail(1, "Usage:%s newsgroup ...", progname); }

int main (int argc, char **argv)
{
   int i;
   char *cp;
   char *v[5];
   DIR *dir;
   struct dirent *dp;
   int pid;

   progname = ((cp = strrchr(argv[0], '/')) ? cp + 1 : argv[0]);

   while ((i = opt_get(argc, argv, "")) > -1)
      switch (i)
      {
         case 'P': log_with_pid(); break;
         case 'd': debug++; break;
         case 'V': version(); _exit(0);
         default: usage();
      }
   if (opt_ind >= argc)
      usage();

   parameters(TRUE);
   if (-1 == chdir(snroot))
      fail(2, "Can't chdir(%s):%m", snroot);
   set_path_var();

   LOG1("Deleting specified groups under %s", snroot);

   for (i = opt_ind; i < argc; i++)
      if (!is_valid_group(argv[i]))
         if (strcmp(argv[i], JUNK_GROUP))
            fail(1, "%s is not a newsgroup", argv[i]);

   for (i = opt_ind; i < argc; i++)
      if (0 == chdir(argv[i]) && (dir = opendir(".")))
      {
         v[0] = "snexpire";
         v[1] = "-0d";
         v[2] = argv[i];
         v[3] = 0;
         LOG1("expiring %s", argv[i]);
         pid = cmdopen(v, 0, 0);
         if (pid <= 0)
            fail(2, "Unable to run snexpire on %s", argv[i]);
         if (cmdwait(pid))
            fail(2, "snexpire failed on %s", argv[i]);
         while ((dp = readdir(dir)))
         {
            cp = dp->d_name;
            if ('.' == *cp)
               if (!cp[1] || ('.' == cp[1] && !cp[2]))
                  continue;
            LOG1("Removing file %s/%s", argv[i], cp);
            unlink(dp->d_name);
         }
         closedir(dir);
         chdir("..");
         LOG1("Removing group directory \"%s\"", argv[i]);
         if (-1 == rmdir(argv[i]))
            fail(2, "Can't remove %s:%m", argv[i]);
         LOG("Removed group \"%s\" from under %s", argv[i], snroot);
      }
      else
         LOG("Ignoring %s, can't chdir:%m", argv[i]);

   _exit(0);
}
