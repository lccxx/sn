/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <string.h>
#include "config.h"
#include "get.h"
#include "parameters.h"
#include "path.h"
#include "valid.h"
#include <out.h>
#include <opt.h>
#include <statf.h>
#include <format.h>

static const char ver_ctrl_id[] = "$Id: snget.c 36 2004-04-27 16:52:02Z patrik $";

int debug = 0;

void usage (void)
{
   fail(1, "Usage:%s [-h Bps] "
        "[-p concurrency] [-c pipelinedepth] [-m max]", progname);
}

/* Main function - uses variables and functions from get.c */

int main (int argc, char **argv)
{
   char optdebugbuf[7];
   int n, mark;
   char *cp;

   progname = ((cp = strrchr(argv[0], '/')) ? cp + 1 : argv[0]);

   while ((n = opt_get(argc, argv, "pthcmo")) > -1)
      switch (n)
      {
         case 'd':
            if (++debug < 6)
	    {
               if (!optdebug)
                  strcpy(optdebug = optdebugbuf, "-d");
               else
                  strcat(optdebug, "d");
            }
            break;
         case 'V': version(); _exit(0);
         case 't':
            if (!opt_arg)
               usage();
            LOG("option \"-t %s\" no longer supported", opt_arg);
            break;
         case 'p':
            if (!opt_arg)
               usage();
            concurrency = strtoul(opt_arg, &cp, 10);
            if (concurrency > MAX_CONCURRENCY || concurrency <= 0 || *cp)
               fail(1, "Bad value for concurrency option -p");
            break;
         case 'h':
            if (!opt_arg)
               usage();
            throttlerate = strtoul(opt_arg, &cp, 10);
            if (throttlerate < 0)
               fail(1, "Bad value for throttle option -h");
            break;
         case 'c':
            if (!(optpipelining = opt_arg))
               usage();
            if (strtoul(optpipelining, &cp, 10) < 0 || *cp)
               fail(1, "Bad value for pipeline option -c");
            break;
         case 'm':
            if (!(optmax = opt_arg))
               usage();
            if (strtoul(optmax, &cp, 10) <= 0 || *cp)
               fail(1, "Bad value for max-prime-articles option -m");
            break;
         case 'P':
            optlogpid = TRUE;
            log_with_pid();
            break;
         default:
            usage();
      }
   close(0);
   open("/dev/null", O_RDONLY);
   /* snag 6 and 7 so we can dup onto them */
   if (-1 == dup2(2, 6) || -1 == dup2(2, 7) || -1 == dup2(2, 1))
      fail(2, "Unable to dup standard error:%m");

   parameters(TRUE);
   if (-1 == chdir(snroot))
      fail(2, "chdir(%s):%m", snroot);
   init();
   if (-1 == set_path_var())
      fail(2, "No memory");

   n = 0;
   if (opt_ind == argc)
   {
      DIR *dir;
      struct dirent *dp;
      struct stat st;
      char ch;

      if (!(dir = opendir(".")))
         fail(2, "opendir(%s):%m", snroot);
      while ((dp = readdir(dir)))
         if (is_valid_group(dp->d_name))
            if (-1 == readlink(dp->d_name, &ch, 1)) /* no symlinks */
               if (0 == statf(&st, "%s/.outgoing", dp->d_name))
                  if (S_ISDIR(st.st_mode))
                     if (add(dp->d_name) > -1)   /* NB: add() from get.c */
                        n++;
      closedir(dir);
   }
   else
   {
      debug++;
      for (; opt_ind < argc; opt_ind++)
         if (add(argv[opt_ind]) > -1)
            n++;
      debug--;
   }

   if (n == 0)
      fail(0, "No groups to fetch");

   for (mark = 0; jobs_not_done(); mark++)
   {
      struct timeval tv;
      fd_set rset;
      int max;

      while (sow() == 0)   /* Start some jobs */
         ;
      FD_ZERO(&rset);
      if (throttlerate)
      {
         max = throttle_setfds(&rset);
         if (sigusr)
         {
            sigusr = FALSE;
            LOG("throttling at %d bytes/sec", throttlerate);
         }
      }
      else
         max = -1;
      
      tv.tv_sec = 1;
      tv.tv_usec = 0;
      if (select(max + 1, &rset, NULL, NULL, &tv) > 0)
         if (throttlerate)
            throttle(&rset);
      
      if (sigchld || 1 == mark % 10)
      {
         sigchld = FALSE;
         while (reap() == 0)
            ;
      }
   }
   quit();
   _exit(0);
}
