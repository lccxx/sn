/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Accept a post from the network.
 * Does not check for .nopost, relies on SNPOST script for that.
 * Oddly, LIST checks .nopost, in group_info().  This is a bit
 * inconsistent, since if snntpd says can't post here, we should not
 * accept the article, but we do, then discard.  In a later version I
 * might just have LIST always have the posting flag set to 'y',
 * since according to RFC it is almost meaningless.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "config.h"
#include "snntpd.h"
#include "args.h"
#include "unfold.h"
#include <readln.h>
#include <b.h>
#include <format.h>
#include <out.h>

static const char ver_ctrl_id[] = "$Id: post.c 29 2004-04-24 23:02:38Z patrik $";

static int tmpfd = -1;
static bool have_newsgroups;
static bool have_control;
static int error;

#define ER_READ 1
#define ER_WRITE 2
#define ER_FMT 3
#define ER_EOF 4
#define ER_MEM 5

static char *getval (char *field)
{
   while (*field && ':' != *field)
      field++;
   field++;
   while (' ' == *field || '\t' == *field)
      field++;
   return field;
}

static int write_tmp (char *line, int len)
{
   static char env_control[1000];
   static char env_newsgroups[1000];

   if (error)
      return 0;
   if (0 == strncasecmp(line, "X-sn-", 5))
      return 0;
   else if (!have_control && 0 == strncasecmp(line, "Control:", 8))
   {
      strcpy(env_control, "CONTROL=");
      strncat(env_control, getval(line), 991);
      if (putenv(env_control))
         error = ER_MEM;
      else
         have_control = TRUE;
   }
   if (-1 == write(tmpfd, line, len))
      error = ER_WRITE;
   if (-1 == write(tmpfd, "\r\n", 2))
      error = ER_WRITE;
   if (!have_newsgroups && 0 == strncasecmp(line, "Newsgroups:", 11))
   {
      char *p;

      for (line = p = getval(line); *p; p++)
         if (',' == *p)
            *p = ' ';
      strcpy(env_newsgroups, "NEWSGROUPS=");
      strncat(env_newsgroups, line, 988);
      if (putenv(env_newsgroups))
         error = ER_MEM;
      else
         have_newsgroups = TRUE;
   }
   return error;
}

void do_post (void)
{
   char buf[80];
   char *line;
   int len, p[2], pid, s;

   if (!posting_ok)
   {
      args_write(1, "440 Posting not allowed\r\n");
      return;
   }

   if (-1 == tmpfd)
   {
      formats(buf, sizeof (buf) - 1, "/tmp/.post.%d", getpid());
      tmpfd = open(buf, O_RDWR | O_CREAT | O_EXCL, 0644);
      if (-1 == tmpfd)
      {
         LOG("do_post:open(%s):%m", buf);
         goto internal;
      }
      unlink(buf);
   }
   error = 0;
   have_control = have_newsgroups = FALSE;
   putenv("CONTROL=");
   putenv("NEWSGROUPS=");
   
   args_write(1, "340 Go ahead\r\n");
   
   /* Writes everything into the tmp file */
   
   lseek(tmpfd, 0, SEEK_SET);
   switch (unfold(&input, write_tmp))
   {
      case -3: break;
      case -2: error = ER_FMT; break;
      case -1: LOG("do_post:read error:%m");
      case 0: return;
   }

   if (!error)
   {
      if (client_ip && *client_ip)
         writef(tmpfd, "NNTP-Posting-Host: %s\r\n", client_ip);
      if (-1 == write(tmpfd, "\r\n", 2))
         error = ER_WRITE;
   }
   while ((len = readln(&input, &line, '\n')) > 0)
   {
      if (!error)
      {
         if (--len > 0)
            if ('\r' == line[len - 1])
               --len;
         if (-1 == write(tmpfd, line, len))
            error = ER_WRITE;
         if (-1 == write(tmpfd, "\r\n", 2))
            error = ER_WRITE;
      }
      if (1 == len && '.' == *line)
         break;
   }

   if (!have_newsgroups)
   {
      args_write(1, "441 No Newsgroups line\r\n");
      return;
   }
   if (error)
   {
      switch (error)
      {
         case ER_READ: LOG("do_post: read error:%m?"); break;
         case ER_WRITE: LOG("do_post: write(tmp):%m"); break;
         case ER_MEM: LOG("do_post: no memory"); break;
         case ER_FMT: args_write(1, "441 Bad article format\r\n"); return;
         case ER_EOF: return;
      }
internal:
      args_write(1, "441 Internal error\r\n");
      return;
   }
   ftruncate(tmpfd, lseek(tmpfd, 0, SEEK_CUR));

   if (-1 == (pid = pipefork(p)))
   {
      LOG("do_post:pipe/fork:%m");
      goto internal;
   }

   if (0 == pid)
   {
      char *av[2];

      close(p[0]);
      lseek(tmpfd, 0, SEEK_SET);
      dup2(tmpfd, 0);
      dup2(p[1], 1);
      av[1] = 0;
      *av = "./.SNPOST";
      execv(*av, av);
      if (ENOENT == errno)
      {
         *av += 3;
         execvp(*av, av);
      }
      fail(2, "do_post:exec(%s):%m", *av);
   }

   close(p[1]);
   *buf = '\0';
   do
   {
      struct readln prog;

      if (readln_ready(p[0], 0, &prog))
         break;
      while ((len = readln(&prog, &line, '\n')) > 0)
      {
         line[--len] = '\0';
         LOG("SNPOST:%s", line);
         if (len > 0)
            if ('\r' == line[len - 1])
               --len;
         if (len > sizeof (buf) - 1)
            len = sizeof (buf) - 1;
         strncpy(buf, line, len)[len] = '\0';
      }
      readln_done(&prog);
   }
   while (0);

   while (-1 == waitpid(pid, &s, 0) && EINTR == errno) ;
   if (WIFEXITED(s))
   {
      if (0 == (s = WEXITSTATUS(s)))
      {
         args_write(1, "240 %s\r\n", *buf ? buf : "Yummy");
         return;
      }
      LOG("do_post:SNPOST died with %d", s);
   }
   else if (WIFSIGNALED(s))
      LOG("do_post:SNPOST caught signal %d", WTERMSIG(s));
   else
      LOG("do_post:SNPOST unknown wait status %d", s);

   args_write(1, "441 %s\r\n", *buf ? buf : "Posting failed");
}
