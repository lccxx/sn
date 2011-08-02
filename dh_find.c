/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Read-only portion of disk hashing.  See dhash.c
 */

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include "config.h"
#include "dhash.h"
#include "allocate.h"
#include "newsgroup.h"

#include <out.h>
#include <nap.h>

static const char ver_ctrl_id[] = "$Id: dh_find.c 56 2004-07-27 16:54:48Z patrik $";

int dh_fd = -1;

char dh_tablefile[sizeof (DH_TABLEFILE) + 32];
char dh_chainfile[sizeof (DH_TABLEFILE) + 32];
char dh_groupfile[sizeof (DH_GROUPFILE) + 32];
char dh_tmp[sizeof (DH_GROUPFILE) + 32];
bool dh_isreadonly = FALSE;

struct table *dh_table = NULL;

/* General string hashing function, taken from djb */

unsigned dhhash (char *buf)
{
   unsigned h;
   int len = strlen(buf);

   h = 5381;
   while (len)
   {
      --len;
      h += (h << 5);
      h ^= (unsigned) *buf++;
   }
   return (h % DH_SIZE);
}

/*
 * TODO: Maybe use a different kind of lock?  Since most of the time
 * we expect to gain the lock, it might make sense to use a flag in
 * the map.  But then we would for sure be faulting the first page,
 * even for readonly access.
 */

int dhlock (void)
{
   int i;

   for (i = 100; i--;)
   {
      if (lockf(dh_fd, F_TLOCK, 0) == 0)
         return 0;
      if (errno != EAGAIN)
         return -1;
      nap(0, 500);
   }
   return -1;
}

int dhunlock (void)
{
   lseek(dh_fd, 0, SEEK_SET);
   lockf(dh_fd, F_ULOCK, 0);
   return 0;
}

struct chain *dhlocatechain (char *messageid)
{
   unsigned index;
   unsigned char *x;
   unsigned h;
   struct chain *chp;

   h = dhhash(messageid);
   x = dh_table->next + h * 3;
   index = char3toint(x);
   if (index == 0)
      return NULL;

   chp = allo_deref(index);
   if (chp == NULL)
   {
      LOG("dhlocatechain:bad allo deref");
   }
   return chp;
}

static int initfile (void)
{
   int fd;
   int integer = DH_MAGIC;
   int i;
   char foo[3] = { '\0', };

   fd = open(dh_tablefile, O_RDWR | O_CREAT | O_EXCL, 0644);
   if (fd == -1)
      return -1;
   if (lockf(fd, F_TLOCK, 0) == -1)
   {
      do
      { /* wait until other process has initialized it */
         if (errno == EAGAIN)
            nap(0, 200);
         else
            goto fail;
      }
      while (lockf(fd, F_TLOCK, 0) == -1);
      lockf(fd, F_ULOCK, 0);
      return fd;
   }
   if (write(fd, &integer, sizeof (int)) != sizeof (int))
      goto fail;
   for (i = 0; i < DH_SIZE; i++)
      if (write(fd, foo, sizeof (foo)) != sizeof (foo))
         goto fail;
   lseek(fd, 0, SEEK_SET);
   lockf(fd, F_ULOCK, 0);
   return fd;

fail:
   if (fd > -1)
      close(fd);
   return -1;
}

int dh_find (struct data *dp, bool readonly)
{
   struct chain *chp;
   unsigned char *x;

   if (!readonly)   /* lockf() fails on read-only file, and locking not needed anyway for R/O? */
      if (dhlock() == -1)
         return -1;
   chp = dhlocatechain(dp->messageid);
   if (chp == NULL)
      goto fail;
   while (strcmp(dp->messageid, chp->messageid) != 0)
   {
      unsigned index = chp->next;

      if (index == 0)
         goto fail;
      chp = allo_deref(index);
      if (chp == NULL)
      {
         LOG2("dh_find:bad allo deref");
         goto fail;
      }
   }
   dp->serial = chp->serial;
   x = (unsigned char *) chp->newsgroup;
   dp->newsgroup = ng_newsgroup(char2toint(x));
   if (dp->newsgroup == NULL)
      goto fail;
   if (!readonly)
      dhunlock();
   return 0;

fail:
   if (!readonly)
      dhunlock();
   return -1;
}

int dh_open (char *pre, bool readonly)
{
   struct stat st;
   int prot;
   int openflag;

   if (pre && *pre)
   {
      if (strlen(pre) > 27)
      {
         LOG("SNROOT too long");
         return -1;
      }
      strcpy(dh_tablefile, pre);
      strcat(dh_tablefile, DH_TABLEFILE);
      strcpy(dh_chainfile, pre);
      strcat(dh_chainfile, DH_CHAINFILE);
      strcpy(dh_groupfile, pre);
      strcat(dh_groupfile, DH_GROUPFILE);
   }
   else
   {
      strcpy(dh_tablefile, DH_TABLEFILE);
      strcpy(dh_chainfile, DH_CHAINFILE);
      strcpy(dh_groupfile, DH_GROUPFILE);
   }

   prot = PROT_READ;
   if (!(dh_isreadonly = !!readonly))
   {
      openflag = O_RDWR;
      prot |= PROT_WRITE;
   }
   else
      openflag = O_RDONLY;

   if ((dh_fd = open(dh_tablefile, openflag)) == -1)
   {
      if (errno == ENOENT)
      {
         if ((dh_fd = initfile()) == -1)
         {
            LOG("dh_open:Can't create %s: %m", dh_tablefile);
            return -1;
         }
      }
      else
      {
         LOG("dh_open:Can't open %s: %m", dh_tablefile);
         return -1;
      }
   }

   if (ng_init() == -1)
      goto fail;

   dh_table = (struct table *) mmap(0, sizeof (struct table), prot, MAP_SHARED, dh_fd, 0);

   if (dh_table == NULL || dh_table == MAP_FAILED)
   {
      LOG("dh_open:mmap:%m?");
      dh_table = NULL;
   }
   else if (dh_table->magic != DH_MAGIC)
      LOG("dh_open:Bad magic");
   else if (fstat(dh_fd, &st) == -1)
      LOG("dh_open:fstat:%m");
   else if (st.st_size != sizeof (struct table))
      LOG("dh_open:table has wrong size!");
   else if (allo_init(dh_chainfile, openflag) == -1 &&
            allo_init(dh_chainfile, openflag | O_CREAT) == -1)
   {
      LOG("dh_open:allo_init(%s):%m?", dh_chainfile);
   }
   else
      return 0;
   if (dh_table)
   {
      munmap(dh_table, sizeof (struct table));
      dh_table = 0;
   }
   ng_fin();

fail:
   close(dh_fd);
   dh_fd = -1;
   return dh_fd;
}

int dh_close (void)
{
   if (dh_fd > -1)
   {
      close(dh_fd);
      dh_fd = -1;
   }
   if (dh_table != NULL)
   {
      munmap((caddr_t) dh_table, sizeof (struct table));
      dh_table = NULL;
   }
   allo_destroy();
   ng_fin();
   return 0;
}
