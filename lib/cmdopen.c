/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

static const char ver_ctrl_id[] = "$Id: cmdopen.c 29 2004-04-24 23:02:38Z patrik $";

int cmdopen (char **command, int *read, int *write)
{
   int p0[2] = { -1, -1 };
   int p1[2] = { -1, -1 };
   int pid = 0;
   int er = 0;
   int s;

   if (read) er = pipe(p0);
   if (write) if (0 == er) er = pipe(p1);
   if (-1 == er) goto fail;
   pid = fork();
   if (-1 == pid) goto fail;
   if (0 == pid)
   {
      if (read && write)
      {
         dup2(p0[0], 0);
         dup2(p1[1], 1);
      }
      else
      {
         if (read) { close(p0[0]); dup2(p0[1], 1); }
         if (write) { close(p1[1]); dup2(p1[0], 0); }
      }
      execvp(command[0], command);
      exit(19);
   }
   if (!(read && write))
   {
      if (read) { close(p0[1]); *read = p0[0]; }
      if (write) { close(p1[0]); *write = p1[1]; }
   }
   else
   {
      *read = p0[0]; *write = p1[1];
   }
   switch (waitpid(pid, &s, WNOHANG))
   {
      case 0:
         return pid;
      case -1:
         pid = -1;
         break;
      default:
         if (WIFEXITED(s))
            pid = 0 - WEXITSTATUS(s);
         else if (WIFSIGNALED(s))
            pid = 0 - (128 + WTERMSIG(s));
   }

fail:
   if (p0[0] > -1) close(p0[0]);
   if (p0[1] > -1) close(p0[1]);
   if (p1[0] > -1) close(p1[0]);
   if (p1[1] > -1) close(p1[1]);
   return pid;
}

int cmdopensh (char *cmd, int *read, int *write)
{
   char *commands[4] = { "sh", "-c", 0, 0 };

   commands[2] = cmd;
   return cmdopen(commands, read, write);
}

int cmdwait (int pid)
{
   int s;

   if (-1 == waitpid(pid, &s, 0))
      return -1;
   if (WIFEXITED(s))
      return WEXITSTATUS(s);
   else if (WIFSIGNALED(s))
      return (128 + WTERMSIG(s));
   return 0;
}
