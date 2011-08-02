/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Feed the database with values.  This could be useful if the
 * database has suffered corruption.  Then delete all the database
 * files so they will be recreated.  You can feed this program from
 * snscan.
 *
 * The database converts article-id to newsgroup:serial.
 *
 * Input is expected in the form one to a line,
 * newsgroup id serial
 * with spaces between the words.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include "config.h"
#include "dhash.h"
#include "parameters.h"
#include <out.h>
#include <readln.h>
#include <tokensep.h>
#include <opt.h>
#include <format.h>

static const char ver_ctrl_id[] = "$Id: snprimedb.c 29 2004-04-24 23:02:38Z patrik $";

int debug = 0;

struct readln input;

static void handler (int signum)
{
   dh_close();
   LOG("Caught signal %d, exiting", signum);
   _exit(3);
}

int nr = 0;

int insert (char *newsgroup, char *id, int serial)
{
   struct data d = { 0, };
   char *cp;

   if (0 == strcmp(newsgroup, JUNK_GROUP))
      return 0;
   if ('<' == *id)
   {
      d.messageid = id + 1;
      if ((cp = strchr(d.messageid, '>')))
         *cp = '\0';
   }
   else
      d.messageid = id;
   d.newsgroup = newsgroup;
   d.serial = serial;
   if (-1 == dh_insert(&d))
   {
      if (EEXIST == errno)
      {
         LOG("insert:\"%s\" already exists in %s:%d", d.messageid, newsgroup, serial);
         return 1;
      }
      else
         LOG("insert:Can't insert record \"%s %s %d\":%m\n", newsgroup, d.messageid, serial);
      return -1;
   }
   nr++;
   return 0;
}

void usage (void)
{
   LOG("Usage: %s [-i]\n", progname);
   LOG("(No arguments).  Input is read from stdin in the form\n"
       "\"newsgroup id serial\"\n" "until end-of-file.\n");
   _exit(1);
}

int main (int argc, char **argv)
{
   char *line;
   char *newsgroup;
   char *id;
   char *serial;
   bool onlyinitialize = FALSE;
   char *cp;
   int len;
   int i;

   progname = ((cp = strrchr(argv[0], '/')) ? cp + 1 : argv[0]);

   parameters(TRUE);

   while ((i = opt_get(argc, argv, "")) > -1)
      switch (i)
      {
         case 'P': log_with_pid(); break;
         case 'i': onlyinitialize = TRUE; break;
         case 'd': debug++; break;
         case 'V': version(); _exit(0);
         default: usage();
      }
   if (opt_ind != argc)
      usage();

   if (-1 == chdir(snroot))
      FAIL(2, "chdir(%s):%m", snroot);

   if (-1 == dh_open(NULL, FALSE))
      FAIL(2, "Can't open database:%m?");

   if (onlyinitialize)
   {
      dh_close();
      _exit(0);
   }

   signal(SIGPIPE, handler);

   if (-1 == readln_ready(0, 0, &input))
      fail(2, "readln_ready:%m");

   while ((len = readln(&input, &line, '\n')) > 0)
   {
      char *p;
      int s;

      line[len - 1] = '\0';
      if (!(newsgroup = tokensep(&line, " ")))
         continue;
      if (!(serial = tokensep(&line, " ")))
         continue;
      if (!(id = tokensep(&line, " ")))
         continue;
      for (p = newsgroup; *p; p++)
         if (*p >= 'A' && *p <= 'Z')
            *p -= 'a' - 'A';
      if (p - newsgroup >= GROUPNAMELEN)
      {
         LOG("newsgroup name too long, skipping:%s", newsgroup);
         continue;
      }
      s = strtoul(serial, &cp, 10);
      if (s <= 0 || *cp)
         fail(3, "Bad value \"%s\" for serial number", serial);
      switch (insert(newsgroup, id, s))
      {
         default: LOG("Eh?"); /* Fall Through */
         case -1: dh_close(); _exit(2);
         case 1: ;
         case 0: ;
      }
   }
   dh_close();
   LOG("%d insertions", nr);
   _exit(0);
}
