/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * News reader daemon, NNTP protocol driver.
 */

#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h> /* inet_ntoa() */
#include <sys/socket.h> /* getpeername */
#include <dirent.h>
#include "config.h"
#include "art.h"
#include "dhash.h"
#include "group.h"
#include "hostname.h"
#include "parameters.h"
#include "args.h"
#include "snntpd.h"
#include "valid.h"
#include "key.h"
#include "path.h"
#include <readln.h>
#include <out.h>
#include <openf.h>
#include <opt.h>
#include <format.h>
#include <tokensep.h>
#include <wildmat.h>

int debug = 0;

static const char ver_ctrl_id[] = "$Id: snntpd.c 36 2004-04-27 16:52:02Z patrik $";

bool posting_ok = FALSE;
char *client_ip;
char *me;
struct readln input;

int fifo = -1;

extern char *currentgroup;
extern int currentserial;

extern void do_article (void);
extern void do_body (void);
extern void do_group (void);
extern void do_head (void);
extern void do_help (void);
extern void do_ihave (void);
extern void do_last (void);
extern void do_list (void);
extern void do_listgroup (void);
extern void do_mode (void);
extern void do_newgroups (void);
extern void do_newnews (void);
extern void do_next (void);
extern void do_post (void);
extern void do_quit (void);
extern void do_sendme (void);
extern void do_slave (void);
extern void do_stat (void);
extern void do_xhdr (void);
extern void do_xover (void);
extern void do_xpat (void);

struct cmd {
   char *name;
   void (*function) (void);
   char needs_args;      /* Min. number of args required */
   char needs_group;     /* Needs a current group set */
   char needs_article;   /* Needs a current article set */
   char needs_grouplist;
};

#define S(func) #func
#define DEF(func,args,group,art,gl) \
  { S(func), do_ ## func, args, group, art, gl},

static struct cmd cmds[] = {
   DEF(article, 1, 0, 0, 0)
   DEF(body, 1, 0, 0, 0)
   DEF(group, 2, 0, 0, 1)
   DEF(head, 1, 0, 0, 0)
   DEF(help, 0, 0, 0, 0)
   DEF(ihave, 1, 0, 0, 0)
   DEF(last, 1, 1, 1, 0)
   DEF(list, 1, 0, 0, 1)
   DEF(listgroup, 1, 0, 0, 1)
   DEF(mode, 2, 0, 0, 0)
   DEF(newgroups, 3, 0, 0, 1)
   DEF(newnews, 4, 0, 0, 1)
   DEF(next, 1, 1, 1, 0)
   DEF(post, 1, 0, 0, 0)
   DEF(quit, 1, 0, 0, 0)
   DEF(sendme, 0, 0, 0, 0)
   DEF(slave, 0, 0, 0, 0)
   DEF(stat, 1, 0, 0, 0)
   DEF(xhdr, 2, 1, 0, 0)
   DEF(xover, 1, 1, 0, 0)
   DEF(xpat, 4, 1, 0, 0)
};

#undef S
#undef DEF

static void cleanup (void)
{
   if (debug >= 3)
   {
      int hit, miss;

      art_filecachestat(&hit, &miss);
      LOG("do_quit:cache requests:%dT=%dH+%dM", hit + miss, hit, miss);
   }
   dh_close();
   group_fin();
   if (nr_keys)
      key_free();
}

void do_quit (void)
{
   args_write(1, "205 bye\r\n");
   cleanup();
   _exit(0);
}

/* Not quite safe to do this... */

static bool checkservice = FALSE;

static void handler (int signum)
{
   if (SIGHUP == signum)
   {
      checkservice = TRUE;
      return;
   }
   dh_close();
   group_fin();
   LOG("Caught signal %d, exiting", signum);
   _exit(3);
}

static void openfifo (void)
{
   struct stat st;

   if (fifo > -1)
      return;
   fifo = open(".fifo", O_RDWR | O_NONBLOCK);
   if (-1 == fifo)
   {
      LOG("open(fifo):%m(ok)");
      return;
   }
   if (-1 == fstat(fifo, &st))
      FAIL(2, "openfifo:stat(.fifo):%m");
   if (!S_ISFIFO(st.st_mode))
   {
      close(fifo);
      fifo = -1;
      LOG1(".fifo is not a fifo, ok");
   }
}

static void docheckservice (void)
{
   struct stat st;
   char *reason = "maintenance";
   char buf[256];
   char *p;

   if (0 == stat(".noservice", &st))
   {
      if (st.st_size)
         if (-1 != topline(NULL, ".noservice", buf, sizeof (buf)))
            reason = buf;
      args_write(1, "400 Service going down: %s\r\n", reason);
      cleanup();
      _exit(0);
   }
   if (0 == stat(".nopost", &st))
      posting_ok = FALSE;
   else if ((p = getenv("POSTING_OK")))
      posting_ok = TRUE;
   else
      posting_ok = FALSE;
   openfifo();
   checkservice = FALSE;
}

static void init (void)
{
   static struct {
      int (*f) ();
      char buf[40];
   } con[] = { { getpeername, "TCPREMOTEIP" }, { getsockname, "TCPLOCALIP" } };
   struct sigaction sa;
   struct sockaddr_in sin;
   int i, len;

   if (-1 == chdir(snroot))
      FAIL(2, "chdir(%s):%m", snroot);
   (void) dh_open(NULL, FALSE);
   if (-1 == group_init())
   {
      dh_close();
      _exit(2);
   }
   for (i = 0; len = sizeof (sin), i < 2; i++)
      if (!getenv(con[i].buf))
         if (0 == (*(con[i].f)) (0, (struct sockaddr *) &sin, &len))
            putenv(strcat(strcat(con[i].buf, "="), inet_ntoa(sin.sin_addr)));
   client_ip = con->buf + sizeof ("TCPREMOTEIP") - 1;
   if ('=' == *client_ip)
      client_ip++;
   me = myname();
   sa.sa_handler = handler;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = SA_RESTART;
   sigaction(SIGHUP, &sa, NULL);
   sa.sa_handler = SIG_IGN;
   sigaction(SIGPIPE, &sa, NULL);

   docheckservice();
}

static int getallgroups (void)
{
   DIR *dir;
   struct dirent *dp;
   char *gr;

   if (!(dir = opendir(".")))
   {
      LOG("getallgroups:opendir(%s):%m", snroot);
      return -1;
   }
   for (nr_keys = 0; (dp = readdir(dir)); nr_keys++)
      if (is_valid_group(gr = dp->d_name))
         if (-1 == key_add(&gr, strlen(gr)))
            fail(2, "No memory");
   closedir(dir);
   return 0;
}

static void usage (void) { fail(1, "Usage:%s [-S] [-t timeout]", progname); }

int main (int argc, char **argv)
{
   int i;
   char *cp;
   bool greeting;
   int tmo = 600;

   progname = ((cp = strrchr(argv[0], '/')) ? cp + 1 : argv[0]);

   parameters(TRUE);

   greeting = TRUE;
   while ((i = opt_get(argc, argv, "t")) > -1)
      switch (i)
      {
         case 'P': log_with_pid(); break;
         case 'd': debug++; break;
         case 'V': version(); _exit(0);
         case 'S': greeting = FALSE; break;
         case 't':
            if (!opt_arg)
               fail(1, "Need value for \"t\"");
            if ((tmo = strtoul(opt_arg, &cp, 10)) < 0 || *cp)
               fail(1, "Timeout must be positive");
            break;
         default: usage();
      }

   if (opt_ind < argc)
   {
      int pid, lg[2];

      if (-1 == (pid = pipefork(lg)))
         fail(2, "pipe/fork:%m");
      if (0 == pid)
      {
         if (-1 == dup2(lg[0], 0))
            fail(2, "dup:%m");
         dup2(1, 2);
         close(lg[1]);
         execvp(argv[opt_ind], argv + opt_ind);
         LOG("exec(%s):%m", argv[opt_ind]);
         _exit(2);
      }
      close(lg[0]);
      if (-1 == dup2(lg[1], 2))
         fail(2, "dup:%m");
   }

   if (-1 == readln_ready(0, tmo, &input))
      FAIL(2, "readln_ready:%m");

   init();
   set_path_var();
   
   if (greeting)
   {
      args[1] = "READER";
      do_mode();
   }
   
   /* Main loop */

   while (1)
   {
      int nr;

      switch ((nr = args_read(&input)))
      {
         case 0:
            args_write(1, "501 Bad command\r\n");
            continue;
         case -1:
            do_quit(); /* Does not return */
      }
      if (checkservice)
         docheckservice(); /* May not return */
      for (i = 0; i < sizeof (cmds) / sizeof (struct cmd); i++)
      {
         if (strcasecmp(args[0], cmds[i].name))
            continue;
         if (nr < cmds[i].needs_args)
            args_write(1, "501 Bad syntax\r\n");
         else if (cmds[i].needs_group && !currentgroup)
            args_write(1, "412 No group selected\r\n");
         else if (cmds[i].needs_article && -1 == currentserial)
            args_write(1, "420 No article selected\r\n");
         else if (!nr_keys && cmds[i].needs_grouplist && -1 == getallgroups())
            args_write(1, "503 No memory\r\n");
         else
            (*cmds[i].function) ();
         break;
      }
      if (i >= sizeof (cmds) / sizeof (struct cmd))
         args_write(1, "500 unimplemented\r\n");
   }
   /* Not Reached */
}

/*
 * Functions used in various places
 */

int alltolower (char *buf)
{
   char *cp;

   for (cp = buf; *cp; cp++)
      if (isupper(*cp))
         *cp = tolower(*cp);
   return (cp - buf);
}

int topline (char *group, char *fn, char *buf, int size)
{
   int fd, i;
   char *cp;

   if (NULL == group)
      fd = open(fn, O_RDONLY);
   else
      fd = openf(0, O_RDONLY, "%s/%s", group, fn);
   if (-1 == fd)
      return -1;
   i = read(fd, buf, size - 1);
   close(fd);
   if (i <= 0)
      return i;
   buf[i] = '\0';
   if ((cp = strchr(buf, '\n')))
   {
      *cp = '\0';
      return (cp - buf);
   }
   return i;
}

char *currentgroup = NULL;
int currentserial = -1; /* this may be set by others */
struct group currentinfo = { 0, };

/*
 * Caller must downcase argment
 */

int make_current (char *group)
{
   if (!currentgroup)
   {
      if (!(currentgroup = malloc(GROUPNAMELEN + 1)))
      {
         LOG("make_current:no memory");
         return -1;
      }
      strcpy(currentgroup, "no.group.selected.yet");   /* Better than random crap... */
   }
   /* prevent =junk from being selected */
   if (!key_exists(group, strlen(group)))
      return -1;
   if (-1 == group_info(group, &currentinfo))
      return -1;
   strcpy(currentgroup, group);
   currentserial = -1;
   return 0;
}

int pipefork (int p[2])
{
   if (0 == pipe(p))
   {
      int pid;

      if ((pid = fork()) > -1)
         return pid;
      close(p[0]);
   }
   return -1;
}
