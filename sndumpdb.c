/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Dump database.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include "config.h"
#include "allocate.h"
#include "newsgroup.h"
#include "dhash.h"
#include "parameters.h"
#include <format.h>
#include <out.h>
#include <opt.h>

static const char ver_ctrl_id[] = "$Id: sndumpdb.c 55 2004-07-27 14:46:57Z patrik $";

int debug = 0;
int outfd = 1;

int dh_dump (int next)
{
   int i;
   int nr = 0;
   unsigned index;
   struct chain *chp;
   int empties = 0;
   int total = 0;
   int maxlen = 0;
   int bytes = 0;
   char tmp_str[20];

   for (i = 0; i < DH_SIZE; i++)
   {
      unsigned char *x = dh_table->next + i * 3;

      index = char3toint(x);
      if (!index)
         empties++;
      else
      {
         int len = 0;

         for (; index; index = chp->next)
         {
            char *group;
            int ident;

            chp = allo_deref(index);
            if (NULL == chp)
            {
               LOG("dh_dump:allo_deref returned bad ref");
               return (0 - nr);
            }
            ident = char2toint(chp->newsgroup);
            group = ng_newsgroup(ident);
            if (NULL == group)
            {
               LOG("dh_dump:bad newsgroup for ident %d", ident);
               return (0 - nr);
            }
            if (!next)
               writef(outfd, "%s %d <%s>\n", group, chp->serial, chp->messageid);
            else
               writef(outfd, "[%d,%d]%s %d <%s>\n",
                      index, chp->next, group, chp->serial, chp->messageid);
            nr++;
            len++;
            bytes += strlen(chp->messageid);
         }
         total += len;
         if (len > maxlen)
            maxlen = len;
      }
   }

   LOG("%d chains of %d empty", empties, DH_SIZE);
   LOG("Total %d entries", total);
   if (empties < DH_SIZE)
   {
      snprintf(tmp_str, 20, "%.2f", ((double) total) / (DH_SIZE - empties));
      LOG("for average chain length of %s", tmp_str);
   }
   LOG("with max chain length of %d", maxlen);
   if (total)
   {
      snprintf(tmp_str, 20, "%.2f", ((double) bytes) / total);
      LOG("Average length of ID is %s", tmp_str);
   }
   return nr;
}

#define usage() fail(1, "Usage: %s [-i] [-o file]", progname)

int main (int argc, char **argv)
{
   int nr;
   char *cp;
   int withnext = 0;
   int i;

   progname = ((cp = strrchr(argv[0], '/')) ? cp + 1 : argv[0]);

   parameters(FALSE);

   if (-1 == chdir(snroot))
      FAIL(2, "chdir(%s):%m", snroot);

   while ((i = opt_get(argc, argv, "o")) > -1)
      switch (i)
      {
         case 'P': log_with_pid(); break;
         case 'd': debug++; break;
         case 'V': version(); _exit(0);
         case 'i': withnext = 1; break;
         case 'o':
            if (!opt_arg)
               usage();
            outfd = open(opt_arg, O_WRONLY | O_CREAT, 0644);
            if (-1 == outfd)
               fail(2, "open(%s):%m", opt_arg);
            break;
         default: usage();
      }
   if (opt_ind < argc)
      usage();

   if (-1 == dh_open(NULL, TRUE))
      _exit(2);

   nr = dh_dump(withnext);
   if (nr < 0)
   {
      LOG("Error (%m?) while reading record number %d", 0 - nr);
      dh_close();
      _exit(2);
   }
   dh_close();
   _exit(0);
}
