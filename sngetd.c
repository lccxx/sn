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
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include "config.h"
#include "get.h"
#include "parameters.h"
#include "path.h"
#include "addr.h"
#include "valid.h"
#include <out.h>
#include <opt.h>
#include <format.h>
#include <statf.h>
#include <time.h>

static const char ver_ctrl_id[] = "$Id: sngetd.c 29 2004-04-24 23:02:38Z patrik $";

int debug = 0;
static int fifofd;
static int age = (10 * 60); /* don't fetch group within 10 min. of last fetch */

struct get {
   struct get *next;
   time_t t;
   char group[1];
};

static struct get *recently = NULL;

static void readgroup (void)
{
   static char inbuf[GROUPNAMELEN + 1];
   static int start = 0;
   static int used = 0;
   time_t t;
   int c;

   if (used == start)
      used = start = 0;
   if ((c = read(fifofd, inbuf + used, (sizeof (inbuf) - 1) - used)) <= 0)
      return;
   used += c;

   time(&t);

   for (;;)
   {
      char *p;
      int end;

      for (end = start; end < used && '\n' != inbuf[end]; end++) ;
      if (used == end)
      {
         /* partial */
         if (start > 0)
         {
            memmove(inbuf, inbuf + start, used - start);
            used -= start;
         }
         else
            used = 0;
         start = 0;
         return;
      }

      /* got a group name */
      inbuf[end] = '\0';
      p = inbuf + start;

      if (end - start < GROUPNAMELEN && '[' != *p)
      {
         struct get *gp;
         struct get *tmp;
         struct stat st;
         char ch;

         if (!is_valid_group(p))
         {
            LOG1("%s is not a valid name", p);
         }
         else if (0 == readlink(p, &ch, 1))
         {
            LOG1("%s is a symlink", p);
         }
         else if (-1 == statf(&st, "%s/.outgoing", p))
         {
            LOG1("%s is a local group", p);
         }
         else if (!S_ISDIR(st.st_mode))
         {
            LOG1("%s is not a global group", p);
         }
         else
         {
            for (tmp = 0, gp = recently; gp; tmp = gp, gp = gp->next)
               if ((unsigned long) gp->t + age < (unsigned long) t)
               {
                  if (tmp)
                     tmp->next = 0;
                  else
                     recently = NULL;
                  do
                  {
                     tmp = gp->next;
                     free(gp);
                  }
                  while ((gp = tmp));
                  gp = recently;
                  break;
               }
               else if (0 == strcmp(gp->group, p))
                  break;

            if (!gp)
            {
               if (0 == add(p))
               {
                  LOG1("Will fetch \"%s\"", p);
                  if ((gp = malloc(sizeof (struct get) + strlen(p))))
                  {
                     strcpy(gp->group, p);
                     gp->t = t;
                     gp->next = recently;
                     recently = gp;
                  }
                  else
                     LOG("Oops, no memory to remember I'm fetching %s", p);
               }
            }
            else
               LOG1("Already fetched/fetching for %s", p);
         }
      }

      start = end + 1;
   }
}

void usage (void)
{
   fail(1, "Usage:%s [-a age] [-h Bps] [-t timeout] "
        "[-p concurrency] [-c pipelinedepth]", progname);
}

int main (int argc, char **argv)
{
   char optdebugbuf[7];
   int n, mark;
   char *cp;
   fd_set rset;

   progname = ((cp = strrchr(argv[0], '/')) ? cp + 1 : argv[0]);

   concurrency = 2;
   while ((n = opt_get(argc, argv, "apthcmo")) > -1)
      switch (n)
      {
         case 'a':
            if (!opt_arg)
               usage();
            if ((age = strtoul(opt_arg, &cp, 10)) <= 0 || *cp)
               fail(1, "Refetch age must be positive");
            break;
         case 'd':
            if (++debug < 5)
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
            if (concurrency > 4 || concurrency <= 0 || *cp)
               fail(1, "Bad value for concurrency option -p");
            break;
         case 'h':
            if (!opt_arg)
               usage();
            throttlerate = strtoul(opt_arg, &cp, 10);
            if (throttlerate < 0 || *cp)
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
   if (-1 == dup2(2, 6) || -1 == dup2(2, 7) || -1 == dup2(2, 1))
      fail(2, "Can't dup standard error:%m");
   parameters(TRUE);
   if (-1 == chdir(snroot))
      fail(2, "chdir(%s):%m", snroot);
   fifofd = open(".fifo", O_RDWR);
   if (-1 == fifofd)
   {
      if (ENOENT == errno)
         fail(1, "%s/.fifo doesn't exist", snroot);
      else
         fail(2, "Can't open %s/.fifo:%m", snroot);
   }
   /* drain the fifo */
   if (-1 == (n = fcntl(fifofd, F_GETFL)) || -1 == (n = fcntl(fifofd, F_SETFL, n | O_NONBLOCK)))
      fail(2, "fcntl(fifo):%m");
   {
      char tmp[10];

      while (read(fifofd, tmp, sizeof (tmp)) > 0) ;
   }
   fcntl(fifofd, F_SETFL, n);
   fcntl(fifofd, F_SETFD, 1);
   init();
   if (-1 == set_path_var())
      fail(2, "No memory");

   /*
    * Alternative: leave sockets open, let server time us out.
    * Not very neighbourly...
    */
   
   optnocache = TRUE;

   for (mark = 0;; mark++)
   {
      struct timeval tv;
      int max;

      if (jobs_not_done())
         while (0 == sow()) ;

      if (sigchld || 1 == mark % 20)
      { /* Can't avoid signal loss */
         sigchld = FALSE;
         while (0 == reap()) ;
      }

      FD_ZERO(&rset);
      FD_SET(fifofd, &rset);

      if (throttlerate)
      {
         max = throttle_setfds(&rset);
         if (max < fifofd)
            max = fifofd;
         if (sigusr)
         {
            sigusr = FALSE;
            LOG("throttling at %d bytes/sec", throttlerate);
         }
      }
      else
         max = fifofd;

      tv.tv_sec = 5;
      tv.tv_usec = 0;
      if (select(max + 1, &rset, NULL, NULL, &tv) > -1)
      {
         if (throttlerate)
            throttle(&rset);
         if (FD_ISSET(fifofd, &rset))
            readgroup();
      }
   }
   _exit(0);
}
