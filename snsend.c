/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Send out or store articles in an article stream.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/param.h>
#include "art.h"
#include "unfold.h"
#include "hostname.h"
#include "group.h"
#include "store.h"
#include "times.h"
#include "config.h"
#include "addr.h"
#include "dhash.h"
#include "field.h"
#include "parameters.h"
#include <opt.h>
#include <readln.h>
#include <tokensep.h>
#include <format.h>
#include <out.h>
#include <b.h>

static const char ver_ctrl_id[] = "$Id: snsend.c 58 2004-07-28 18:56:20Z patrik $";

static char path1[MAXPATHLEN + 1];
static char path2[MAXPATHLEN + 1];

typedef enum { NG_UNKNOWN, NG_LOCAL, NG_SPECIAL, NG_SPECIALX, NG_GLOBAL, NG_FIFO } ng_t;

static struct ng {
   char *s;
   ng_t t;
} *newsgroups;

static struct readln input = { 0, };
static struct article art = { 0, };

static void nomem (void) { fail(2, "No memory"); }

static char *me;
static int melen;
static int nrgroups;
static char *messageid;
static bool check_exist = FALSE;
static int pid;
static struct timeval now;
static bool canalias = TRUE;
static bool report = FALSE;
int debug = 0;

#define B_APPENDL(b,str) b_appendl((b), (str), strlen(str))

static ng_t newsgroup_type (char *group)
{
   struct stat st;

   formats(path1, sizeof (path1) - 1, "%s/.outgoing", group);
   if (0 == stat(path1, &st))
   {
      if (S_ISDIR(st.st_mode))
         return NG_GLOBAL;
      if (S_ISREG(st.st_mode))
      {
         if (S_IXUSR & st.st_mode)
            return NG_SPECIALX;
         else
            return NG_SPECIAL;
      }
      if (S_ISFIFO(st.st_mode))
         return NG_FIFO;
      return NG_UNKNOWN;
   }
   if (ENOTDIR == errno)
      return NG_UNKNOWN;
   if (0 == stat(group, &st) && S_ISDIR(st.st_mode))
      return NG_LOCAL;
   return NG_UNKNOWN;
}

/* -----------------------------------------------------
 * "Methods" to send/store an article.
 */

static int writeart (int fd)
{
   if (write(fd, art.head, art.hlen) > -1)
      if (write(fd, "\r\n", 2) > -1)
         if (art.body && write(fd, art.body, art.blen) > -1)
            if (write(fd, ".\r\n", 3) > -1)
               return 0;
   return -1;
}

static int store_global (char *group)
{
   struct stat st;
   int fd, er;

   formats(path1, sizeof (path1) - 1, "%s/.outgoing/$%u.%u.%u",
           group, now.tv_sec, now.tv_usec, pid);
   if (0 == stat(path1, &st))
      return 0;
   *strrchr(path1, '$') = '+';
   fd = open(path1, O_WRONLY | O_CREAT | O_EXCL, 0644);
   if (-1 == fd)
   {
      if (EEXIST == errno)
      {
         LOG("store_global:tmp file %s exists", path1);
         return 0; /* darn unlikely */
      }
      else
         fail(2, "store_global:open(%s):%m", path1);
   }
   er = writeart(fd);
   close(fd);
   if (0 == er)
   {
      strcpy(path2, path1);
      *strrchr(path2, '+') = '$';
      if (-1 == link(path1, path2) && EEXIST != errno)
      {
         er = -1;
         LOG("store_global:link(%s,%s):%m", path1, path2);
      }
   }
   else
      LOG("store_global:write(%s):%m", path1);

   (void) unlink(path1);
   if (er)
      _exit(2);
   return 0;
}

static int store_local (char *group)
{
   struct data d;

   d.messageid = messageid;
   if (group)
      if (check_exist)
         if (0 == dh_find(&d, FALSE))
            if (0 == strcasecmp(group, d.newsgroup))
            {
               LOG1("store_local:<%s> already exists in %s, not storing", messageid, group);
               return 1;
            }
   d.serial = sto_add(group ? group : JUNK_GROUP, &art);
   if (-1 == d.serial)
   {
      if (group)
         _exit(2); /* FIXME: safe to return -1 here? */
      else
         return -1; /* =junk */
   }
   times_append(group ? group : JUNK_GROUP, d.serial);
   if ((d.newsgroup = group))
   {
      if (d.serial < 0)
         _exit(2);
      if (-1 == dh_insert(&d))
      {
         if (EEXIST == errno)
         {
            LOG1("store_local:oops, stored <%s> but it already exists", messageid);
         }
         else
            LOG("store_local:can't insert in db for <%s> in %s:%d, %m?",
                messageid, group, d.serial);
      }
   }
   if (report)
      writef(1, "%s %u <%s>\n", group ? group : JUNK_GROUP, d.serial, messageid);
   return (group ? d.serial : 0);
}

static char *av[] = { "sh", 0, 0 };

static int special (char *group, int argv0)
{
   int pid, p[2], er, s;

   formats(path1, sizeof (path1) - 1, "./%s/.outgoing", group);
   av[1] = path1;
   if (pipe(p))
      fail(2, "special:pipe:%m");
   if (-1 == (pid = fork()))
      fail(2, "special:fork:%m");
   if (0 == pid)
   {
      formats(path2, sizeof (path2) - 1, "NEWSGROUP=%s", group);
      if (putenv(path2))
         fail(2, "putenv:%m?");
      close(p[1]);
      dup2(p[0], 0);
      execvp(av[argv0], av + argv0);
      fail(2, "exec(%s):%m", av[argv0]);
   }
   close(p[0]);
   er = writeart(p[1]);
   close(p[1]);
   while (-1 == waitpid(pid, &s, 0) && EINTR == errno) ;
   if (WIFEXITED(s))
   {
      if (0 == WEXITSTATUS(s))
         return 0;
      s = WEXITSTATUS(s);
      fail(s, "%s/.outgoing exited with %d", s, s);
   }
   if (WIFSIGNALED(s))
      fail(WTERMSIG(s) + 128, "%s/.outgoing caught signal %d", WTERMSIG(s));
   fail(9, "%s/.outgoing unknown wait status %d", group, s);
   _exit(1); /* dumb compiler */
}

static int store_special (char *group) { return special(group, 0); }
static int store_specialx (char *group) { return special(group, 1); }

static int store_fifo (char *group)
{
   int fd;

   formats(path1, sizeof (path1) - 1, "%s/.outgoing", group);
   fd = open(path1, O_WRONLY | O_NONBLOCK);
   if (-1 == fd)
      fail(2, "store_fifo:open(%s):%m", path1);
   if (-1 == fcntl(fd, F_SETFL, O_WRONLY))
      fail(2, "store_fifo:fcntl(%s):%m", path1);
   lockf(fd, F_LOCK, 0);
   if (-1 == writeart(fd))
      fail(2, "store_fifo:write(%s):%m", path1);
   close(fd);
   return 0;
}

#define for_each_newsgroup(n) \
   for (n = 0; n < nrgroups; n++) \
      if (!newsgroups[n].s) ; else

static int localstore (void)
{
   int i, serial, done;

   done = 0;
   for_each_newsgroup(i)
      if (NG_UNKNOWN != newsgroups[i].t)
      {
         serial = store_local(newsgroups[i].s);
         if (1 == serial)
            return 1; /* short circuit dups */
         if (canalias && art.body)
         {
            art.body = 0;
            art.blen = 0;
            art.hlen = formats(art.head, art.hlen, "Message-ID: %s:%u<%s>\r\n",
                               newsgroups[i].s, serial, messageid);
         }
         newsgroups[i].s = 0;
         done++;
      }
   return done;
}

/* -----------------------------------------------------
 * snsend here.  Order: do globals first because they are the easiest
 * for admin to undo.  Locals are last because we are lazy and want
 * to overwrite the article with it's alias.
 */

static void junk_article (void)
{
   static int have_junk_group = -1;

   LOG("article <%s> not for any newsgroup", messageid);
   if (0 == have_junk_group)
      return;
   if (store_local(0) < 0)
   {
      if (debug >= 1)
      {
         LOG(">>>");
         writeart(2);
         LOG("<<<");
      }
      have_junk_group = 0;
   }
   else
      have_junk_group = 1;
}

static void snsend (void)
{
   int i, serial, done;

   done = 0;
   for_each_newsgroup(i)
   {
      switch ((newsgroups[i].t = newsgroup_type(newsgroups[i].s)))
      {
         default:
         case NG_UNKNOWN:
         case NG_LOCAL:
            continue;
         case NG_GLOBAL:
            serial = store_global(newsgroups[i].s);
            break;
         case NG_SPECIAL:
            serial = store_special(newsgroups[i].s);
            break;
         case NG_SPECIALX:
            serial = store_specialx(newsgroups[i].s);
            break;
         case NG_FIFO:
            serial = store_fifo(newsgroups[i].s);
            break;
      }
      if (report)
         writef(1, "%s %u <%s>\n", newsgroups[i].s, serial, messageid);
      newsgroups[i].s = 0;
      done++;
   }

   done += localstore();
   if (!done)
      junk_article();
}

/* -----------------------------------------------------
 * snstore here.  Ignore newsgroup type, just store it regardless.
 * We won't be checking .nopost like we did in previous versions,
 * because we don't have -1 option anymore.
 */

static void snstore (void)
{
   int i;

   for_each_newsgroup(i) newsgroups[i].t = newsgroup_type(newsgroups[i].s);
   if (0 == localstore())
      junk_article();
}

/* -----------------------------------------------------
 */

static void store___NOT (void)
{
   if (-1 == writeart(1))
      fail(2, "write:%m");
}

/* -----------------------------------------------------
 */

static void usage (void)
{
   fail(1, "Usage: %s [-rcnva]\n"
        "-r input is in rnews batch format\n"
        "-c when storing to a local newsgroup, check first if it exists\n"
        "-n do nothing; echo articles to descriptor 1\n"
        "-v for each article, write to descriptor 1 where it went\n"
        "-a do not alias crossposts\n", progname);
}

/* -----------------------------------------------------
 * Main program.
 */

int main (int argc, char **argv)
{
   bool read_article(bool);
   int i;
   bool rnews;
   void (*dispatcher) (void);
   char *p;

   progname = ((p = strrchr(argv[0], '/')) ? p + 1 : argv[0]);

   if (0 == strcmp(progname, "snsend"))
      dispatcher = snsend;
   else if (0 == strcmp(progname, "snstore"))
      dispatcher = snstore;
   else
   {
      fail(1, "Eh?");
      _exit(1);
   }
   rnews = FALSE;
   canalias = TRUE;

   while ((i = opt_get(argc, argv, "")) > -1)
      switch (i)
      {
         case 'd': debug++; break;
         case 'V': version(); _exit(0);
         case 'P': log_with_pid(); break;
         case 'r': rnews = TRUE; break;
         case 'v': report = TRUE; break;
         case 'c': check_exist = TRUE; break;
         case 'n': dispatcher = store___NOT; break;
         case 'a': canalias = FALSE; break;
         default: usage();
      }
   if (opt_ind < argc)
      usage();

   parameters(TRUE);
   if (chdir(snroot))
      fail(2, "chdir(%s):%m", snroot);
   me = myname();
   melen = strlen(me);
   pid = getpid();

   if (readln_ready(0, 0, &input))
      fail(2, "no memory");
   if (-1 == group_init())
      _exit(2);
   if (-1 == sto_init())
      fail(2, "can't initialize store:%m?");
   if (-1 == times_init())
      fail(2, "can't initialize times:%m");
   (void) dh_open(NULL, FALSE);

   for (gettimeofday(&now, 0); read_article(rnews); gettimeofday(&now, 0))
      dispatcher();
   _exit(0);
}

/* -----------------------------------------------------
 * Article reading and fixing-up.
 */

/* symbols private to us */

static struct b msgid = { 0, };
static struct b path = { 0, };
static struct b body = { 0, };
static struct b head = { 0, };
static struct b htmp = { 0, };
static int body_lines;
static bool have_date;
static bool have_newsgroups;
static char *rline;
static int rlen;

static void badread (void) { fail(2, "can't read article:%m"); }
static void eof (void) { fail(3, "unexpected end of file"); }
static void badrnews (void) { fail(3, "bad rnews line"); }

static bool first_call;

static void init (void)
{
   messageid = 0;
   first_call = TRUE;
   body_lines = body.used = head.used = htmp.used = msgid.used = path.used = 0;
   have_date = have_newsgroups = FALSE;
}

static int getline (void)
{
   int len;

   len = rlen = readln(&input, &rline, '\n');
   if (0 == rlen)
      return 0;
   if (rlen < 0) badread();
   rline[--rlen] = '\0';
   if (rlen > 0)
      if ('\r' == rline[rlen - 1])
         rline[--rlen] = '\0';
   return len;
}

static void appendbody (void)
{
   if (b_appendl(&body, rline, rlen) || B_APPENDL(&body, "\r\n")) nomem();
}

/* Sets newsgroups[]; valid until next call */

static void parsenewsgroups (char *ng)
{
   static int size = 0;
   static struct b ngbuf = { 0, };
   char *p;

   if (0 == size)
      if (!(newsgroups = malloc((size = 10) * sizeof (struct ng))))
         nomem();
   while (' ' == *ng)
      ng++;

   ngbuf.used = 0;
   if (B_APPENDL(&ngbuf, ng)) nomem();
   ng = ngbuf.buf;

   for (nrgroups = 0; (p = tokensep(&ng, ",")); nrgroups++)
   {
      while (' ' == *p)
         p++;
      if (!*p)
         continue;
      if (nrgroups + 1 >= size)
      {
         struct ng *tmp;
         if (!(tmp = malloc(size * 2 * sizeof (struct ng)))) nomem();
         memcpy(tmp, newsgroups, size * sizeof (struct ng));
         free(newsgroups);
         newsgroups = tmp;
         size *= 2;
      }
      while (p[strlen(p) - 1] == ' ') /* Strip trailing spaces */
         p[strlen(p) - 1] = '\0';
      newsgroups[nrgroups].s = p;
      newsgroups[nrgroups].t = NG_UNKNOWN;
   }
}

/*
 * Called by unfold() (which is called by read_*()) to hand us
 * an unfolded line.
 */

#define HEADER(lit) \
   if (hlen == sizeof (lit) - 1 && \
       0 == strncasecmp(line, lit, sizeof (lit) - 1) && \
       ':' == line[sizeof (lit) - 1])

#define MAXREFLEN 500

static int append (char *line, int len)
{
   char *p;
   int c, hlen;

   if (first_call)
   {
      first_call = FALSE;
      if (0 == strncmp(line, "From ", 5))
         return 0;
   }
   hlen = check_field(line, len);
   if (!hlen)
      goto badline;

   do
   {
      HEADER("References")
      {
         if (len > MAXREFLEN)
         {
            p = line + len - MAXREFLEN;
            c = MAXREFLEN;
            while (*p && ' ' != *p)
            {
               p++;
               c--;
            }
            if (!*p) break;
            if (B_APPENDL(&htmp, "References: ")) nomem();
            if (b_appendl(&htmp, p + 1, c - 1)) nomem();
            if (B_APPENDL(&htmp, "\r\n")) nomem();
            return 0;
         }
         break;
      }
      HEADER("Path")
      {
         if (path.used)
            return 0;
         for (line += hlen + 1, len -= hlen + 1; ' ' == *line; line++, len--) ;
         if (b_appendl(&path, line, len)) nomem();
         return 0;
      }
      HEADER("Newsgroups")
      {
         if (have_newsgroups) break; /* XXX ignore or fail? */
         if (b_appendl(&htmp, line, len)) nomem();
         if (B_APPENDL(&htmp, "\r\n")) nomem();
         have_newsgroups = TRUE;
         parsenewsgroups(line + hlen + 1);
         return 0;
      }
      HEADER("X-sn-Newsgroups")
      {
         parsenewsgroups(line + hlen + 1);
         return 0;
      }
      HEADER("Message-ID")
      {
         int len;

         if (msgid.used) break; /* XXX */
         for (p = line + hlen + 1; ' ' == *p; p++) ;
         if ('<' != *p || (len = addr_msgid(p)) <= 0)
         {
            LOG("append: will replace bad Message-ID \"%s\"", p);
            return 0;
         }
         if (b_appendl(&msgid, p + 1, len - 2)) nomem();
         break;
      }
      if (0 == strncasecmp(line, "X-sn-", 5))
         return 0;
      HEADER("Bytes") return 0;
      HEADER("Lines") return 0;
      HEADER("Xref") return 0;
      HEADER("Date") have_date = TRUE;
   }
   while (0);
   if (0)
badline:
      LOG("append:bad header \"%s\"", line);
   if (b_appendl(&htmp, line, len)) nomem();
   if (B_APPENDL(&htmp, "\r\n")) nomem();
   return 0;
}

static int read_nntp (void)
{
   int consumed;

   consumed = unfold(&input, append);
   if (0 == consumed)
      return 0;
   if (consumed < 0) badread();

   for (body_lines = 0;;)
   {
      int len;

      if (0 == (len = getline())) eof();
      consumed += len;
      if (1 == rlen)
         if ('.' == *rline)
            break;
      body_lines++;
      appendbody();
   }
   return consumed;
}

static int read_rnews (void)
{
   int bytes, c, consumed;

   if (0 == getline())
      return 0;

   if ('#' != *rline++) badrnews();
   if ('!' != *rline++) badrnews();
   while (' ' == *rline)
      rline++;
   if (strncmp(rline, "rnews ", 6)) badrnews();
   for (rline += 6; ' ' == *rline; rline++) ;
   for (bytes = 0; (c = *rline); rline++)
   {
      if (c < '0' || c > '9') badrnews();
      bytes *= 10;
      bytes += (c - '0');
   }

   consumed = unfold(&input, append);
   if (0 == consumed) eof();
   if (consumed < 0) badread();

   for (body_lines = 0; consumed < bytes; )
   {
      if (0 == (c = getline())) eof();
      consumed += c;
      if (consumed > bytes)
         fail(3, "rnews input overshot");
      body_lines++;
      if ('.' == *rline)
         if (B_APPENDL(&body, ".")) nomem();
      appendbody();
   }
   return consumed;
}

static void appendhead (char *line, int len)
{
   if (b_appendl(&head, line, len)) nomem();
}

#define APPENDHEAD(str) appendhead((str), strlen(str))

#define PUT2(i,sep) c = i; *p-- = c%10 + '0'; *p-- = c/10 + '0'; *p-- = sep;

bool read_article (bool rnews)
{
   char buf[80];
   int c, i;

   init();
   if (rnews)
      c = read_rnews();
   else
      c = read_nntp();
   if (0 == c)
      return FALSE; /* no more */

   /* fix the headers.  We don't care if there were newsgroups. */
   /* Path: */
   APPENDHEAD("Path: ");
   appendhead(me, melen);
   if (path.used)
   {
      APPENDHEAD("!");
      appendhead(path.buf, path.used);
   }
   else
      LOG3("created Path header...");
   APPENDHEAD("\r\n");

   appendhead(htmp.buf, htmp.used);

   /* Message-ID */
   if (!msgid.used)
   {
      c = formats(buf, sizeof (buf) - 1, "%x.%x.%x@%s", pid, now.tv_sec, now.tv_usec, me);
      if (b_appendl(&msgid, buf, c)) nomem();
      APPENDHEAD("Message-ID: <");
      appendhead(buf, c);
      APPENDHEAD(">\r\n");
      LOG1("created Message-ID \"<%s>\"", buf);
   }
   else
      LOG3("read article \"<%s>\"", msgid.buf);
   messageid = msgid.buf;

   /* Date.  strftime(3) uses locale, we don't want that. */
   if (!have_date)
   {
      static char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
      static char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
      char *p;
      time_t t;
      struct tm *tmp;

      p = buf + sizeof (buf) - 1;
      *p-- = '\0';
      time(&t);
      tmp = gmtime(&t);
      PUT2(tmp->tm_sec, ':')
      PUT2(tmp->tm_min, ':')
      PUT2(tmp->tm_hour, ' ')
      c = tmp->tm_year + 1900;
      for (i = 0; i < 4; i++)
      {
         *p-- = c % 10 + '0';
         c /= 10;
      }
      *p-- = ' ';
      p -= 3;
      memcpy(p + 1, months[tmp->tm_mon], 3);
      *p-- = ' ';
      PUT2(tmp->tm_mday, ' ')
      *p-- = ',';
      p -= 2;
      memcpy(p, days[tmp->tm_wday], 3);
      APPENDHEAD("Date: ");
      APPENDHEAD(p);
      APPENDHEAD(" -0000\r\n");
      LOG3("added Date to article \"<%s>\"", messageid);
   }

   if (0 == body_lines)
   {
      if (B_APPENDL(&body, "\r\n")) nomem();
      body_lines++;
   }

   /* Bytes, Lines */
   c = formats(buf, sizeof (buf) - 1, "Bytes: %u\r\nLines: %u\r\n", body.used, body_lines);
   appendhead(buf, c);

   /* done */
   art.head = head.buf;
   art.hlen = head.used;
   art.body = body.buf;
   art.blen = body.used;
   return TRUE;
}
