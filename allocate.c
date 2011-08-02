/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Module to allocate space for keys and data in the file.
 * Space allocated is in multiples of 4 bytes, from 4 bytes to 160
 * bytes inclusive.  Space larger than that will not be allocated.
 *
 * It is assumed that alignment on 4 bytes boundaries is OK.  Change
 * if not.  It is also assumed the usage pattern will be very constant.
 *
 * How:
 * The file starts out empty but for an array of list headers, which
 * point to nothing.  Each time a request is recieved, the size is
 * rounded up to the next multiple of 4, and the corresponding list
 * is checked for space.  If it is empty, a new block is allocated
 * from the end of the file and returned.
 *
 * If the list isn't empty, the next free block is returned.
 *
 * Splits are not done, because there's no provision for
 * rejoining splits either.  Without joining, the allocator would
 * fragment rapidly.
 *
 * There is no facility to grow a buffer.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/time.h>
#include "config.h"
#include "allocate.h"

#include <nap.h>

#define ALLO_MAGIC 0xd0bed0

#ifndef ALLO_ALIGNMENT
#warning Must define ALLO_ALIGNMENT (4, 8, 16, etc bytes)
#endif
#ifndef ALLO_MIN_CHUNKSIZE
#define ALLO_MIN_CHUNKSIZE ALLO_ALIGNMENT
#endif
#ifndef ALLO_MAX_CHUNKSIZE
#define ALLO_MAX_CHUNKSIZE 256
#endif

#define ALLO_MAX_CHAINS ((ALLO_MAX_CHUNKSIZE - ALLO_MIN_CHUNKSIZE)/ALLO_ALIGNMENT)

#define ALLO_MAX_SIZE ALLO_MAX_CHAINS * ALLO_ALIGNMENT

static const char ver_ctrl_id[] = "$Id: allocate.c 29 2004-04-24 23:02:38Z patrik $";

struct chainfile {
   int chain_magic;
   volatile int next[ALLO_MAX_CHAINS];
};

struct table {
   char *filename;
   char *map;
   int fd;
   int size;
   int oflag;
   int mprot;
};

static struct table table = { 0, };

static int remapfile (void);

void *allo_deref (unsigned int offset)
{
   if (offset >= table.size - ALLO_MAX_CHUNKSIZE)
   {
      if (-1 == remapfile())
         return 0;
      else if (offset >= table.size)
         return 0;
   }
   return ((void *) (table.map + offset));
}

int allo_ref (void *obj)
{
   int off;

   off = (int) obj - (int) table.map;
   if (off >= table.size)
      return -1;
   return off;
}

/* Create the file */

static int initfile (char *filename)
{
   struct chainfile cf = { 0, };
   int fd;
   int i;
   int ret = 0;

   fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
   if (-1 == fd)
      return -1;
   cf.chain_magic = ALLO_MAGIC;
   for (i = 0; i < ALLO_MAX_CHAINS; i++)
      cf.next[i] = 0;
   i = write(fd, &cf, sizeof (cf));
   if (i == sizeof (cf))
   {
      int pad;

      pad = sizeof (cf) % (ALLO_ALIGNMENT);
      if (pad > 0)
         for (i = 0; i < pad && 0 == ret; i++)
            if (1 != write(fd, "", 1))
               ret = -1;
   }
   else
      ret = -1;
   close(fd);
   return ret;
}

static void unmapfile (void)
{
   if (table.fd >= 0)
   {
      close(table.fd);
      table.fd = -1;
   }
   if (table.map)
   {
      munmap(table.map, table.size);
      table.map = NULL;
      table.size = 0;
   }
}

static size_t rounduptopagesize (size_t size)
{
   static size_t pagesize = 0;
   int pages;

   if (0 == pagesize)
      pagesize = getpagesize();
   pages = (size / pagesize) + 1;
   return ((size_t) (pages * pagesize));
}

static int mapfile (void)
{
   struct stat st;

   if (-1 == table.fd)
      table.fd = open(table.filename, table.oflag, 0644);
   if (-1 == table.fd)
      goto fail;

   if (-1 == fstat(table.fd, &st))
      goto fail;
   table.size = rounduptopagesize(st.st_size);
   table.map = mmap(0, table.size, table.mprot, MAP_SHARED, table.fd, 0);
   if (!table.map || table.map == MAP_FAILED)
      goto fail;

   return 0;

fail:
   unmapfile();
   return -1;
}

/*
 * -Maybe- remap.  If nothing has changed, why bother?  Thanks rth.
 */

static int remapfile (void)
{
   struct stat st;

   if (-1 == fstat(table.fd, &st))
      return -1;

   if (st.st_size <= table.size)
      return 0;

   munmap((caddr_t) table.map, table.size);
   return mapfile();
}

/*
 * Returns 0 if lock was obtained; -1 on failure.  If it succeeded,
 * the entry may have been remapped, so caller will need to reset
 * automatic variables from global.
 */

static int lock (void)
{
   if (-1 == lockf(table.fd, F_TLOCK, 0))
   {
      do
      {
         if (EAGAIN != errno)
            return -1;
         else
            nap(0, 200);
      }
      while (-1 == lockf(table.fd, F_TLOCK, 0));
      if (-1 == remapfile())
         return -1;
   }
   return 0;
}

static void unlock (void)
{
   lseek(table.fd, 0, SEEK_SET);
   lockf(table.fd, F_ULOCK, 0);
}

static int checkvalidfile (void)
{
   if (table.size > 0)
   {
      if (table.size < sizeof (struct chainfile))
         return -1;
      if (((struct chainfile *) table.map)->chain_magic != ALLO_MAGIC)
         return -1;
   }
   return 0;
}

/*
 * Initialize from file.  Create if it doesn't exist
 */

#ifndef O_SYNC
#define FLAG_SYNC 0
#else
#define FLAG_SYNC O_SYNC
#endif

int allo_init (char *path, int flag)
{
   if (O_RDWR == (flag & O_RDWR))
      table.mprot = PROT_READ | PROT_WRITE;
   else
      table.mprot = PROT_READ;
   table.oflag = flag & (O_RDONLY | O_RDWR | FLAG_SYNC | O_CREAT);

   if (O_CREAT == (flag & O_CREAT))
      if (-1 == initfile(path))
         return -1;

   table.filename = path;
   table.fd = -1;
   if (-1 == mapfile() || -1 == checkvalidfile())
   {
      unmapfile();
      table.filename = NULL;
      return -1;
   }

   table.filename = strdup(path);

   return 0;
}

static int rounduptoalignment (int size)
{
   if (size <= 0)
      return ALLO_ALIGNMENT;
   return ((((size - 1) / ALLO_ALIGNMENT) + 1) * ALLO_ALIGNMENT);
}

/*
 * Return an offset into the allocated area
 */

#define CFP ((struct chainfile *)table.map)

int allo_make (int size)
{
   static char tmpchunk[ALLO_MAX_CHUNKSIZE + 16] = { '\0', };
   int chain;
   int block;
   int failures;

   size = rounduptoalignment(size);
   if (size > ALLO_MAX_SIZE)
      return -1;

   chain = (size / ALLO_ALIGNMENT) - 1;

   for (failures = 0; failures < 10; failures++)
   {
      if (CFP->next[chain])
      {
         /* Have a free block we can use */
         if (-1 == lock())
            return -1;
         if (!(block = CFP->next[chain]))
         {
            unlock();
            continue;
         }
         CFP->next[chain] = *(int *) (table.map + block);
         memset(table.map + block, 0, size);
      }
      else
      {
         /* No free block to use, extend the file */
         if (-1 == lock())
            return -1;
         block = lseek(table.fd, 0, SEEK_END);
         write(table.fd, tmpchunk, (1 + chain) * ALLO_ALIGNMENT);
         remapfile();
      }
      unlock();
      return block;
   }
   return -1;
}

/*
 * Must pass size, so we know which free list to hook this chunk to.
 * Giving the wrong size is a good way to create garbage.
 */

int allo_free (int chunk, int size)
{
   int chain;

   size = rounduptoalignment(size);
   if (size > ALLO_MAX_SIZE)
      return -1;

   chain = (size / 4) - 1;

   if (-1 == lock())
      return -1;
   *(int *) (table.map + chunk) = CFP->next[chain];
   CFP->next[chain] = chunk;
   unlock();
   return 0;
}

int allo_destroy (void)
{
   unmapfile();
   free(table.filename);
   table.filename = NULL;
   return 0;
}
