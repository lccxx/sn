/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * NNTP commands here.  Each command is a function of
 * type void foo (void).
 */

#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <limits.h>
#include "config.h"
#include "times.h"
#include "dhash.h"
#include "art.h"
#include "group.h"
#include "args.h"
#include "body.h"
#include "snntpd.h"
#include "key.h"
#include <b.h>
#include <tokensep.h>
#include <wildmat.h>
#include <out.h>
#include <format.h>

static const char ver_ctrl_id[] = "$Id: commands.c 29 2004-04-24 23:02:38Z patrik $";

#define putdot() args_write(1, ".\r\n")

void do_ihave (void) { args_write(1, "435 I'm happy for you\r\n"); }
void do_sendme (void) { args_write(1, "500 No thanks\r\n"); }
void do_slave (void) { args_write(1, "202 Whatever\r\n"); }
void do_help (void) { args_write(1, "100 Help yourself\r\n.\r\n"); }

void do_mode (void)
{
   if (posting_ok)
      args_write(1, "200 Hi, you can post (sn version " VERSION ")\r\n");
   else
      args_write(1, "201 Hi, you can't post (sn version " VERSION ")\r\n");
}

/* Several ways to specify an article.  The group may be the current
   one, or it may be a different one */

static char *specgroup;
static int speclo, spechi;

/* ...but with just one of two results */

static struct article article;
static char *specerror;

/*
 * For nntp commands that refer to an article, or a range of them,
 * these always appear in the form of a number, or range, or a mssage
 * ID.  readspec() understands these and sets the low and high end of
 * the range, and the group, and any error.
 */

static int readspec (char *spec)
{
   char *p;

   if (!spec)
   {
      if ((specgroup = currentgroup))
         if ((spechi = speclo = currentserial) > 0)
            return 0;
         else
            specerror = "420 No article selected\r\n";
      else
         specerror = "412 No group selected\r\n";
      return -1;
   }

   if ('<' == *spec)
   {
      struct data d;

      d.messageid = spec + 1;
      if ((p = strchr(d.messageid, '>')))
      {
         *p = '\0';
         if (0 == dh_find(&d, FALSE))
         {
            spechi = speclo = d.serial;
            specgroup = d.newsgroup; /* is static */
            return 0;
         }
         else
            specerror = "430 No such article\r\n";
      }
      else
         specerror = "430 Invalid message ID\r\n";
      return -1;
   }

   if ((specgroup = currentgroup))
   {
      char *end;

      speclo = strtoul(spec, &end, 10);
      spec = end;
      if ('-' == *end)
      {
         spec++;
         if (*spec)
         {
            spechi = strtoul(spec, &end, 10);
            if (*end)
               goto invalid;
            if (spechi < speclo)
               goto invalid;
         }
         else
            spechi = INT_MAX;
         if (spechi > currentinfo.last)
         {
            if (speclo <= currentinfo.last)
               spechi = currentinfo.last;
            else
               spechi = speclo;
         }
      }
      else if (!*spec)
         spechi = speclo;
      else
         goto invalid;
   }
   return 0;

invalid:
   specerror = "501 Invalid range\r\n";
   return -1;
}

static char *findid (void)
{
   char *id;

   if ((id = art_findfield(article.head, "Message-ID")) && *id)
      return id;
   return "<0>";
}

/*
 * Each time writefifo is called, the degree of interest in this
 * group increases until the fifo is written.
 */

static void writefifo (void)
{
   static char oldgroup[GROUPNAMELEN + 1] = "";
   static int interested = 0;

   if (-1 == fifo)
      return;
   if (strcasecmp(oldgroup, currentgroup) == 0)
   {
      interested++;
      if (3 == interested % 10)
      {
         int len;

         len = strlen(currentgroup);
         currentgroup[len] = '\n';
         (void) write(fifo, currentgroup, len + 1);
         currentgroup[len] = '\0';
      }
   }
   else
   {
      interested = 0;
      strcpy(oldgroup, currentgroup);
   }
}

#define PUTHEAD 0x1
#define PUTBODY 0x2

static void putarticle (int flag, int code, char *msg)
{
   char *p;

   if (-1 == readspec(args[1]))
   {
      args_write(1, specerror);
      return;
   }
   if (-1 == art_gimme(specgroup, speclo, &article))
   {
      args_write(1, "430 No such article\r\n");
      return;
   }
   if (speclo != spechi)
   {
      args_write(1, "512 Invalid argument\r\n");
      return;
   }

   if (flag & PUTBODY)
      do
      {
         /*
          * Bit dangerous.  To test the validity of the body we are about
          * to display, check these 2 things: the bytes -before- and
          * -after- the body buffer must be '\0'.  This is not normally
          * checked for in art_gimme() because it will fault in the page,
          * but now we know we will be accessing it.
          */

         if (!art_bodyiscorrupt(article.body, article.blen))
            if (0 == body(&article.body, &article.blen))
               if (!article.body[article.blen])
                  break;
               else
                  p = "Internal error, uncompressed to corrupt body";
            else
               p = "Internal error, can't uncompress body";
         else
            p = "Article is corrupt";
         args_write(1, "423 %s\r\n", p);
         return;
      }
      while (0);

   if (!args[1] || '<' != *args[1])
   {
      args_write(1, "%d %d %s %s\r\n", code, speclo, findid(), msg);
      currentserial = speclo;
      writefifo();
   }
   else
      args_write(1, "%d %d <%s> %s\r\n", code, speclo, args[1] + 1, msg);

   switch (flag)
   {
      case 0:
         return;
      case PUTHEAD:
      case PUTBODY | PUTHEAD:
         /* should never have let Xref into header, now have to remove it */
         p = strstr(article.head, "Xref:");
         if (p && (p == article.head || '\n' == p[-1]))
            writef(1, "%SXref: %s %s:%d%s",
                   p - article.head, article.head, me, specgroup, speclo, p + 5);
         else
            writef(1, "%sXref: %s %s:%d\r\n", article.head, me, specgroup, speclo);
         if (PUTHEAD == flag)
            break;
         write(1, "\r\n", 2);
      case PUTBODY:
         write(1, article.body, article.blen);
         if ('\n' != article.body[article.blen - 1])
         {
            LOG("putarticle:%s:%d does not end in newline", specgroup, speclo);
            write(1, "\r\n", 2);
         }
   }
   putdot();
}

void do_article (void) { putarticle(PUTHEAD | PUTBODY, 220, "Article follows"); }
void do_body (void) { putarticle(PUTBODY, 222, "Body follows"); }
void do_head (void) { putarticle(PUTHEAD, 221, "Head follows"); }
void do_stat (void) { putarticle(0, 223, "Request text separately"); }

void do_group (void)
{
   if (alltolower(args[1]) >= GROUPNAMELEN || make_current(args[1]))
      args_write(1, "411 No such group here as %s\r\n", args[1]);
   else
   {
      args_write(1, "211 %d %d %d %s\r\n", currentinfo.nr_articles,
                 currentinfo.first, currentinfo.last, args[1]);
      specgroup = 0;
      
      writefifo();
   }
}

static void nextlast (int inc, char *ermsg)
{
   int serial;
   int count = 2 * ARTSPERFILE;

   for (serial = currentserial + inc; serial && count; serial += inc, count--)
      if (0 == art_gimme(currentgroup, serial, &article))
      {
         args_write(1, "223 %d %s request text separately\r\n", serial, findid());
         currentserial = serial;
         return;
      }
   args_write(1, ermsg);
}

void do_last (void) { nextlast(-1, "422 No previous article\r\n"); }
void do_next (void) { nextlast(+1, "421 No next article\r\n"); }

/* date: YYMMDD; clock: HHMMSS */

#define DIG(c) (c - '0')

static time_t converttime (char *date, char *clock, char *gmt)
{
   struct tm tm = { 0, };
   time_t t = 0;
   int difference = 0;
   
   if (strspn(date, "0123456789") != 6 || strspn(clock, "0123456789") != 6)
      return ((time_t) -1);
   
   tm.tm_year = DIG(*date) * 10 + DIG(date[1]);
   date += 2;
   if (tm.tm_year < 70)
      tm.tm_year += 100;  /* 00 - 69 => 2000 - 2069 */
   tm.tm_mon = DIG(*date) * 10 + DIG(date[1]);
   date += 2;
   tm.tm_mon -= 1;
   tm.tm_mday = DIG(*date) * 10 + DIG(date[1]);
   
   /* FvL was here */
   tm.tm_hour = DIG(*clock) * 10 + DIG(clock[1]);
   clock += 2;
   tm.tm_min = DIG(*clock) * 10 + DIG(clock[1]);
   clock += 2;
   tm.tm_sec = DIG(*clock) * 10 + DIG(clock[1]);
   
   tm.tm_isdst = -1;   /* Avoid having the hour adjusted +1 by mktime() during DST */
   
   if (gmt && 0 == strcasecmp(gmt, "GMT"))
   {
      /* Convert to local time, all our stuff is in local time. */
      time_t then = mktime(&tm);
      struct tm *then_tm = gmtime(&then);
      
      then_tm->tm_isdst = -1;   /* Avoid having the hour adjusted +1 by mktime() during DST */
      difference = then - mktime(then_tm);
   }
   
   t = mktime(&tm);
   if (t == ((time_t) -1))
      return ((time_t) -1);
   return (t + difference);
}

void do_newgroups (void)
{
   time_t cutoff;

   cutoff = converttime(args[1], args[2], args[3]);
   if ((long) cutoff > 0)
   {
      /* XXX Do we -have- to always reply 231? */
      args_write(1, "231 new newsgroups follows\r\n");
      if (nr_keys > 0)
      {
         int n, i;
         struct key *kp;
         char *group;
         char buf[GROUPNAMELEN + 32];
         struct stat st;

         for_each_key(i, n, kp)
         {
            group = KEY(kp);
            formats(buf, sizeof (buf) - 1, "%s/.created", group);
            if (-1 == stat(buf, &st))
            {
               LOG("do_newsgroups:stat(%s/.created):%m", group);
               continue;
            }
            if (st.st_mtime >= cutoff)
            {
               struct group g;

               if (-1 == group_info(group, &g))
                  continue;
               args_write(1, "%s %d %d y\r\n", group, g.first, g.last);
            }
         }
      }
      else
         LOG2("do_newgroups:we have no groups");
   }
   else
      args_write(1, "231 Bad date or time\r\n");
   putdot();
}

void do_newnews (void)
{
   time_t cutoff;
   extern int nr_keys;
   int i, j, serial;
   char *pat;

   cutoff = converttime(args[2], args[3], args[4]);

   if (cutoff > 0)
   {
      struct key *kp;
      struct group g;

      args_write(1, "230 list of new articles follows\r\n");
      alltolower(args[1]);
      while ((pat = tokensep(args + 1, ",")))
         for_each_key(i, j, kp)
      {
         char *group;

         group = KEY(kp);
         if (wildmat(group, pat))
            if (0 == group_info(group, &g))
               if ((serial = times_since(group, cutoff)) > 0)
                  for (; serial < g.last; serial++)
                     if (0 == art_gimmenoderef(group, serial, &article))
                        args_write(1, "%s\r\n", findid());
      }
   }
   else
      args_write(1, "230 Bad date or time\r\n");
   args_write(1, ".\r\n");
}

static void xlooper (char *spec, void (*f) (void), char *msg)
{
   if (-1 == readspec(spec))
   {
      args_write(1, specerror);
      return;
   }
   args_write(1, msg);
   for (; speclo <= spechi; speclo++)
      if (0 == art_gimme(specgroup, speclo, &article))
         f();
   putdot();
   if (spec && '<' != *spec)
      writefifo();
}

static void putxover (void)
{
   struct xover x;

   art_makexover(&article, &x);
   writef(1, "%d\t%S\t%S\t%S\t%S\t%S\t%S\t%S\tXref: %s %s:%d\r\n",
          speclo,
          x.subject.len, x.subject.pointer,
          x.from.len, x.from.pointer,
          x.date.len, x.date.pointer,
          x.messageid.len, x.messageid.pointer,
          x.references.len, x.references.pointer,
          x.bytes.len, x.bytes.pointer,
	  x.lines.len, x.lines.pointer,
	  me, specgroup, speclo);
}

void do_xover (void) { xlooper(args[1], putxover, "224 XOVER follows\r\n"); }

static void putxhdr (void)
{
   char *id;

   id = art_findfield(article.head, args[1]);
   if (*id)
      args_write(1, "%d %s\r\n", speclo, id);
   else if (strcasecmp(args[1], "xref") == 0)   /* Xref needs to be generated on the fly */
      args_write(1, "%d %s %s:%d\r\n", speclo, me, specgroup, speclo);
}

void do_xhdr (void) { xlooper(args[2], putxhdr, "221 XHDR follows\r\n"); }

static char *field;

static void putxpat (void)
{
   char **globs;
   char *line;

   globs = args + 3;
   if ((line = art_findfield(article.head, field)) && *line)
      for (; *globs; globs++) /* XXX ORed? */
         if (wildmat(line, *globs))
         {
            args_write(1, "%d %s: %s\r\n", speclo, field, line);
            break;
         }
}

void do_xpat (void)
{
   field = args[1];
   xlooper(args[2], putxpat, "221 Headers follows\r\n");
}
