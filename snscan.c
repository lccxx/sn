/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * If called as snscan:
 * Display the newsgroup, serial, and message id of all newsgroups
 * specified on command line.
 * If called as sncat:
 * Displays all articles mentioned.
 * See usage().
 */

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <netdb.h>
#include <time.h> /* for time_t */
#include <sys/uio.h>
#include <sys/mman.h>
#include <errno.h>
#include "config.h"
#include "artfile.h"
#include "art.h"
#include "group.h"
#include "dhash.h"
#include "hostname.h"
#include "parameters.h"
#include "times.h"
#include "body.h"
#include <out.h>
#include <tokensep.h>
#include <opt.h>
#include <b.h>
#include <format.h>

int debug = 0;

static const char ver_ctrl_id[] = "$Id: snscan.c 29 2004-04-24 23:02:38Z patrik $";

char *host;
int errors = 0;

int (*print) (struct article *, char *, char *, int);
int (*gimme) (char *, int, struct article *);

void usage (void)
{
   fail(1, "Usage:\n"
        "%s [-n] [-s since] [-o file] newsgroup[:lo[-[hi][,...]]]\n"
        "%s [-n] [-s since] [-o file] -i id ...", progname, progname);
}

int snscan (char *newsgroup, int serial)
{
   struct article a;
   char *id;
   char *e = NULL;

   if (gimme(newsgroup, serial, &a))
      return 1;
   if (*a.head && !art_bodyiscorrupt(a.body, a.blen))
      if ((id = art_findfield(a.head, "Message-ID")))
         if (writef(1, "%s %d %s\n", newsgroup, serial, *id ? id : "<0>") > 0)
            return 0;
         else
            fail(2, "write error:%m");
      else
         e = "Can't find %s:%d:%m?";
   else
      e = "Corrupt article %s:%d";
   LOG(e, newsgroup, serial);
   errors++;
   return -1;
}

static int print_native (struct article *ap, char *host, char *newsgroup, int serial)
{
   struct iovec v[10] = { {0,}, };
   int i = 0;
   char buf[256];

   v[i].iov_base = ap->head;
   v[i].iov_len = ap->hlen;
   i++;

   if (host && newsgroup && serial)
   {
      formats(buf, sizeof (buf) - 1, "Xref: %s %s:%d\r\n", host, newsgroup, serial);
      v[i].iov_base = buf;
      v[i].iov_len = strlen(buf);
      i++;
   }

   v[i].iov_base = "\r\n";
   v[i].iov_len = 2;
   i++;

   v[i].iov_base = ap->body;
   v[i].iov_len = ap->blen;
   i++;
   v[i].iov_base = ".\r\n";
   v[i].iov_len = 3;
   i++;

   return writev(1, v, i);
}

static int print_batch (struct article *ap, char *host, char *newsgroup, int serial)
{
   struct b b = { 0, };
   struct iovec v[2];
   char *cp;
   char *p;
   char buf[256];
   int e;

   for (cp = ap->head; (p = strchr(cp, '\r')); cp = p + 2)
   {
      b_appendl(&b, cp, p - cp);
      if (-1 == b_appendl(&b, "\n", 1))
         goto fail;
   }

   if (host && newsgroup && serial)
   {
      formats(buf, sizeof (buf) - 1, "Xref: %s %s:%d\n\n", host, newsgroup, serial);
      b_append(&b, buf);
   }
   else
      b_appendl(&b, "\n", 1);

   for (cp = ap->body; (p = strchr(cp, '\r')); cp = p + 2)
   {
      if ('.' == *cp)
         b_appendl(&b, ".", 1);
      b_appendl(&b, cp, p - cp);
      if (-1 == b_appendl(&b, "\n", 1))
         goto fail;
   }

   formats(buf, sizeof (buf) - 1, "#! rnews %d\n", b.used);

   v[0].iov_base = buf;
   v[0].iov_len = strlen(buf);
   v[1].iov_base = b.buf;
   v[1].iov_len = b.used;
   e = writev(1, v, 2);
   free(b.buf);
   return e;

fail:
   if (b.buf)
      free(b.buf);
   return -1;
}

int sncat (char *newsgroup, int serial)
{
   struct article a;
   char *e = NULL;

   if (gimme(newsgroup, serial, &a))
      return 1;
   if (!art_bodyiscorrupt(a.body, a.blen))
      if (0 == body(&a.body, &a.blen))
         if (print(&a, host, newsgroup, serial))
            return 0;
         else
            fail(2, "write error:%m");
      else
         e = "Can't decompress? %s:%d:%m?";
   else
      e = "Corrupt body in %s:%d";
   LOG(e, newsgroup, serial);
   errors++;
   return -1;
}

int sncancel (char *newsgroup, int serial)
{
   struct file *fp;
   struct article a;
   struct data d;
   int slot, fd, er;
   char path[GROUPNAMELEN + 32];
   char *id;
   char *p;

   if (-1 == art_gimmenoderef(newsgroup, serial, &a))
      return 1;
   id = art_findfield(a.head, "Message-ID");
   if (!id || !*id)
   {
      LOG1("sncancel:Can't find ID in %s:%d", newsgroup, serial);
      errors++;
      return -1;
   }
   if ('<' == *id)
      id++;
   if ((p = strrchr(id, '>')))
      *p = '\0';

   formats(path, sizeof (path) - 1, "%s/%d", newsgroup, serial / ARTSPERFILE);
   er = 1;
   if ((fd = open(path, O_RDWR)) > -1)
   {
      fp = (struct file *) mmap(0, sizeof (*fp), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      if (fp && fp != MAP_FAILED)
      {
         if (0 == lockf(fd, F_LOCK, 0))
         {
            slot = serial % ARTSPERFILE;
            fp->info[slot].hoffset = fp->info[slot].boffset = -1;
            d.messageid = id;
            if (dh_delete(&d))
               LOG1("dh_delete(<%s>):%m", id);
            return 0;
         }
         else
         {
            LOG1("lockf(%s):%m", path);
         }
         munmap(fp, sizeof (*fp));
      }
      else
      {
         LOG1("mmap(%s):%m", path);
      }
      close(fd);
   }
   else if (ENOENT != errno)
      LOG("sncancel:open(%s):%m", path);
   errors += er;
   return -1;
}

static int range (char *spec, int *hi, int *lo)
{
   char *cp;

   if (*spec < '0' || *spec > '9')
      return -1;
   if ((*lo = strtol(spec, &cp, 10)) < 0)
      return -1;
   if (!*cp)
      return (*hi = 0);
   if ('-' != *cp)
      return -1;
   spec = cp + 1;
   if (!*spec)
   {
      *hi = -1;
      return 0;
   }
   if ((*hi = strtol(spec, &cp, 10)) < 0)
      return -1;
   return (*cp ? -1 : 0);
}

static time_t timefmt (char *str)
{
   struct tm tm = { 0, };
   char *p = str;

   if (*p < '0' || *p > '9')
      return -1;
   tm.tm_year = strtoul(p, &str, 10);
   if (!str || !*str)
      return -1;
   if (tm.tm_year > 1900)
      tm.tm_year -= 1900;
   p = ++str;
   tm.tm_mon = strtoul(p, &str, 10);
   if (!str || !*str)
      return -1;
   tm.tm_mon--;
   p = ++str;
   tm.tm_mday = strtoul(p, &str, 10);
   if (str && *str)
   {
      p = ++str;
      tm.tm_hour = strtoul(p, &str, 10);
      if (str && *str)
      {
         p = ++str;
         tm.tm_min = strtoul(p, &str, 10);
         if (str && *str)
         {
            p = ++str;
            tm.tm_sec = strtoul(p, &str, 10);
         }
      }
   }
   return mktime(&tm);
}

int getnoaliases (char *group, int serial, struct article *ap)
{
   if (-1 == art_gimmenoderef(group, serial, ap))
      return 1;
   if (ap->blen > 0)
      return 0;
   if (0 == strncasecmp(ap->head, "Message-ID:", 11))
      return 1;
   return 0;
}

int earliest (char *ng, time_t since)
{
   int start;

   if ((start = times_since(ng, since)) <= 0)
   {
      if (-1 == start)
      {
         LOG("times_since(%s):%m?", ng);
         errors++;
      }
      return 0;
   }
   return start;
}

int main (int argc, char **argv)
{
   time_t since;
   int (*doit) (char *, int);
   int i, outfd;
   char *cp;
   bool useid = FALSE;
   struct group g;
   bool dhro;

   progname = ((cp = strrchr(argv[0], '/')) ? cp + 1 : argv[0]);

   dhro = TRUE;
   if (0 == strcmp(progname, "sncancel"))
   {
      dhro = FALSE;
      doit = sncancel;
   }
   else if (0 == strcmp(progname, "snscan"))
      doit = snscan;
   else if (0 == strcmp(progname, "sncat"))
      doit = sncat;
   else
   {
      fail(1, "Eh?");
      _exit(29); /* hush compiler */
   }

   parameters(!dhro);

   if (-1 == chdir(snroot))
      FAIL(2, "chdir(%s):%m", snroot);
   host = myname();

   gimme = art_gimme;
   print = print_native;
   since = (time_t) 0;
   while ((i = opt_get(argc, argv, "so")) > -1)
      switch (i)
      {
         case 'P': log_with_pid(); break;
         case 'd': debug++; break;
         case 'n': gimme = getnoaliases; break;
         case 'V': version(); _exit(0);
         case 'i': useid = TRUE; break;
         case 'r': print = print_batch; break;
         case 'o':
            if (!opt_arg)
               usage();
            outfd = open(opt_arg, O_WRONLY | O_CREAT, 0644);
            if (-1 == outfd)
               fail(2, "open(%s):%m", opt_arg);
            if (-1 == dup2(outfd, 1))
               fail(2, "dup:%m");
            break;
         case 's':
            if (!opt_arg)
               usage();
            if ((time_t) - 1 == (since = timefmt(opt_arg)))
               fail(1, "Bad time format");
            if (times_init())
               fail(2, "times_init:%m");
            break;
         default: usage();
      }
   if (opt_ind >= argc)
      usage();

   if (-1 == group_init())
      fail(2, "group_init:%m");

   if (!dhro || useid)
      if (-1 == dh_open(NULL, dhro))
         fail(2, "Can't open database");

   if (useid)
   {
      for (; opt_ind < argc && argv[opt_ind]; opt_ind++)
      {
         struct data d = { 0, };

         if ('<' == *argv[opt_ind])
         {
            d.messageid = argv[opt_ind] + 1;
            if ((cp = strchr(d.messageid, '>')))
               *cp = '\0';
         }
         else
            d.messageid = argv[opt_ind];
         if (-1 == dh_find(&d, dhro))
            continue;
         if (-1 == group_info(d.newsgroup, &g))
            fail(2, "group_info(%s):%m", d.newsgroup);
         if (since)
         {
            int start;

            if (0 == (start = earliest(d.newsgroup, since)))
               continue;
            if (d.serial < start)
               continue;
         }
         (*doit) (d.newsgroup, d.serial);
      }
      dh_close();
      _exit(0);
   }

   if (opt_ind == argc)
      usage();

   for (; opt_ind < argc && argv[opt_ind]; opt_ind++)
   {
      int hi, lo, start;
      char *colon;
      char *newsgroup;

      start = 0;
      newsgroup = argv[opt_ind];
      /* No check to is_valid_name(), not our business.  But check for fs subversions */
      if ((colon = strchr(newsgroup, ':')))
         *colon++ = '\0';
      if (strchr(newsgroup, '/') || strstr(newsgroup, ".."))
      {
         LOG("Bad newsgroup name \"%s\"", newsgroup);
         errors++;
         continue;
      }
      if (-1 == group_info(newsgroup, &g))
      {
         LOG("group \"%s\":%m?", newsgroup);
         errors++;
         continue;
      }
      if (since)
         if (0 == (start = earliest(newsgroup, since)))
            continue;

      if (colon)
      {
         char *comma;

         while ((comma = tokensep(&colon, ",")))
         {
            if ('-' == *comma && !comma[1])
            {
               for (i = g.first; i <= g.last; i++)
                  if (0 == (*doit) (newsgroup, i))
                     break;
               if (i > g.last)
                  errors++;
               for (i = g.last; i >= g.first; i--)
                  if (0 == (*doit) (newsgroup, i))
                     break;
               if (i < g.first)
                  errors++;
            }
            else
            {
               if (range(comma, &hi, &lo))
               {
                  LOG("Bad range \"%s\"", comma);
                  errors++;
                  continue;
               }
               if (hi)
               { /* is a range */
                  if (-1 == hi || hi > g.last)
                     hi = g.last;
                  if (lo < g.first)
                     lo = g.first;
                  if (lo < start)
                     lo = start;
                  for (i = lo; i <= hi; i++)
                     (*doit) (newsgroup, i);
               }
               else
                  (*doit) (newsgroup, lo);
            }
         }
      }
      else
         for (i = (start ? start : g.first); i <= g.last; i++)
            (*doit) (newsgroup, i);
   }
   if (since)
      times_fin();
   group_fin();
   if (!dhro)
      dh_close();
   _exit(errors ? 3 : 0);
}
