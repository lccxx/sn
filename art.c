/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * File and article module.  The entry points in art_gimme() and
 * art_gimmenoderef() which return the specified article.
 */

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include "config.h"
#include "art.h"
#include "artfile.h"
#include "cache.h"
#include <b.h>
#include <out.h>
#include <format.h>

static const char ver_ctrl_id[] = "$Id: art.c 29 2004-04-24 23:02:38Z patrik $";

struct fileobj *file_map (char *path)
{
   static size_t pagesize = 0;
   struct fileobj *fp;
   struct stat st;
   int fd;
   int len;

   if (!pagesize)
      pagesize = getpagesize();

   fd = open(path, O_RDONLY);
   if (fd > -1)
   {
      if (fstat(fd, &st) > -1)
      {
         len = strlen(path);
         if ((fp = malloc(sizeof (struct fileobj) + len + 1)))
         {
            fp->size = ((st.st_size / pagesize) + 1) * pagesize; /* oversize */
            fp->map = mmap(0, fp->size, PROT_READ, MAP_SHARED, fd, 0);
            if (fp->map && fp->map != MAP_FAILED)
            {
               if (FILE_MAGIC == *(int *) fp->map)
               {
                  close(fd);
                  fp->path = strcpy((char *) fp + sizeof (struct fileobj), path);
                  return fp;
               }
               else
                  LOG("file_map:%s has bad magic", path);
            }
            else
               LOG("file_map:mmap:%m");
            munmap(fp->map, fp->size);
         }
         else
            LOG("file_map:malloc:%m");
         free(fp);
      }
      else
         LOG("file_map:fstat:%m");
      close(fd);
   }
   else if (ENOENT != errno)
      LOG("file_map:open(%s):%m", path);
   return NULL;
}

static void file_unmap (void *p)
{
   struct fileobj *fp = (struct fileobj *) p;

   munmap(fp->map, fp->size);
   free(fp);
}

static int file_cmp (void *a, void *b)
{
   struct fileobj *x = (struct fileobj *) a;
   struct fileobj *y = (struct fileobj *) b;

   return strcmp(x->path, y->path);
}

static int desc = -1;

static int file_init (void)
{
   desc = cache_init(8, file_cmp, file_unmap, NULL);
   if (-1 == desc)
   {
      LOG("file_init:%m");
      return -1;
   }
   return 0;
}

static struct file *file_gimme (char *name, int *size)
{
   struct fileobj f = { 0, };
   struct fileobj *fp;

   f.path = name;

   if (NULL == (fp = cache_find(desc, &f)))
   {
      if (NULL == (fp = file_map(name)))
         return NULL;
      else
         cache_insert(desc, fp);
   }

   if (size)
      *size = fp->size;
   return ((struct file *) fp->map);
}

/*************************************************************
 Return an article.  Can only request by newsgroup and serial.
 To request by id, first use dh_*() to get the newsgroup and
 serial number.

 Should dereference aliased articles also.
 *************************************************************/

static bool file_initialized = FALSE;

int art_gimmenoderef (char *group, int serial, struct article *ap)
{
   struct file *f;
   struct info info;
   char path[GROUPNAMELEN + 32];
   int filenumber;
   int part;
   int filesize, lim;

   if (serial < 0)
      return -1;
   if (strlen(group) >= GROUPNAMELEN)
      return -1;

   if (!file_initialized)
   {
      if (-1 == file_init())
         return -1;
      else
         file_initialized = TRUE;
   }

   filenumber = serial / ARTSPERFILE;
   part = serial % ARTSPERFILE;

   formats(path, sizeof (path) - 1, "%s/%d", group, filenumber);

   /* Check for extended file, thanks ZB. */
   
   do
   {
      if (!(f = file_gimme(path, &filesize)))
         return -1;
      lim = f->info[part].boffset + f->info[part].blen;
      if (lim <= 0)
      {
         errno = ENOENT;
         return -1;
      }
      if (lim <= filesize)
         break;

      {
         struct fileobj fo;

         fo.path = path;
         cache_invalidate(desc, &fo);
      }

      if (!(f = file_gimme(path, &filesize)))
         return -1;
      lim = f->info[part].boffset + f->info[part].blen;
      if (lim <= 0)
      {
         errno = ENOENT;
         return -1;
      }
      if (lim <= filesize)
         break;
      LOG("art_gimmenoderef:body extends beyond file in %s:%d", group, serial);
      return -1;
   }
   while (0);

   /* Check for corruption. */

   info = f->info[part];
   if (info.hoffset <= 0 || info.boffset <= 0)
   {
      if (0 == info.hoffset)
      { /* unused */
         if (info.boffset)
            LOG("art_gimmenoderef:corrupted index in %s:%d", group, serial);
      }
      else if (info.hoffset < 0) /* cancelled */
         if (info.boffset >= 0)
            LOG("art_gimmenoderef:bad cancel in %s:%d", group, serial);
      return -1;
   }
   if (info.blen < 2 || info.hlen <= 11)
   {
      LOG("art_gimmenoderef:Corrupted index in %s:%d", group, serial);
      return -1;
   }

   ap->head = (char *) f + info.hoffset + 1;
   ap->hlen = info.hlen - 2;
   if (ap->head[-1] /* || ap->head[ap->hlen] */ )
   {
      LOG("art_gimmenoderef:Bad header in %s:%d", group, serial);
      return -1;
   }

   ap->body = (char *) f + f->info[part].boffset + 1;
   ap->blen = f->info[part].blen - 2;

   return 0;
}

int art_gimme (char *group, int serial, struct article *ap)
{
   int deref;
   char *g;
   int s;

   g = group;
   s = serial;
   for (deref = 0; ; deref++)
   {
      static char ngroup[GROUPNAMELEN + 32];
      char *p;
      char *q;
      char *lim;
      int c;

      if (deref >= 10)
      {
         LOG1("Too many aliases under %s:%d", g, s);
         return -1;
      }
      if (-1 == art_gimmenoderef(group, serial, ap))
         return -1;
      if (-1 == ap->blen)
         return -1; /* canceled */
      /*
       * If its an alias, the body will be empty and the
       * head will contain only one field:
       * Message-ID: news.group:serial<messageid>\r\n\0
       * We ignore the messageid part.
       */
      if (ap->blen > 0 && strncmp(ap->head, "Message-ID:", 11))
         return 0; /* found */
      /* is alias */
      for (p = ap->head + 11; ' ' == *p; p++) ;
      lim = ngroup + sizeof (ngroup);
      for (q = ngroup; (*q = *p) != ':' && q < lim; q++, p++) ;
      if (':' != *p++)
         break;
      *q = '\0';
      for (serial = 0;; serial *= 10, serial += c, p++)
      {
         c = *p - '0';
         if (c > 9 || c < 0)
            break;
      }
      if ('<' != *p)
         break;
      group = ngroup; /* serial already set */
   }
   LOG("art_gimme:bad alias under %s:%d", g, s);
   return -1;
}

void art_filecachestat (int *hit, int *miss)
{
   cache_stat(desc, hit, miss);
}

static int getfield (char *buf, struct field *f)
{
   register char *cp = buf;

   cp += strspn(cp, " \t\f");   /* I don't think \r\n is needed here, since sn unfolds all headers */
   if (!*cp)
   {
      f->pointer = NULL;
      return 0;
   }
   f->len = strcspn(cp, "\r\n");
   if (!f->len)
      return 0;
   f->pointer = cp;
   return ((cp + f->len) - buf);
}

/*
 * The "xref" field is not filled. Been removed! --harold
 */

#define CMP(string) strncasecmp(cp, string, sizeof(string) - 1)

int art_makexover (struct article *ap, struct xover *xp)
{
   struct xover x = { {0,}, };
   char *cp;

   if (NULL == ap || NULL == ap->head || NULL == xp)
      return -1;

   cp = ap->head;

   goto start;

   while (*cp)
   {
      if ('\n' == *cp && cp[1])
      {
         cp++;
start:
         switch (*cp)
         {
            case 's': case 'S':
               if (!x.subject.pointer && 0 == CMP("subject:"))
                  cp += getfield(cp + sizeof ("subject:") - 1, &x.subject);
               break;
            case 'f': case 'F':
               if (!x.from.pointer && 0 == CMP("from:"))
                  cp += getfield(cp + sizeof ("from:") - 1, &x.from);
               break;
            case 'd': case 'D':
               if (!x.date.pointer && 0 == CMP("date:"))
                  cp += getfield(cp + sizeof ("date:") - 1, &x.date);
               break;
            case 'm': case 'M':
               if (!x.messageid.pointer && 0 == CMP("message-id:"))
                  cp += getfield(cp + sizeof ("message-id:") - 1, &x.messageid);
               break;
            case 'r': case 'R':
               if (!x.references.pointer && 0 == CMP("references:"))
                  cp += getfield(cp + sizeof ("references:") - 1, &x.references);
               break;
            case 'b': case 'B':
               if (!x.bytes.pointer && 0 == CMP("bytes:"))
                  cp += getfield(cp + sizeof ("bytes:") - 1, &x.bytes);
               break;
            case 'l': case 'L':
               if (!x.lines.pointer && 0 == CMP("lines:"))
                  cp += getfield(cp + sizeof ("lines:") - 1, &x.lines);
               break;
            default:
               ;
         }
      }
      cp++;
   }

   *xp = x;
   return 0;
}

char *art_findfield (char *head, char *fieldname)
{
   int len;
   char *cp = head;
   static struct b b = { 0, };
   char c;

   len = strlen(fieldname);
   b.used = 0;
   c = *fieldname & (~040);

   goto enter;

   for (; *cp; cp++)
   {
      if ('\n' != *cp)
         continue;
      cp++;
enter:
      if ((*cp & (~040)) == c && 0 == strncasecmp(cp, fieldname, len))
      {
         char *start = cp + len;

         if (':' != *start)
            continue;
         start++;
         start += strspn(start, " \t\r\n\f");
         for (cp = start; *cp && '\r' != *cp; cp++) ;
         b_appendl(&b, start, cp - start);
         return b.buf;
      }
   }
   return "";
}
