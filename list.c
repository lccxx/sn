/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Functions for the NNTP LIST command and its variants.
 * Assumes args[0] is "LIST", and args[1] is not null.
 */

#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "config.h"
#include "art.h"
#include "group.h"
#include "args.h"
#include "snntpd.h"
#include "key.h"
#include <out.h>
#include <wildmat.h>

static const char ver_ctrl_id[] = "$Id: list.c 29 2004-04-24 23:02:38Z patrik $";

/* Same as LIST but may have an optional wildmat */

static void listactive (void)
{
   struct group g;
   struct key *kp;
   int i, j;
   char *group;

   args_write(1, "215 list follows\r\n");

   for_each_key(i, j, kp)
   {
      group = KEY(kp);
      if (!args[1] || !args[2] || wildmat(group, args[2]))
         if (0 == group_info(group, &g))
            args_write(1, "%s %d %d %s\r\n", group, g.last, g.first, g.nopost ? "n" : "y");
   }

   args_write(1, ".\r\n");
}

/* Display format of overview database */

static void listoverviewfmt (void)
{
   args_write(1, "215 ok\r\n");
   args_write(1, "Subject:\r\n"
              "From:\r\n"
              "Date:\r\n"
              "Message-ID:\r\n" "References:\r\n" "Bytes:\r\n" "Lines:\r\n" "Xref:full\r\n.\r\n");
}

static void listnewsgroups (void)
{
   args_write(1, "215 Here you go\r\n");

   if (nr_keys > 0)
   {
      char *group;
      int i, j;
      struct key *kp;

      for_each_key(i, j, kp)
      {
         group = KEY(kp);
         if (!args[2] || wildmat(group, args[2]))
         {
            char buf[128];

            if (topline(group, ".info", buf, sizeof (buf)) <= 0)
               *buf = '\0';
            args_write(1, "%s %s\r\n", group, buf);
         }
      }
   }
   args_write(1, ".\r\n");
}

/* ZB was here */

static void listdistribpats (void)
{
   args_write(1, "215 ok\r\n");
   args_write(1, ".\r\n");
}

void do_list (void)
{
   if (args[1])
   {
      if (0 == strcasecmp(args[1], "active"))
         listactive();
      else if (0 == strcasecmp(args[1], "newsgroups"))
         listnewsgroups();
      else if (0 == strcasecmp(args[1], "overview.fmt"))
         listoverviewfmt();
      else if (0 == strcasecmp(args[1], "distrib.pats"))
         listdistribpats();
      else
         args_write(1, "503 \"LIST %s\" command not implemented\r\n", args[1]);
   }
   else
      listactive();
}

void do_listgroup (void)
{
   struct article bogus;
   int i;

   if (args[1])
   {
      if (alltolower(args[1]) > GROUPNAMELEN || make_current(args[1]))
      {
         args_write(1, "411 No such group\r\n");
         return;
      }
   }
   else if (!currentgroup)
   {
      args_write(1, "412 No group selected\r\n");
      return;
   }
   args_write(1, "211 Article numbers follow\r\n");
   for (i = currentinfo.first; i <= currentinfo.last; i++)
      if (0 == art_gimme(currentgroup, i, &bogus))
         writef(1, "%d\r\n", i);
   args_write(1, ".\r\n");
   currentserial = currentinfo.first;
}
