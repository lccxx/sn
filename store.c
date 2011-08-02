/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Back end of snstore.
 * How it works: Every time an article is to be stored, we find the
 * next available slot to use, lock the fd, write the head and
 * the body, update the info, and
 * unlock.  If the file is full, we reorder the file by writing a
 * new file and copying the heads, then the bodies.  Reordering is
 * done from a call to cache_invalidate() which calls sto_free().
 * This way, we are almost always guaranteed a valid article file,
 * even if the arrangement is sub-optimal.
 */

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "config.h"
#include "times.h"
#include "art.h"
/* Shut up compiler messages about discarding volatile */
#undef VOLATILE
#define VOLATILE /* nothing */
#include "artfile.h"
#include "cache.h"
#include "group.h"
#include <out.h>
#include <format.h>
#include <nap.h>

static const char ver_ctrl_id[] = "$Id: store.c 29 2004-04-24 23:02:38Z patrik $";

#ifdef USE_ZLIB
#include "body.h"
#endif

extern int rename (const char *old, const char *new);
extern ssize_t writev(); /* Slackware 4 needs this */

struct storeobj {
   char *filename;
   int compressok;
   int fd;
   struct file *file;
};

static int sto_cmp (void *a, void *b)
{
   register struct storeobj *x = (struct storeobj *) a;
   register struct storeobj *y = (struct storeobj *) b;

   return strcmp(x->filename, y->filename);
}

static int nosigio (ssize_t(*op) (), int fd, char *buf, int len)
{
   int er;

   while (-1 == (er = (*op) (fd, buf, len)))
      if (EINTR != errno)
      {
         LOG1("nosigio:%m");
         break;
      }
   return er;
}

static int copyart (int tofd, int fromfd, int fromseek, int len)
{
   char buf[1024];

   if (-1 == lseek(fromfd, fromseek, SEEK_SET))
   {
      LOG("copyart:lseek:%m");
      return -1;
   }

   for (; len > 1024; len -= 1024)
      if (-1 == nosigio(read, fromfd, buf, 1024) || -1 == nosigio(write, tofd, buf, 1024))
         return -1;
   if (-1 == nosigio(read, fromfd, buf, len) || -1 == nosigio(write, tofd, buf, len))
      return -1;

   return 0;
}

static int checkindex (struct info *p)
{
   if (p->hoffset > 0)
   {
      if (p->boffset > 0)
         if (p->hlen > 0)
            if (p->blen > 0)
               return 0;
   }
   else if (-1 == p->hoffset && -1 == p->boffset)
      return 1;
   return -1;
}

/*
 * Convention: First one to create the file has it "locked".  It's
 * ok if we fail here, we can't do more damage than is already done.
 * The next call must be to invalidate the object.
 */

static void reorder (struct storeobj *sp)
{
   char tmpname[GROUPNAMELEN + 32];
   char *p;
   char *q;
   int fd;
   struct file f = { 0, };
   int er, i;

   for (p = tmpname, q = sp->filename; (*p++ = *q++) != '/';) ;
   *p++ = '+';
   while ((*p++ = *q++)) ;
   fd = open(tmpname, O_RDWR | O_CREAT | O_EXCL, 0644);
   if (-1 == fd)
   {
      if (EEXIST != errno)
         LOG("reorder:open(%s):%m", tmpname);
      return;
   }

   f.magic = FILE_MAGIC;
   if (-1 == nosigio(write, fd, (char *) &f, sizeof (f)))
      goto fail;

   /*
    * If an article has been cancelled, its info[] will be all -1;
    * other cases are an error.  The info[] is still copied, but the
    * contents are not.  Major checking going on.
    * Also we have to be careful that sncancel is not simultaneously
    * trying to cancel an article we are copying.  We don't have to
    * try too hard, since it isn't likely, and the consequences of it
    * happening are not bad.
    */
   
   if (-1 == lockf(sp->fd, F_TLOCK, 0))
   {
      char *p;

      if (EAGAIN != errno)
         p = "reorder:can't lockf %s:%m";
      else
         p = "reorder:article in %s being cancelled?";
      LOG(p, sp->filename);
      goto fail;
   }

   for (i = 0; i < ARTSPERFILE; i++)
      switch (checkindex(sp->file->info + i))
      {
         case 0: /* all ok */
            if ((f.info[i].hoffset = lseek(fd, 0, SEEK_END)) > 0)
            {
               f.info[i].hlen = sp->file->info[i].hlen;
               f.info[i].blen = sp->file->info[i].blen;
               er = copyart(fd, sp->fd, sp->file->info[i].hoffset, f.info[i].hlen);
               if (0 == er)
                  break;
            }
            else
               LOG("reorder:lseek(%s):%m", tmpname);
            goto fail;
         case 1: /* cancelled article */
            f.info[i].boffset = f.info[i].hoffset = f.info[i].blen = f.info[i].hlen = -1;
            break;
         case -1:
            LOG("reorder:corrupt index in %s", sp->filename);
            goto fail;
      }
   for (i = 0; i < ARTSPERFILE; i++)
   {
      if (-1 == f.info[i].boffset)
         continue;
      if (-1 == (f.info[i].boffset = lseek(fd, 0, SEEK_END)))
      {
         LOG("reorder:lseek(%s):%m", tmpname);
         goto fail;
      }
      er = copyart(fd, sp->fd, sp->file->info[i].boffset, f.info[i].blen);
      if (-1 == er)
         goto fail;
   }

   if (-1 == lseek(fd, 0, SEEK_SET))
   {
      LOG("reorder:lseek:%m");
      goto fail;
   }
   if (-1 == nosigio(write, fd, (char *) &f, sizeof (f)))
      goto fail;
   if (-1 == rename(tmpname, sp->filename))
   {
      LOG("reorder:rename:%m");
      goto fail;
   }
   close(fd);
   return;

fail:
   LOG("reorder:write failed for %s:%m", sp->filename);
   if (-1 == unlink(tmpname))
      LOG("reorder:unlink(%s):%m", tmpname);
   close(fd);
}

static void sto_free (void *p)
{
   struct storeobj *sp = (struct storeobj *) p;

   close(sp->fd);
   if (-1 == munmap((caddr_t) sp->file, sizeof (struct file)))
      LOG("sto_free:munmap:%m");
   free(sp);
}

static int desc;

/* We want a large cache, so we don't end up flushing it often. */

int sto_init (void)
{
   desc = cache_init(8, sto_cmp, sto_free, NULL);
   if (-1 == desc)
      return -1;
   return 0;
}

void sto_fin (void)
{
   if (debug >= 3)
   {
      int hit, miss;

      cache_stat(desc, &hit, &miss);
      LOG("sto_fin:cache requests:%dT=%dH+%dM", hit + miss, hit, miss);
   }
   cache_fin(desc);
}

/*
 * Create and initialize article file if it doesn't already exist.
 * If it does exist, it would have been initialized.
 */

static int tryopen (char *fn)
{
   char fn2[GROUPNAMELEN + 32];
   int fd, i;
   char *p;
   char *q;
   struct timeval tv;

   if ((fd = open(fn, O_RDWR)) > -1)
   {
      lockf(fd, F_LOCK, 0);
      return fd;
   }
   for (p = fn2, q = fn; (*p++ = *q++) != '/';) ;
   *p++ = '+';
   *p++ = '+';
   gettimeofday(&tv, 0);
   
   /*
    * tmp file is 30 second lock.  What is the advantage of this over
    * using, say, the pid?  Minor: creation more likely to fail early at
    * open() than later at link(). XXX should lock life be longer or
    * shorter than timeout?  If longer snstore can fail prematurely; if
    * shorter garbage locks can accumulate.
    */
   
   i = tv.tv_sec / 30;
   do
      *p++ = '0' + (i % 10);
   while ((i /= 10));
   *p++ = '\0';

   for (i = 0; i < 100; nap(0, 300 + i), i++)
   {
      static struct file f = { FILE_MAGIC, {{0,},}, };

      if (14 == i % 15)
         LOG("tryopen:racing on %s", fn);
      if ((fd = open(fn2, O_RDWR | O_CREAT | O_EXCL, 0644)) > -1)
      {
         lockf(fd, F_LOCK, 0);
         if (sizeof (f) == write(fd, (char *) &f, sizeof (f)))
            if (0 == link(fn2, fn))
            {
               unlink(fn2);
               return fd;
            }
         close(fd);
         unlink(fn2);
      }
      /* probably EEXIST due to another process */
      if ((fd = open(fn, O_RDWR)) > -1)
      {
         lockf(fd, F_LOCK, 0);
         return fd;
      }
   }
   LOG("tryopen:timed out opening %s:%m", fn);
   return -1;
}

/*
 * Returns the storeobj already locked.  The file may be full; caller
 * must know what to do.  Caller must specify a file name in
 * sequence, no gaps allowed.
 */

static struct storeobj *getstore (char *filename)
{
   struct storeobj *sp;
   struct storeobj s;
   int fd;
   struct file *fp;

   #ifdef USE_ZLIB
   char buf[GROUPNAMELEN + 32];
   int c;
   #endif

   s.filename = filename;
   if ((sp = cache_find(desc, &s)))
   {
      lockf(sp->fd, F_LOCK, 0);
      return sp;
   }

   if (-1 == (fd = tryopen(filename)))
      return 0;

   /* file exists and is open, locked, and initialized. */

   fp = (struct file *) mmap(0, sizeof (*fp), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (!fp || fp == MAP_FAILED)
      LOG("getstore:mmap(%s):%m", filename);
   else if (!(sp = malloc(sizeof (*sp) + strlen(filename) + 1)))
   {
      LOG("getstore:no memory");
      munmap(fp, sizeof (*fp));
   }
   else
   {
      sp->fd = fd;
      sp->file = fp;
      sp->filename = (char *) sp + sizeof (*sp);
      strcpy(sp->filename, filename);
      #ifdef USE_ZLIB
      {
         char *p;
         char *q;

         for (p = buf, q = filename; (*p++ = *q++) != '/';) ;
         strcpy(p, ".compress");
      }
      if ((fd = open(buf, O_RDONLY)) > -1)
      {
         sp->compressok = 1024;
         if ((c = read(fd, buf, sizeof (buf) - 1)) > 0)
         {
            char *end;

            buf[c] = '\0';
            c = strtoul(buf, &end, 10);
            if (c > 1024 && '\n' == *end)
               sp->compressok = c;
         }
         close(fd);
      }
      else
      #endif
         sp->compressok = 0;
      cache_insert(desc, sp);
      return sp;
   }
   close(fd);
   return 0;
}

/*
 * Start at file 1 (article 10), not 0.  Thanks Marko.
 * We always start with the last file in a group, holes do not
 * affect us.
 */

int sto_add (char *newsgroup, struct article *ap)
{
   struct storeobj *sp;
   struct info info;
   int filenum, slot, er;
   char *p;
   char filename[GROUPNAMELEN + 32];
   struct group g;

   if (-1 == group_info(newsgroup, &g))
      return -1;
   for (filenum = (g.last > 0 ? g.last / ARTSPERFILE : 1);; filenum++)
   {
      formats(filename, sizeof (filename) - 1, "%s/%d", newsgroup, filenum);
      if (!(sp = getstore(filename)))
         return -1;
      for (slot = 0; slot < ARTSPERFILE; slot++)
         if (0 == sp->file->info[slot].hoffset)
            break;
      if (slot < ARTSPERFILE)
         break;
      lseek(sp->fd, 0, SEEK_SET);
      lockf(sp->fd, F_ULOCK, 0);
      cache_invalidate(desc, sp);
   }

   /* Replace tabs, so art_makexover won't have to */
   
   for (p = ap->head; *p; p++)
      if ('\t' == *p)
         *p = ' ';

   er = (int) (info.hoffset = lseek(sp->fd, 0, SEEK_END));
   if (er > -1)
   {
      struct iovec iov[4];
      char *zbuf;

      info.hlen = ap->hlen + 2;
      info.boffset = info.hoffset + ap->hlen + 2;
      info.blen = ap->blen + 2;
      iov[0].iov_base = "";
      iov[0].iov_len = 1;
      iov[1].iov_base = ap->head;
      iov[1].iov_len = ap->hlen + 1;
      iov[2].iov_base = "";
      iov[2].iov_len = 1;

      zbuf = 0;
      if (ap->body && ap->blen > 0)
      {
         #ifdef USE_ZLIB
         /* Failure here just means don't compress */
         unsigned long zlen;

         if (sp->compressok && ap->blen > sp->compressok)
         {
            zlen = ap->blen + ap->blen / 1000 + 13;
            if ((zbuf = malloc(zlen + COMPRESS_MAGIC_LEN)))
            {
               memcpy(zbuf, COMPRESS_MAGIC, COMPRESS_MAGIC_LEN);
               er = compress((unsigned char *) zbuf + COMPRESS_MAGIC_LEN,
                             &zlen, (unsigned char *) ap->body, (unsigned) ap->blen);
               if (Z_OK != er)
               {
                  LOG1("sto_add:compression failed on %s:%d, \"Z_%s_ERROR\"",
                       newsgroup, (filenum * ARTSPERFILE) + slot,
                       (Z_BUF_ERROR == er) ? "BUF" : ((Z_MEM_ERROR == er) ? "MEM" : "[unknown]"));
                  free(zbuf);
                  zbuf = 0;
               }
               else
                  zbuf[zlen + COMPRESS_MAGIC_LEN] = '\0';
            }
            else
               LOG("sto_add:No memory to compress");
         }
         if (zbuf)
         {
            iov[3].iov_base = zbuf;
            iov[3].iov_len = zlen + 1 + COMPRESS_MAGIC_LEN;
         }
         else
         #endif
         {
            iov[3].iov_base = ap->body;
            iov[3].iov_len = ap->blen + 1;
         }
      }
      else
      {
         iov[3].iov_base = "";
         iov[3].iov_len = 1;
      }
      info.blen = iov[3].iov_len + 1;

      if ((er = nosigio(writev, sp->fd, (char *) iov, 4)) > -1)
      {
         sp->file->info[slot] = info;
         er = (filenum * ARTSPERFILE) + slot;
      }
      else
         LOG("sto_add:writev(%s):%m", sp->filename);
      if (zbuf)
         free(zbuf);
   }
   else
      LOG("sto_add:lseek(%s):%m", sp->filename);

   lseek(sp->fd, 0, SEEK_SET);
   lockf(sp->fd, F_ULOCK, 0);
   if (slot == ARTSPERFILE - 1)
   {
      reorder(sp);
      cache_invalidate(desc, sp);
   }
   return er;
}
