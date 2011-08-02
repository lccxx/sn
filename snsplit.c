/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Run a program on each article in an article stream.
 * We could use a pipe instead of tmp file, but user might want to
 * seek on it.  Anyway profiling indicates the pipe method not to be
 * significantly faster.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "config.h"
#include "unfold.h"
#include <opt.h>
#include <readln.h>
#include <out.h>
#include <format.h>

static const char ver_ctrl_id[] = "$Id: snsplit.c 36 2004-04-27 16:52:02Z patrik $";

static void nomem (void) { fail(2, "no memory"); }

int debug;
struct readln input = { 0, };
static int tmpfd;

#define SEQUENCE "SEQUENCE=000000"
#define MAXSEQ 999999

static char seqbuf[sizeof (SEQUENCE)];
static char bytebuf[40];
static char hlbuf[50];
static char blbuf[50];
static int head_lines;
static int body_lines;

static struct export {
   char *s;
   int len;
   char *name;
} *exports = NULL;

static int nrexports;

static char **env = NULL;
static int envstart, envused, envend;
static int article_bytes;

void clear_env (void)
{
   int i;

   for (i = envstart; i < envend; i++)
      if (env[i])
      {
         free(env[i]);
         env[i] = NULL;
      }
   for (i = 0; i < nrexports; i++)
      exports[i].s = 0; /* just freed */
   env[envused = envstart] = NULL;
}

void put_env (int index, char *value)
{
   struct export *ep;
   int len;
   char *p;
   char *q;

   ep = exports + index;
   ep->s = malloc(len = ep->len + 4 + 1 + strlen(value) + 1);
   if (!ep->s)
      nomem();
   strcpy(ep->s, "FLD_");
   for (p = ep->s + 4, q = ep->name; *q; q++, p++)
      if (*q >= 'a' && *q <= 'z')
         *p = *q - ('a' - 'A');
      else if ('-' == *q)
         *p = '_';
   *p++ = '=';
   strcpy(p, value);
   env[envused++] = ep->s;
}

extern char **environ; /* the real thing */

void setup_env (char **headers)
{
   int i, nr;
   int seq;

   for (nr = 0; environ[nr]; nr++) ;
   seq = 0;
   for (i = 0; environ[i];)
      if (!seq && 0 == strncmp(environ[i], "SEQUENCE=", 9))
      {
         seq = atoi(environ[i] + 9);
         environ[i] = environ[--nr];
         environ[nr] = 0;
      }
      else if (0 == strncmp(environ[i], "FLD_", 4)
               || 0 == strncmp(environ[i], "BYTES=", 6)
               || 0 == strncmp(environ[i], "HEAD_LINES=", 11)
               || 0 == strncmp(environ[i], "BODY_LINES=", 11))
      {
         environ[i] = environ[--nr];
         environ[nr] = 0;
      }
      else
         i++;
   if (!(env = malloc((i + 6) * sizeof (char *))))
      nomem();
   for (nr = 0; nr < i; nr++)
      env[i] = environ[i];
   env[nr++] = strcpy(seqbuf, SEQUENCE);
   if (seq > 0 && seq < MAXSEQ)
   {
      char *p;

      seq++;
      for (p = seqbuf + sizeof (SEQUENCE) - 2; seq > 0; p--, seq /= 10)
         *p += seq % 10;
   }
   env[nr++] = bytebuf;
   env[nr++] = hlbuf;
   env[nr++] = blbuf;
   env[nr] = 0;
   envused = envstart = nr;
   envend = nr + nrexports;
   if (!headers)
      return;

   exports = malloc(nrexports * sizeof (struct export));
   if (!exports)
      nomem();
   for (i = 0; i < nrexports; i++)
   {
      exports[i].s = 0;
      exports[i].len = strlen(exports[i].name = headers[i]);
   }
}

static void writetmp (char *buf, int len)
{
   if (-1 == write(tmpfd, buf, len))
      fail(2, "can't write to tmp file:%m");
   article_bytes += len;
}

int write_file (char *line, int len)
{
   int i;

   writetmp(line, len);
   writetmp("\n", 1);
   head_lines++;

   for (i = 0; i < nrexports; i++)
   {
      int n;

      n = exports[i].len;
      if (!exports[i].s && len >= n && ':' == line[n])
         if (0 == strncasecmp(line, exports[i].name, n))
         {
            char *p;

            for (p = line + n + 1; ' ' == *p; p++) ;
            put_env(i, p);
            break;
         }
   }
   return 0;
}

static void shortread (void) { fail(3, "unexpected EOF"); }
static void badread (void) { fail(2, "can't read article:%m"); }
static void badfmt (void) { fail(3, "bad rnews line"); }

int read_rnews (void)
{
   char *line;
   int len, c, bytes;

   len = readln(&input, &line, '\n');
   if (0 == len)
      return 0;
   if (-1 == len) badread();
   line[--len] = '\0';
   if (len > 0)
      if ('\r' == line[len - 1])
         line[--len] = '\0';
   if ('#' != *line++) badfmt();
   if ('!' != *line++) badfmt();
   while (' ' == *line)
      line++;
   if (strncmp(line, "rnews ", 6)) badfmt();
   for (line += 6; ' ' == *line; line++) ;
   for (bytes = 0; (c = *line); line++)
      if (c < '0' || c > '9') badfmt();
      else
      {
         bytes *= 10;
         bytes += c - '0';
      }

   len = unfold(&input, write_file);
   if (0 == len) shortread();
   if (len < 0) badread();
   bytes -= len;
   writetmp("\n", 1);

   do
   {
      if (-1 == (len = readln(&input, &line, '\n'))) badread();
      if (0 == len) shortread();
      if ((bytes -= len) < 0) shortread();
      if (--len > 0)
         if ('\r' == line[len - 1])
            --len;
      if (len)
         writetmp(line, len);
      writetmp("\n", 1);
      body_lines++;
   }
   while (bytes > 0);
   return 1;
}

int read_wire (void)
{
   char *line;
   int len;

   switch (unfold(&input, write_file))
   {
      case 0:
         return 0;
      case -1:
         badread();
      case -2:
         badfmt();
   }
   writetmp("\n", 1);
   while ((len = readln(&input, &line, '\n')) > 0)
   {
      if (--len > 0)
         if ('\r' == line[len - 1])
            --len;
      if ('.' == *line)
      {
         if (0 == --len)
            return 1;
         line++;
      }
      if (len > 0)
         writetmp(line, len);
      writetmp("\n", 1);
      body_lines++;
   }
   if (0 == len)
      shortread();
   badread();
   /* Not Reached */
   return 0;
}

void usage (void) { fail(1, "Usage: %s [-r] [header ... -] prog ...", progname); }

static void incseq (void)
{
   int i;

   for (i = sizeof (SEQUENCE) - 2; i > sizeof ("SEQUENCE=") - 2; i--)
   {
      seqbuf[i]++;
      if (seqbuf[i] <= '9')
         break;
      seqbuf[i] = '0';
   }
}

int main (int argc, char **argv)
{
   char buf[80];
   char **xv;
   char **hv;
   char *p;
   int i, pid, s;
   int (*reader) (void);

   progname = ((p = strrchr(argv[0], '/')) ? p + 1 : argv[0]);

   reader = read_wire;
   while ((i = opt_get(argc, argv, "")) > -1)
      switch (i)
      {
         case 'r': reader = read_rnews; break;
         case 'd': debug++; break;
         case 'V': version(); _exit(0);
         default: usage();
      }

   if (opt_ind >= argc)
      usage();

   hv = argv + opt_ind;
   for (i = opt_ind; i < argc; i++)
      if ('-' == *argv[i] && !argv[i][1])
         break;
   if (i < argc)
   {
      argv[i] = NULL;
      xv = argv + i + 1;
   }
   else
   {
      xv = hv;
      hv = NULL;
   }
   if (!*xv)
      usage();

   if (readln_ready(0, 0, &input)) nomem();

   formats(buf, sizeof (buf) - 1, "/tmp/.%s.%d", progname, getpid());
   tmpfd = open(buf, O_RDWR | O_CREAT | O_EXCL, 0644);
   if (-1 == tmpfd)
      fail(2, "open(%s):%m", buf);
   unlink(buf);

   if (hv)
      nrexports = i - opt_ind;
   else
      nrexports = 0;
   setup_env(hv);

   for (;; incseq())
   {
      article_bytes = head_lines = body_lines = 0;
      if (!reader())
         break;
      formats(hlbuf, sizeof (hlbuf) - 1, "HEAD_LINES=%d", head_lines);
      formats(blbuf, sizeof (blbuf) - 1, "BODY_LINES=%d", body_lines);
      formats(bytebuf, sizeof (bytebuf) - 1, "BYTES=%d", article_bytes);
      pid = fork();
      if (-1 == pid)
         fail(2, "fork:%m");
      if (0 == pid)
      {
         lseek(tmpfd, 0, SEEK_SET);
         dup2(tmpfd, 0);
         environ = env;
         execvp(*xv, xv);
         fail(1, "exec(%s):%m", *xv);
      }

      while (-1 == waitpid(pid, &s, 0) && EINTR == errno) ;
      if (WIFEXITED(s))
      {
         if ((s = WEXITSTATUS(s)))
            fail(s, "%s exited with %d", *xv, s);
      }
      else if (WIFSIGNALED(s))
      {
         s = WTERMSIG(s);
         fail(128 + s, "%s caught signal %d", *xv, s);
      }
      else
         fail(128, "%s unknown wait status %d", *xv, s);

      lseek(tmpfd, 0, SEEK_SET);
      ftruncate(tmpfd, 0);
      if (hv)
         clear_env();
   }
   _exit(0);
}
