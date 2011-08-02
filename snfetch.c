/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Fetch articles with a degree of pipelining.
 */

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <time.h>
#include "config.h"
#include "args.h"
#include "dhash.h"
#include "parameters.h"
#include "key.h"
#include <readln.h>
#include <b.h>
#include <out.h>
#include <format.h>
#include <opt.h>

static const char ver_ctrl_id[] = "$Id: snfetch.c 40 2004-04-28 18:00:25Z patrik $";

struct readln input = { 0, };
bool newsbatch = FALSE;
int pipelining = 0;
int debug = 0;
int bytesin = 0;
int nrhave;
int nrdup;

static void nomem (void) { fail(2, "No memory"); }
static void badresponse (char *cmd) { LOG("Bad response to %s, got \"%s\"", cmd, args_inbuf); _exit(3); }
static void badoutput (char *cmd, char *line) { LOG("Bad output to %s, got \"%s\"", cmd, line); }

static int doread (void)
{
   int i;

   switch ((i = args_read(&input)))
   {
      case -1:
         fail(2, "network read error:%m");
      case 0:
         fail(3, "invalid reply :%s", args_inbuf);
   }
   return i;
}

static struct art {
   int serial;
   char *id;
} *arts = NULL;

static int nr_arts = 0;

static void addserial (int serial, char *messageid, int len)
{
   static int size = 0;
   char *id;

   id = messageid;
   switch (key_add(&id, len))
   {
      case -1:
         nomem();
      case 1:
         nrdup++;
         return;
      case 0:;
   }
   if (nr_arts >= size)
   {
      struct art *tmp;
      int newsize;

      newsize = (size ? size * 2 : 30);
      if (!(tmp = malloc(sizeof (*arts) * newsize)))
         nomem();
      if (size)
      {
         memcpy(tmp, arts, sizeof (*arts) * size);
         free(arts);
      }
      arts = tmp;
      size = newsize;
   }
   arts[nr_arts].serial = serial;
   arts[nr_arts++].id = id;
}

void sendstat (int from, int to)
{
   int i, n, p, stopwriting, startreading;

   if (debug)
      LOG("sendstat:trying");
   if ((p = to - from) > pipelining)
      p = pipelining;

   stopwriting = to;
   to += p;
   startreading = from + p;
   for (n = from; n <= to; n++)
   {
      if (n <= stopwriting)
         if (-1 == args_write(7, "STAT %d\r\n", n))
            fail(2, "sendstat:args_write:%m");
      if (n >= startreading)
      {
         char *cp;
         char *id;
         char *end;

         if (doread() < 3)
            badresponse("STAT");
         i = strtoul(args[0], &end, 10);
         if (423 == i || 430 == i)
            continue;
         if (223 == i && !*end)
            if ('<' == args[2][0])
               if ((cp = strchr(id = args[2] + 1, '>')))
               {
                  *cp = '\0';
                  i = strtoul(args[1], &end, 10);
                  if (i > 0 && !*end)
                  {
                     addserial(i, id, cp - id);
                     continue;
                  }
               }
         badoutput("STAT", args_inbuf);
      }
   }
}

void sendxhdr (int from, int to)
{
   char *fmt;

   if (-1 == to)
      fmt = "XHDR Message-Id %d-\r\n";
   else
      fmt = "XHDR Message-Id %d-%d\r\n";
   args_write(7, fmt, from, to);

   doread();
   if (221 != strtoul(args[0], 0, 10))
   {
      sendstat(from, to);
      return;
   }

   for (;;)
   {
      char *line;
      int len, serial;
      char *cp;
      char *id;

      len = readln(&input, &line, '\n');
      if (len <= 0)
         fail(2, "readln:%m");
      line[--len] = '\0';
      if (len > 0)
         if ('\r' == line[len - 1])
            line[--len] = '\0';
      if (1 == len && '.' == line[0])
         return;

      serial = strtoul(line, &cp, 10);
      if (serial > 0)
         if (cp && (' ' == *cp || '\t' == *cp))
            if ((cp = strchr(cp, '<')))
               if ((cp = strchr(id = cp + 1, '>')))
               {
                  *cp = '\0';
                  addserial(serial, id, cp - id);
                  continue;
               }
      badoutput("XHDR", line);
   }
}

void readprint (void)
{
   int len;
   char *line;
   static struct b b = { 0, };
   char *endline;
   int endlinelen;
   bool seenbreak;

   if (newsbatch)
   {
      endline = "\n";
      endlinelen = 1;
   }
   else
   {
      endline = "\r\n";
      endlinelen = 2;
   }

   b.used = 0;
   seenbreak = FALSE;
   while ((len = readln(&input, &line, '\n')) > 1)
   {
      bytesin += len;
      line[--len] = '\0';
      if (len > 0)
         if ('\r' == line[len - 1])
            line[--len] = '\0';
      if (0 == len)
         seenbreak = TRUE;
      if ('.' == *line)
      {
         if (1 == len)
            break;
         else if (newsbatch)
         {
            line++;
            len--;
         }
      }
      if (len)
         if (b_appendl(&b, line, len))
            nomem();
      if (b_appendl(&b, endline, endlinelen))
         nomem();
   }

   if (len <= 0)
      fail(4, "readprint:readln:%m");
   if (len != 1)
      fail(3, "readprint:bad format");
   if (!seenbreak)
      if (b_appendl(&b, endline, endlinelen))
         nomem();
   if (newsbatch)
   {
      char buf[64];
      struct iovec v[2];

      v[0].iov_len = formats(buf, sizeof (buf) - 1, "#! rnews %d\n", b.used);
      v[0].iov_base = buf;
      v[1].iov_base = b.buf;
      v[1].iov_len = b.used;
      len = writev(1, v, 2);
   }
   else
   {
      if (b_appendl(&b, ".\r\n", 3))
         nomem();
      len = write(1, b.buf, b.used);
   }
   if (-1 == len)
      fail(2, "readprint:write:%m");
}

int fetch (int *nr)
{
   int n, p, collected, last;

   last = collected = 0;
   p = pipelining;
   if (p > nr_arts)
      p = nr_arts;
   for (n = 0; n < nr_arts + p; n++)
   {
      if (n < nr_arts)
      {
         struct data d;

         d.messageid = arts[n].id;
         if (0 == dh_find(&d, FALSE))
         {
            nrhave++;
            arts[n].serial = -1;
         }
         else
            args_write(7, "ARTICLE %d\r\n", arts[n].serial);
      }
      if (n >= p && arts[n - p].serial > -1)
      {
         char *end;

         if (doread() < 3)
            badresponse("ARTICLE");
         switch (strtoul(args[0], &end, 10))
         {
            default:
               badresponse("ARTICLE");
            case 220:
               last = strtoul(args[1], &end, 10);
               readprint();
               collected++;
               break;
            case 430:
            case 423:
               if (*end)
                  fail(3, "fetch:Bad response to ARTICLE, got \"%s\"", args_inbuf);
         }
      }
   }
   *nr = collected;
   return last;
}

static int cat (char *fn, int *val)
{
   char buf[64];
   int i;
   char *end;
   int fd = open(fn, O_RDONLY);

   if (-1 == fd)
      return -1;
   i = read(fd, buf, sizeof (buf) - 1);
   close(fd);
   if (-1 == i)
      return -1;
   buf[i] = '\0';
   *val = strtoul(buf, &end, 10);
   if (*end && *end != '\n')
      return -1;
   return 0;
}

void usage (void) { fail(1, "Usage:%s [-r] [-c depth] [-t timeout] newsgroup [serial [max]]", progname); }

int main (int argc, char **argv)
{
   time_t t;
   char *newsgroup;
   int i;
   char *cp;
   int serial = -1;
   int max = -1;
   int timeout = 60;
   int from, to, last, nr;
   bool logpid = FALSE;
   int fd, len;

   progname = ((cp = strrchr(argv[0], '/')) ? cp + 1 : argv[0]);

   while ((i = opt_get(argc, argv, "tc")) > -1)
      switch (i)
      {
         case 'r': newsbatch = TRUE; break;
         case 'd': debug++; break;
         case 'V': version(); _exit(0);
         case 't':
            if (!opt_arg)
               usage();
            timeout = strtoul(opt_arg, &cp, 10);
            if (timeout <= 0 || *cp)
               usage();
            break;
         case 'c':
            if (!opt_arg)
               usage();
            pipelining = strtoul(opt_arg, &cp, 10);
            if (pipelining < 0 || *cp)
               usage();
            break;
         case 'P': logpid = TRUE; break;
         default: usage();
      }
   if (opt_ind >= argc)
      usage();
   newsgroup = argv[opt_ind++];
   if (opt_ind < argc)
   {
      if ((serial = strtoul(argv[opt_ind++], &cp, 10)) < 0)
         fail(1, "serial must be positive");
      else if (*cp)
         usage();
      if (opt_ind < argc)
      {
         if ((max = strtoul(argv[opt_ind++], &cp, 10)) < 0)
            fail(1, "max must be positive");
         else if (*cp)
            usage();
      }
   }

   if ((cp = malloc(i = strlen(progname) + strlen(newsgroup) + 32)))
   {
      if (!logpid)
         formats(cp, i - 1, "%s:%s", progname, newsgroup);
      else
         formats(cp, i - 1, "%s[%u]:%s", progname, getpid(), newsgroup);
      progname = cp;
   }

   parameters(TRUE);

   if (-1 == chdir(snroot) || -1 == chdir(newsgroup))
      fail(2, "chdir(%s/%s):%m", snroot, newsgroup);

   if (-1 == serial)
   {
      if (cat(".serial", &serial) < 0)
      {
         LOG("open(.serial):%m");
         serial = 0;
      }
      else if (serial < 0)
      {
         LOG("Bad serial %d changed to 0", serial);
         serial = 0;
      }
   }
   if (-1 == max)
   {
      if (cat(".max", &max) < 0)
         max = -1;
      else if (max < 0)
      {
         LOG("Bad max %d changed to -1", max);
         max = -1;
      }
   }
   if (pipelining < 0)
   {
      LOG("Bad pipelining %d changed to 0", pipelining);
      pipelining = 0;
   }

   if (-1 == readln_ready(6, timeout, &input))
      fail(2, "readln_ready:%m");

   time(&t);

   if (-1 == args_write(7, "GROUP %s\r\n", newsgroup))
      fail(2, "args_write:%m");

   if (doread() < 5 || strtoul(args[0], &cp, 10) != 211 || *cp)
      badresponse("GROUP");

   from = strtoul(args[2], &cp, 10);
   if (from < 0 || *cp)
      badresponse("GROUP");
   to = strtoul(args[3], &cp, 10);
   if (to < 0 || *cp)
      badresponse("GROUP");

   if (to <= from)
      fail(0, "Empty newsgroup:%s", args_inbuf);
   if (serial == to)
      fail(0, "No new articles");
   last = 0;
   to += 2; /* look ahead a bit */
   if (-1 == dh_open("../", FALSE))
      LOG("WARNING:you will have duplicates");
   if (serial > to || serial < from)
      LOG("out of sync, %d not between %d-%d", serial, last = from, to);
   else
      from = serial; /* normally taken */

   if (max > 0)
      if (to - from > max)
         from = to - max - 1; /* -1 added to make the number of arts downloaded == max. */

   nrhave = nrdup = 0;
   sendxhdr(from, to);

   if (!nr_arts || (last = fetch(&nr)) <= 0)
      fail(0, "Nothing to fetch");

   dh_close();
   LOG("%d articles (%d bytes) in %d seconds", nr, bytesin, time(NULL) - t);
   LOG1("new/dup/present = %d/%d/%d", nr, nrdup, nrhave);

   if (-1 == (fd = open(".serial.tmp", O_WRONLY | O_CREAT, 0644)))
      LOG("open(.serial.tmp):%m");
   else if (-1 == (len = writef(fd, "%d\n", last)))
      LOG("write(.serial.tmp):%m");
   else
   {
      ftruncate(fd, len);
      close(fd);
      key_free();
      _exit(0);
   }
   LOG("Last serial fetched was %d", last);
   _exit(2);
}
