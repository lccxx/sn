/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Code common to snget and sngetd.  Schedules each newsgroup to
 * fetch, controls throttling.
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netinet/in.h>
#if 0
/* XXX */
#include <asm/types.h> /* ward off __u32 errors XXX */
#endif
#include <signal.h>
#include "config.h"
#include "get.h"
#include "addr.h"
#include "valid.h"
#include <cmdopen.h>
#include <out.h>
#include <format.h>
#include <opt.h>

static const char ver_ctrl_id[] = "$Id: get.c 36 2004-04-27 16:52:02Z patrik $";

struct job {
   int pid;
   int sd;
   int infd;    /* for throttling */
   int outfd;   /* either goes to snstore, or a file */
   int attempts;
   /* these are constant for the lifetime of the job */
   char *server;
   unsigned long addr;
   unsigned short port;
   char group[1];
};

/* opt* values are propagated to children */
char *optdebug = NULL;
char *optpipelining = NULL;
char *optmax = NULL;     /* only for priming a group (serial == 0) */
bool optlogpid = FALSE;
bool optnocache = FALSE;   /* internal option */
int throttlerate = 0;
int concurrency = 4;

extern int rename(const char *, const char *);

static char path1[(GROUPNAMELEN + sizeof ("/.serial.tmp")) + 1];
static char path2[(GROUPNAMELEN + sizeof ("/.serial.tmp")) + 1];

static char *argv_snstore[4] = { "snstore", };
static int argc_snstore;
static char *argv_snfetch[14] = { "snfetch", };
static int argc_snfetch;

#define MAX_CONCURRENCY 8
#define ASSERT(cond) /* nothing */

static int atou (char *buf)
{
   int i, c;

   for (i = 0; *buf; buf++)
   {
      c = *buf - '0';
      if (c > 9 || c < 0)
         return -1;
      i *= 10;
      i += c;
   }
   return i;
}

/* --------------------------------------------------
 * Handling store descriptors.  This is either a pipe to snstore
 * or an fd to a (common) file.
 */

static int stores[MAX_CONCURRENCY];
static int nstores = 0;

static int store_get (void)
{
   if (!nstores)
   {
      int pid;

      pid = cmdopen(argv_snstore, 0, stores);
      if (pid <= 0)
      {
         if (-19 == pid)
            LOG("store_get:exec snstore:PATH set?");
         else if (-1 == pid)
            LOG("store_get:exec snstore:%m?");
         else
            LOG("store_get:snstore exited with %d", 0 - pid);
         return -1;
      }
      fcntl(*stores, F_SETFD, 1);
      nstores++;
      LOG3("pipe %d is snstore pid %d", *stores, pid);
   }
   return stores[--nstores];
}

static void store_put (int fd)
{
   if (nstores < MAX_CONCURRENCY)
      stores[nstores++] = fd;
   else
      close(fd);
}

/* --------------------------------------------------
 * Handling and reusing socket descriptors.  Constructs and reuses,
 * but does not destroy; caller destroys sockets.
 */

struct sock {
   char *server;
   unsigned long addr;
   unsigned short sd;
   unsigned short port;
};

static struct sock socks[MAX_CONCURRENCY];
static int nsocks = 0;

static int sock_get (char *server, int port, unsigned long *addrp, int *reused)
{
   int i, sd;

   for (i = 0; i < nsocks; i++)
      if (port == socks[i].port)
         if (0 == strcasecmp(server, socks[i].server))
         {
            *reused = 1;
            sd = socks[i].sd;
            *addrp = socks[i].addr;
            nsocks--;
            if (i != nsocks)
               socks[i] = socks[nsocks];
            return sd;
         }

   /* XXX We could be passing a socket that isn't fully connected yet. */

   *reused = 0;
   if ((sd = socket(AF_INET, SOCK_STREAM, 0)) > -1)
   {
      struct hostent *hp;

      if ((hp = gethostbyname(server)))
      {
         struct sockaddr_in sa;
         int fl;

         sa.sin_family = AF_INET;
         sa.sin_port = htons(port);
         sa.sin_addr.s_addr = *(unsigned long *) (hp->h_addr_list[0]);
         *addrp = ntohl(sa.sin_addr.s_addr);
         fl = fcntl(sd, F_GETFL);
         fcntl(sd, F_SETFL, fl | O_NONBLOCK);
         i = connect(sd, (struct sockaddr *) &sa, sizeof (sa));
         if (i > -1 || EINPROGRESS == errno)
         {
            fcntl(sd, F_SETFL, fl);
            fcntl(sd, F_SETFD, 1);
            return sd;
         }
         else
            LOG("sock_get:connect to %s:%m?", server);
      }
      else
         LOG("sock_get:resolve \"%s\"", server);
      close(sd);
   }
   else
      LOG("sock_get:socket to %s:%m", server);
   return -1;
}

static void sock_put (int sd, char *server, unsigned long addr, int port)
{
   if (nsocks < MAX_CONCURRENCY)
   {
      socks[nsocks].sd = sd;
      socks[nsocks].server = server;
      socks[nsocks].port = port;
      socks[nsocks].addr = addr;
      nsocks++;
   }
   else
      close(sd);
}

/* --------------------------------------------------
 * Maintaining queues for jobs running and not yet done.
 *
 * queue: 1=============2===========3----4
 * 1: start of run q (index 0)
 * 2: end of run q, start of todo q (index toff)
 * 3: end of todo q, start of free space (index tend)
 * 4: end of space (qsize)
 */

static struct job **queue = NULL;
static int toff = 0;
static int tend = 0;
static int qsize;

static int queue_add (struct job *jp)
{
   if (tend >= qsize)
   {
      struct job **tmp;
      int newsize;

      newsize = (queue ? (qsize * 2) : 20);
      if (!(tmp = malloc(newsize * sizeof (struct job *))))
         return -1;
      if (queue)
      {
         memcpy(tmp, queue, qsize * sizeof (struct job *));
         free(queue);
      }
      queue = tmp;
      qsize = newsize;
   }
   queue[tend++] = jp;
   return 0;
}

static void swap (int a, int b)
{
   struct job *tmp;

   if (a == b)
      return;
   tmp = queue[a];
   queue[a] = queue[b];
   queue[b] = tmp;
}

static void queue_todo2run (int j) { ASSERT(j >= toff && j < tend) swap(toff++, j); }
static void queue_run2todo (int j) { ASSERT(j > -1 && j < toff) swap(--toff, j); }

static void queue_rm (int j)
{
   ASSERT(tend > 0)
   if (j < toff)
   {
      queue_run2todo(j);
      j = toff;
   }
   swap(j, --tend);
   free(queue[tend]);
}

static int similarity (char *g1, char *g2)
{
   char *c1;
   int inc1;
   char *c2;
   int inc2;
   int score;

   score = 0;
   for (inc1 = 7; inc1 && *g1; inc1--)
   {
      inc2 = 7;
      for (c2 = g2; inc2 && *c2; inc2--)
      {

         for (c1 = g1; *c1 == *c2 && *c1 && '.' != *c1; c1++, c2++)
            score++;
         if (*c1 == *c2) /* both dots or \0 */
            score += inc1 + inc2; /* bonus */

         do
            if (!*c2)
               break;
         while ('.' != *c2++) ;
      }
      do
         if (!*g1)
            break;
      while ('.' != *g1++) ;
   }
   return score;
}

/*
 * Choose a new group to run, unlike any other currently running.
 */

static int queue_getnext (void)
{
   int max;
   int bestscore, bestgroup;
   int t, r;

   switch ((max = tend - toff))
   {
      case 0:
         return -1;
      case 1:
         return toff;
      default:
         max = 4;
      case 2:
      case 3:
      case 4:;
   }

   bestscore = 10000;
   bestgroup = toff;
   for (t = toff; t < toff + max; t++)
   { /* todo */
      int score;

      score = 0;
      for (r = 0; r < toff; r++) /* running */
         score += similarity(queue[r]->group, queue[t]->group);
      if (score < bestscore)
      {
         bestscore = score;
         bestgroup = t;
      }
   }
   return bestgroup;
}

/* --------------------------------------------------
 * Adding throttling.  Caller hands us 2 descriptors, fromfd is the
 * network socket, tofd is a pipe.  We don't care who it belongs to.
 */

int throttle_setfds (fd_set * rs)
{
   int max, i;

   max = -1;
   for (i = 0; i < toff; i++)
      if (queue[i]->infd > -1)
      {
         FD_SET(queue[i]->sd, rs);
         if (queue[i]->sd > max)
            max = queue[i]->sd;
      }
   return max;
}

/*
 * Select on the sockets (read), not the pipes (write),
 * because we assume snstore won't block us (for long).
 */

void throttle (fd_set * rs)
{
   static struct timeval last = { 0, };
   char buf[300]; /* mustn't be too big, for pipes */
   static int bytes = 0;
   int i, c;

   /* Pay for the bytes transferred last time. */

   if (bytes > 0)
   {
      struct timeval tv;
      long sec, msec, usec;

      gettimeofday(&tv, 0);
      sec = tv.tv_sec - last.tv_sec;
      usec = tv.tv_usec - last.tv_usec;
      if (usec < 0)
      {
         usec += 1000000;
         sec--;
      }
      msec = (bytes * 1000) / throttlerate;
      if (msec > 0)
      {
         sec = (msec / 1000) - sec;
         usec = ((msec % 1000) * 1000) - usec;
         if (usec < 0)
         {
            usec += 1000000;
            sec--;
         }
         tv.tv_sec = sec;
         tv.tv_usec = usec;
         if (sec >= 0)
            select(0, NULL, NULL, NULL, &tv);
      }
      bytes = 0;
   }

   bytes = 0;
   for (i = 0; i < toff; i++)
      if (queue[i]->infd > -1 && FD_ISSET(queue[i]->sd, rs))
         /* ignore socket read errors */
         if ((c = read(queue[i]->sd, buf, sizeof (buf))) > 0)
         {
            if (-1 == write(queue[i]->infd, buf, c))
            {
               LOG("throttle:pipe to %s:%m", queue[i]->group);
               close(queue[i]->infd);
               queue[i]->infd = -1;
            }
            else
               bytes += c;
         }

   gettimeofday(&last, 0);
}

/* --------------------------------------------------
 * Choosing groups to run and running programs to fetch for them.
 */

static void readfile (char *dir, char *fn, char buf[20])
{
   int fd, c, i;

   formats(path1, sizeof (path1) - 1, "%s/%s", dir, fn);
   if ((fd = open(path1, O_RDONLY)) > -1)
   {
      c = read(fd, buf, 19);
      close(fd);
      if (c > 0)
      {
         buf[c] = '\0';
         for (i = 0; i < c; i++)
         {
            if ('\n' == buf[i])
            {
               buf[i] = '\0';
               break;
            }
            if (buf[i] < '0' || buf[i] > '9')
            {
               LOG("readfile:bad value in %s/%s", dir, fn);
               *buf = '\0';
               return;
            }
         }
         return;
      }
      else
         LOG("readfile:read %s:%m", path1);
   }
   else if (ENOENT != errno)
      LOG("readfile:open %s:%m", path1);
   *buf = '\0';
}

int reap (void)
{
   int pid, ex, j;
   struct job *jp;

   if (toff <= 0 || (pid = waitpid(-1, &ex, WNOHANG)) <= 0)
      return 1;

   for (j = toff - 1; j > -1 && pid != queue[j]->pid; j--) ;
   if (-1 == j)
      return 1;

   jp = queue[j];
   if (WIFEXITED(ex))
   {
      if ((ex = WEXITSTATUS(ex)))
         LOG("reap:job %s exited %d", jp->group, ex);
   }
   else if (WIFSIGNALED(ex))
   {
      LOG("reap:job %s caught signal %d", jp->group, ex = WTERMSIG(ex));
      ex = 0 - ex;
   }
   else
   {
      ASSERT(0)
      return 1;
   }

   /*
    * If child failed in any way, close socket, otherwise save it.
    * If child failed from signal or system error, close store pipe,
    * otherwise it's still useable so save it.
    */
   
   if (ex || optnocache)
      close(jp->sd);
   else
      sock_put(jp->sd, jp->server, jp->addr, jp->port);
   if (2 == ex || ex < 0 || optnocache)
      (void) close(jp->outfd);
   else
      store_put(jp->outfd);
   if (jp->infd > -1)
      close(jp->infd);

   if (ex && jp->attempts <= 2)
   {
      jp->infd = jp->sd = jp->outfd = jp->pid = -1;
      queue_run2todo(j);
      return 1;
   }
   if (0 == ex)
   {
      formats(path1, sizeof (path1) - 1, "%s/.serial.tmp", jp->group);
      formats(path2, sizeof (path2) - 1, "%s/.serial", jp->group);
      if (-1 == rename(path1, path2))
         if (ENOENT != errno)
            LOG("reap:rename %s, %s:%m", path1, path2);
   }
   else
      LOG("reap:Giving up on %s", jp->group);
   queue_rm(j);
   return 0;
}

bool sigchld = FALSE;

static void chldhand (int x) { sigchld = TRUE; }

bool sigusr = FALSE;

static void usrhand (int u)
{
   sigusr = TRUE;
   if (SIGUSR1 == u)
   {
      if (throttlerate > 1)
         throttlerate /= 2;
   }
   else if (SIGUSR2 == u)
      throttlerate *= 2;
}

/* called only after fork */

static void fixfd (int from, int to)
{
   if (-1 == dup2(from, to))
      fail(2, "Can't dup %d to %d:%m", from, to);
}


int sow (void)
{
   int j, reused, p[2], tmo;
   struct job *jp;
   char maxbuf[20], serialbuf[20], timeoutbuf[20];
   char envgroup[GROUPNAMELEN + sizeof ("NEWSGROUP=")];
   char envserver[256];
   char envport[20 + sizeof ("TCPREMOTEPORT=")];
   char envip[20 + sizeof ("TCPREMOTEIP=")];
   char envtimeout[20 + sizeof ("TIMEOUT=")];

   if (toff >= concurrency)
      return 1;
   if (-1 == (j = queue_getnext()))
      return 1;
   jp = queue[j];
   jp->attempts++;

   do
   {
      if ((jp->outfd = store_get()) > -1)
      {
         if ((jp->sd = sock_get(jp->server, jp->port, &jp->addr, &reused)) > -1)
         {
            if (!throttlerate || pipe(p) > -1)
            {
               if ((jp->pid = fork()) > -1)
               {
                  if (0 == jp->pid)
                     break;
                  if (throttlerate)
                  {
                     close(p[0]);
                     jp->infd = p[1];
                  }
                  else
                     jp->infd = -1;
                  queue_todo2run(j);
                  return 0;
               }
               else
                  LOG("sow:fork for %s:%m", jp->group);
            }
            else
               LOG("sow:pipe for %s:%m", jp->group);
            close(jp->sd); /* Can't reuse, not logged in */
            jp->sd = -1;
         }
         store_put(jp->outfd);
         jp->outfd = -1;
      }
      return -1;
   }
   while (0);

   /* child */

   if (chdir(jp->group))
      fail(2, "Can't chdir(%s):%m", jp->group);
   readfile(".outgoing", ".timeout", timeoutbuf);
   argv_snfetch[argc_snfetch++] = "-t";
   if ((tmo = atou(timeoutbuf)) <= 0)
   {
      tmo = 120;
      strcpy(timeoutbuf, "120");
   }
   LOG3("%s on socket %d, pipe %d", jp->group, jp->sd, jp->outfd);
   argv_snfetch[argc_snfetch++] = timeoutbuf;
   argv_snfetch[argc_snfetch++] = jp->group;

   formats(envgroup, sizeof (envgroup) - 1, "NEWSGROUP=%s", jp->group);
   putenv(envgroup);
   formats(envtimeout, sizeof (envtimeout) - 1, "TIMEOUT=%s", timeoutbuf);
   putenv(envtimeout);
   formats(envserver, sizeof (envserver) - 1, "SERVER=%s", jp->server);
   putenv(envserver);
   formats(envport, sizeof (envport) - 1, "TCPREMOTEPORT=%d", (int) jp->port);
   putenv(envport);
   strcpy(envip, "TCPREMOTEIP=");
   {
      unsigned char ch;
      unsigned long a;
      char buf[32];
      char *p;

      a = jp->addr;
      p = buf + 31;
      *p-- = '\0';
      ch = a & 0xff;
      do
         *p-- = '0' + (ch % 10);
      while ((ch /= 10));
      *p-- = '.';
      ch = (a >> 8) & 0xff;
      do
         *p-- = '0' + (ch % 10);
      while ((ch /= 10));
      *p-- = '.';
      ch = (a >> 16) & 0xff;
      do
         *p-- = '0' + (ch % 10);
      while ((ch /= 10));
      *p-- = '.';
      ch = (a >> 24) & 0xff;
      do
         *p-- = '0' + (ch % 10);
      while ((ch /= 10));
      strcat(envip, p + 1);
   }
   putenv(envip);

   fixfd(jp->sd, 7);
   fixfd(throttlerate ? p[0] : jp->sd, 6);
   fixfd(jp->outfd, 1);

   if (!reused)
   {
      int pid, s;
      fd_set set;

      sigchld = FALSE;
      if (-1 == (pid = fork()))
         fail(2, "Can't fork for SNHELLO:%m");
      if (0 == pid)
      {
         char *v[2];

         if (-1 == chdir(".outgoing"))
            fail(2, "chdir(%s/.outgoing):%m", jp->group);
         fixfd(2, 1);
         v[1] = 0;
         *v = "./.SNHELLO";
         execv(*v, v);
         *v = "SNHELLO";
         execvp(*v, v);
         fail(2, "Can't exec %s:%m", *v);
      }
      FD_ZERO(&set);
      while (!sigchld)
      {
         struct timeval tv;

         tv.tv_sec = tmo;
         tv.tv_usec = 0;
         FD_SET(jp->sd, &set);
         if (select(jp->sd + 1, &set, NULL, NULL, &tv))
         {
            tv.tv_sec = 2;
            select(0, NULL, NULL, NULL, &tv);
         }
         else
            kill(pid, SIGALRM);
      }
      while (-1 == waitpid(pid, &s, 0) && EINTR == errno) ;
      /*
       *  XXX Just because our child died doesn't mean it's children
       *  have died, so it's possible that a bastard grandchild has
       *  stolen our connection.
       */
      if (WIFEXITED(s))
      {
         if ((s = WEXITSTATUS(s)))
            fail(2, "SNHELLO(%s) exited %d", jp->group, s);
      }
      else if (WIFSIGNALED(s))
         fail(2, "SNHELLO(%s) caught signal %d", jp->group, WTERMSIG(s));
      else
         fail(2, "SNHELLO(%s) unknown wait status %d", jp->group, s);
   }

   readfile(".", ".serial", serialbuf);
   argv_snfetch[argc_snfetch++] = serialbuf;
   if (*serialbuf == '\0')
   {
      LOG3("sow:couldn't get a value from %s/.serial:%m?", jp->group);
      strcpy(serialbuf, "0");
   }
   if (strcmp(serialbuf, "0") == 0)
      argv_snfetch[argc_snfetch++] = (optmax ? optmax : "200");
   else
   {
      readfile(".", ".max", maxbuf);
      argv_snfetch[argc_snfetch++] = (*maxbuf ? maxbuf : 0);
   }

   argv_snfetch[argc_snfetch] = 0;
   execvp("snfetch", argv_snfetch);
   fail(19, "sow:can't exec snfetch for %s:%m", jp->group);
   /* Not Reached */
   return -1;
}

void init (void)
{
   struct sigaction sa;

   sigemptyset(&sa.sa_mask);

   sa.sa_flags = 0;
   sa.sa_handler = chldhand; sigaction(SIGCHLD, &sa, 0);
   sa.sa_handler = usrhand; sigaction(SIGUSR1, &sa, 0);
   sa.sa_handler = usrhand; sigaction(SIGUSR2, &sa, 0);
   sa.sa_handler = SIG_IGN; sigaction(SIGPIPE, &sa, 0);

   argc_snstore = argc_snfetch = 1;
   if (optlogpid)
      argv_snstore[argc_snstore++] = argv_snfetch[argc_snfetch++] = "-P";
   if (optdebug)
      argv_snstore[argc_snstore++] = argv_snfetch[argc_snfetch++] = optdebug;
   if (optpipelining)
   {
      argv_snfetch[argc_snfetch++] = "-c";
      argv_snfetch[argc_snfetch++] = optpipelining;
   }
   argv_snstore[argc_snstore] = 0;
}

int add (char *group)
{
   struct s {
      struct s *next;
      unsigned short port;
      char server[1];
   };
   static struct s *servers = NULL;
   struct job *jp;
   int c, port;
   char *p;

   formats(path1, sizeof (path1) - 1, "%s/.outgoing", group);
   if ((c = readlink(path1, path2, sizeof (path2) - 1)) > 0)
   {
      path2[c] = '\0';
      if ((p = strrchr(path2, ':')) && (port = atou(p + 1)) > 0)
      {
         *p = '\0';
         if ((p = strrchr(path2, '/')))
         {
            struct s *sp;

            for (p++, sp = servers;; sp = sp->next)
               if (!sp)
               {
                  if ((sp = malloc(sizeof (struct s) + strlen(p))))
                  {
                     sp->port = port;
                     strcpy(sp->server, p);
                     sp->next = servers;
                     servers = sp;
                  }
                  break;
               }
               else if (port == sp->port)
                  if (0 == strcasecmp(sp->server, p))
                     break;
            if (sp && (jp = malloc(sizeof (struct job) + strlen(group) + 1)))
            {
               jp->port = port;
               jp->server = sp->server;
               strcpy(jp->group, group);
               jp->sd = jp->outfd = jp->pid = jp->infd = -1;
               jp->attempts = 0;
               if (0 == queue_add(jp))
                  return 0;
               free(jp);
            }
            LOG("add:No memory");
         }
         else
            LOG("add:%s has bad server symlink", group);
      }
      else
         LOG("add:%s server symlink missing port", group);
   }
   else
      LOG("add:Can't read symlink in %s:%m", group);
   return -1;
}

int jobs_not_done (void) { return tend; }

/*
 * Rather cavalier about not reaping snstore processes.
 * When not shutting down, reap() will wait for any.
 */

void quit (void)
{
   if (nstores)
      do
         close(stores[--nstores]);
      while (nstores);
   if (nsocks)
      do
         close(socks[--nsocks].sd);
      while (nsocks);
}
