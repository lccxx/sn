/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Rewrite an rfc822 mail message into one suitable for news:
 *   Replace all Received: lines by a single Path: line.
 *   Construct a Newsgroups: line, using either a given newsgroup
 *   name, or from the From: header.
 *   Unfold all folded header lines.
 *   Convert In-Reply-To: and add to References: (Thanks ZB).
 *
 * Usenet message ID's are more restrictive than mail ID's.  I don't
 * care.
 *
 * Exec snstore and pass the altered message to it.
 * The message is read on fd 0.
 *
 * End-of-line expected is normal Unix style '\n', which gets
 * converted to '\r\n'.
 */

#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "config.h"
#include "parameters.h"
#include "hostname.h"
#include "unfold.h"
#include "path.h"
#include "addr.h"
#include "field.h"
#include <b.h>
#include <readln.h>
#include <out.h>
#include <opt.h>
#include <cmdopen.h>
#include <format.h>

int debug = 0;

static const char ver_ctrl_id[] = "$Id: snmail.c 58 2004-07-28 18:56:20Z patrik $";

char *suffix = NULL;
char *prefix = "local.";
char *in_reply_to = NULL;
struct b path = { 0, };
struct b references = { 0, };
struct readln input = { 0, };

void memerr (void) { fail(2, "No memory"); }

/*
 * Construct a References: line
 */

void appendtoreferences (char *buf)
{
   char *dst;
   char *p;

   dst = p = buf;
   while ((p = addr_qstrchr(p, '<')))
   {
      int len;
      
      if ((len = addr_msgid(p)) <= 0)
      {
         p++;
         continue;
      }
      if (-1 == b_appendl(&references, " <", 2)) memerr();
      if (-1 == b_appendl(&references, buf, addr_unescape(p + 1, buf, len - 2))) memerr();
      if (-1 == b_appendl(&references, ">", 1)) memerr();
      p += len;
   }
}

/*
 * Construct a Path: line from Received: lines
 */

void appendtopath (char *rcvd)
{
   for (;; rcvd++)
   {
      int len;

      if (!(rcvd = addr_qstrchr(rcvd, ' ')))
         return;
      if (strncasecmp(rcvd, " by ", 4))
         continue;
      for (rcvd += 4; ' ' == *rcvd || '\t' == *rcvd; rcvd++)
         if (!*rcvd)
            return;
      if ((len = addr_domain(rcvd)) > 0)
      {
         if (-1 == b_appendl(&path, "!", 1)) memerr();
         if (-1 == b_appendl(&path, rcvd, len)) memerr();
      }
   }
}

/*
 * To construct the tail part of a Newsgroups: line
 */

void savelocal (char *frm)
{
   int len;

   if ((frm = addr_qstrchr(frm, '<')))
      if ((len = addr_localpart(++frm)) > 0)
      {
         if (!(suffix = malloc(len + 1)))
            memerr();
         addr_unescape(frm, suffix, len);
      }
}

/*
 * Decide what to do with each header line.
 */

struct b head = { 0, };

static int check_from_;

int switchline (char *line, int len)
{
   char *p;
   int i;

   if (check_from_)
   {
      check_from_ = 0;
      if (0 == strncmp(line, "From ", 5))
         return 0;
   }

   if (!check_field(line, len))
      LOG("accepting bad header \"%s\"", line);
   switch (*line)
   {
      case 'f': case 'F':
         if (!suffix)
            if (0 == strncasecmp(line, "From:", 5))
               savelocal(line + 5);
         break;
      case 'r': case 'R':
         if (0 == strncasecmp(line, "Received:", 9))
         {
            appendtopath(line + 9);
            return 0;
         }
         if (0 == strncasecmp(line, "References:", 11))
         {
            appendtoreferences(line + 11);
            return 0;
         }
         if (0 == strncasecmp(line, "Return-Path:", 12))
            savelocal(line + 12); /* overrides From: line */
         break;
      case 'i': case 'I':
         if (0 == strncasecmp(line, "In-Reply-To:", 12))
         {
            char *tmp;
            /* Find the last '<' */
            for (p = tmp = line + 12; (tmp = addr_qstrchr(tmp, '<')); p = tmp++)
               ;
            i = strlen(p);
            if(!(in_reply_to = malloc(i + 1)))
               memerr();
            strncpy(in_reply_to, p, i);   /* Remember for later use if msg has no References: */
            return 0;
         }
         break;
      case 'n': case 'N':
         if (0 == strncasecmp(line, "Newsgroups:", 11))
            return 0;
         break;
   }
   if (-1 == b_appendl(&head, line, len)) memerr();
   if (-1 == b_appendl(&head, "\r\n", 2)) memerr();
   return 0;
}

void usage (void) { fail(1, "Usage:%s [-scanv] [listname [prefix]]", progname); }

int main (int argc, char **argv)
{
   char *fullnewsgroup;
   char *newsgroup;
   char *cp;
   int i;
   int fd;
   char storeopts[12];
   int s;
   int storepid;

   progname = ((cp = strrchr(argv[0], '/')) ? cp + 1 : argv[0]);

   parameters(FALSE);

   if (-1 == chdir(snroot))
      FAIL(2, "Can't chdir(%s):%m", snroot);

   newsgroup = 0;
   s = 1;
   storepid = 0;
   if (argc > 1)
   {
      while ((i = opt_get(argc, argv, "")) > -1)
      {
         switch (i)
         {
            case 'd': debug++; storeopts[s++] = 'd'; break;
            case 'V': version(); _exit(0);
            case 's': storepid = -1; break;
            case 'P': log_with_pid(); storeopts[s++] = 'P'; break;
            case 'c': case 'a': case 'n': case 'v': storeopts[s++] = i; break;
            default: usage();
         }
         if (s >= sizeof (storeopts))
            fail(1, "Too many options");
      }
      if (opt_ind < argc)
         newsgroup = argv[opt_ind++];
      if (opt_ind < argc)
         prefix = argv[opt_ind++];
      if (opt_ind != argc)
         usage();
   }

   if (-1 == readln_ready(0, 0, &input))
      FAIL(2, "readln_ready:%m");

   if (storepid != 0)
   {
      char *args[3];

      storeopts[s] = '\0';
      args[0] = "snstore";
      i = 1;
      if (s > 1)
      {
         args[i++] = storeopts;
         *storeopts = '-';
      }
      args[i] = 0;
      set_path_var();
      if ((storepid = cmdopen(args, 0, &fd)) <= 0)
         fail(2, "Can't exec snstore:%m?");
   }
   else
      fd = 1;

   check_from_ = 1;
   switch (unfold(&input, switchline))
   {
      case 0: _exit(0);
      case -1: fail(2, "Read or memory error:%m");
      case -2: case -3: fail(3, "Bad message format");
   }
   if (references.buf == NULL && in_reply_to != NULL)
   {
      appendtoreferences(in_reply_to);   /* Convert In-Reply-To: into References: */
      free(in_reply_to);
   }
   if (!newsgroup)
      if (!(newsgroup = suffix))
         FAIL(1, "No newsgroup specified");

   i = strlen(newsgroup) + strlen(prefix);
   if (!(fullnewsgroup = malloc(i + 1)))
      memerr();
   strcat(strcpy(fullnewsgroup, prefix), newsgroup);

   for (cp = fullnewsgroup; *cp; cp++)
      if (*cp <= 'Z' && *cp >= 'A')
         *cp += 'a' - 'A';

   if (path.buf != NULL)
   {
      if (-1 == writef(fd, "Path: %s\r\n", path.buf + 1))
         goto writeerr;
      free(path.buf);
   }

   if (-1 == write(fd, head.buf, head.used))
      goto writeerr;
   free(head.buf);
   if (references.buf != NULL)
   {
      if (-1 == writef(fd, "References: %s\r\n", references.buf + 1))
         goto writeerr;
      free(references.buf);
   }
   if (-1 == writef(fd, "Newsgroups: %s\r\n\r\n", fullnewsgroup))
      goto writeerr;

   while ((i = readln(&input, &cp, '\n')) > 0)
   {
      cp[--i] = '\0';
      if (i && '\r' == cp[i - 1])
         cp[--i] = '\0';
      if ('.' == *cp)
         write(fd, ".", 1);
      if (-1 == writef(fd, "%s\r\n", cp))
         goto writeerr;
   }
   if (-1 == write(fd, ".\r\n", 3))
      goto writeerr;

   if (!storepid)
      _exit(0);
   i = close(fd);
   if (-1 == (i = cmdwait(storepid)))
      fail(2, "error in waitpid:%m");
   if (i)
      LOG("snstore failed");
   _exit(i);

 writeerr:
   fail(2, "output write error:%m");
   _exit(1);
}
