/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Disk based persistent hash table, like gdbm.
 *
 * This is specifically for the news system.  The keys are message
 * ids, the values are {newsgroup, serial number} tuples.
 *
 * The table is static in size.  There a 3 files: ".table" is the hash
 * table proper.  It contains an array of chain "pointers".  The
 * other 2 files are handled by allo_*().  ".chain" is an arena of
 * chain structures; ".newsgroup" contains all newsgroups.
 *
 * Supported opertions are insert, delete, and search.
 *
 * Requires allocate.c to allocate space on a disk file (".chain")
 * for data.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/time.h>
#include "config.h"
#include "allocate.h" /* Save ids and chains */
#include "dhash.h"
#include "newsgroup.h" /* Save newsgroup names */
#include <out.h>
#include <nap.h>

static const char ver_ctrl_id[] = "$Id: dhash.c 56 2004-07-27 16:54:48Z patrik $";

extern char dh_tablefile[];
extern char dh_chainfile[];
extern char dh_groupfile[];
extern struct table *dh_table;
extern unsigned dhhash (char *);
extern struct chain *dhlocatechain (char *);
extern bool dh_isreadonly;
extern int dhlock(void);
extern int dhunlock(void);

static char *inttochar2 (int integer)
{
   static unsigned char buf[2];

   buf[0] = (integer & 0xffff) >> 8;
   buf[1] = integer & 0xff;
   return ((char *) buf);
}

static char *inttochar3 (unsigned integer)
{
   static unsigned char buf[3];

   integer >>= 2; /* last 2 bits will be 0 */
   buf[0] = (integer & 0xffffff) >> 16;
   buf[1] = (integer & 0xffff) >> 8;
   buf[2] = integer & 0xff;
   return ((char *) buf);
}

int dh_insert (struct data *dp)
{
   struct chain *chp;
   unsigned h;
   int off;
   int len;

   if (dh_isreadonly)
   {
      errno = EPERM;
      return -1;
   }
   /* Search first; if found, cannot insert. */
   if (dhlock() == -1)
      return -1;
   chp = dhlocatechain(dp->messageid);
   while (chp != NULL)
   {
      unsigned index;

      if (strcmp(dp->messageid, chp->messageid) == 0)
      {
         dhunlock();
         errno = EEXIST;
         return -1;
      }
      index = (unsigned) chp->next;
      if (index == 0)
         break;
      chp = allo_deref(index);
      if (chp == NULL)
      {
         LOG("dh_insert:bad deref in chain for \"<%s>\" during search", dp->messageid);
         break;
      }
   }

   len = sizeof (struct chain) + strlen(dp->messageid) + 1 - sizeof (chp->messageid);
   off = allo_make(len);
   chp = allo_deref(off);
   if (chp == NULL)
   {
      LOG("dh_insert:allo_make gave bad allo_deref");
      dhunlock();
      return -1;
   }
   chp->serial = dp->serial;
   strcpy(chp->messageid, dp->messageid);
   {
      int ident;

      ident = ng_ident(dp->newsgroup);
      if (-1 == ident)
         if (-1 == (ident = ng_addgroup(dp->newsgroup)))
            return -1;
      memcpy(chp->newsgroup, inttochar2(ident), 2);
   }

   /*
    * If someone searches before
    * dh_table->next[h] is updated, they just will miss the new one.
    * Readers don't need to lock, not even LOCK_SH.  The worst that
    * can come of this is, someone else has just inserted the same
    * object, and we have two of them instead of one.
    */

   h = dhhash(dp->messageid);

   {
      unsigned char *x = dh_table->next + h * 3;

      chp->next = char3toint(x);
      memcpy(x, inttochar3(off), 3);
   }
   dhunlock();
   return 0;
}

int dh_delete (struct data *dp)
{
   struct chain *oldchp = NULL;
   struct chain *chp;

   if (dh_isreadonly)
   {
      errno = EPERM;
      return -1;
   }

   if (dhlock() == -1)
      return -1;
   chp = dhlocatechain(dp->messageid);
   if (chp == NULL)
      goto fail;

   while (strcmp(chp->messageid, dp->messageid) != 0)
   {
      unsigned index = chp->next;

      if (index == 0)
         goto fail;
      oldchp = chp;
      chp = allo_deref(index);
      if (chp == NULL)
      {
         LOG2("dh_delete:dereferencing bad offset");
         goto fail;
      }
   }

   *chp->messageid = '\0';
   if (oldchp != NULL)
      oldchp->next = chp->next;
   else
   {
      unsigned i = chp->next;
      unsigned h = dhhash(dp->messageid);

      memcpy(dh_table->next + h * 3, inttochar3(i), 3);
   }
   allo_free(allo_ref(chp), sizeof (struct chain) + strlen(dp->messageid) + 1 - sizeof (chp->messageid));

   dhunlock();
   return 0;

fail:
   dhunlock();
   return -1; /* Not found */
}
