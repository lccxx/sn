/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Expire old news articles in a newsgroup.  Expiration is per file,
 * so ARTSPERFILE articles are deleted at a time.
 *
 * snexpire [-exp] newsgroup [[-exp] newsgroup] ...
 * If exp is not provided, uses the expiration time in that groups
 * .expire file.
 * The format is: #[hdwmy] where h is hours, d is days, w is weeks,
 * m is months, and y is years.
 * so 1w is 1 week.  1 week is the default if nothing is mentioned.
 */

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <limits.h>
#include "config.h"
#include "art.h"
#include "group.h"
#include "dhash.h"
#include "times.h"
#include "parameters.h"
#include "valid.h"
#include <out.h>
#include <openf.h>
#include <format.h>
#include <opt.h>

static const char ver_ctrl_id[] = "$Id: snexpire.c 29 2004-04-24 23:02:38Z patrik $";

int debug = 0;
bool report = FALSE;

time_t parseexp (char *buf)
{
   double exp;
   char *unit;
   
   if (buf == NULL)
      return -1;
   
   exp = strtod(buf, &unit);
   if (errno == ERANGE)
      return -1;
   
   switch (*unit)
   {
      case 'y':
         exp *= 12.0;
      case 'm':
         exp *= 4.345;
      case 'w':
         exp *= 7.0;
      case 'd':
         exp *= 24.0;
      case 'h':
         exp *= 60.0;
         exp *= 60.0;
         if (unit[1])
      case '\0': default:
            return -1;
   }
   
   return ((time_t) exp);
}

static time_t readexp (char *newsgroup)
{
   char buf[32];
   int fd;
   int count;
   time_t t;

   if ((fd = openf(0, O_RDONLY, "%s/.expire", newsgroup)) > -1)
   {
      if ((count = read(fd, buf, sizeof (buf) - 1)) > -1)
      {
         for (buf[count--] = '\0'; count > 0; count--)
            if (buf[count] <= ' ')
               buf[count] = '\0';
            else
               break;
         if ((t = parseexp(buf)) > -1)
         {
            close(fd);
            return t;
         }
         LOG("bad format in %s/.expire", newsgroup);
      }
      close(fd);
   }
   return DEFAULT_EXP;
}

/*
 * Always leave at least one file behind, so we know where the
 * article numbers start/end.
 */

static int expire (char *newsgroup, time_t age)
{
   char buf[GROUPNAMELEN + sizeof ("/12345678901234")];
   int maxfile, minfile;
   int file, serial, i;
   DIR *dir;
   struct dirent *dp;
   int tmp;
   char *end;
   
   LOG3("expire:group: %s, max age: %d hours", newsgroup, (age / 60 / 60));
   
   if ((dir = opendir(newsgroup)) == NULL)
   {
      LOG("expire:opendir(%s):%m", newsgroup);
      return 0;
   }
   for (maxfile = -1, minfile = INT_MAX; (dp = readdir(dir)); )
      if (*dp->d_name > '0' && *dp->d_name <= '9')
         if ((tmp = strtoul(dp->d_name, &end, 10)) > 0 && !*end)
         {
            if (tmp > maxfile)
               maxfile = tmp;
            if (tmp < minfile)
               minfile = tmp;
         }
   closedir(dir);
   if (maxfile == -1)
   {
      LOG1("expire:group \"%s\" is empty", newsgroup);
      return 0;
   }

   if (age)
      switch (serial = times_since(newsgroup, time(NULL) - age))
      {
         case -1:
            LOG("times_since(%s):%m", newsgroup);
            return -1;
         case 1: /* everything is too young */
            return 0;
         case 0: /* everything needs to be expired */
            maxfile--; /* but leave one file behind */
            break;
         default:
            maxfile = serial / ARTSPERFILE;
            if (serial % ARTSPERFILE != ARTSPERFILE - 1)
               maxfile--;
            break;
      }

   for (file = maxfile; file >= minfile; file--)
   {
      struct article a;

      for (i = ARTSPERFILE; i > -1; i--)
      {
         char *id;

         serial = (file * ARTSPERFILE) + i;
         if (-1 == art_gimmenoderef(newsgroup, serial, &a))
            continue;
         id = art_findfield(a.head, "message-id");
         if (id && *id)
         {
            struct data d;
            char *cp;

            if ('<' == *id)
               id++;
            if ((cp = strchr(id, '>')))
               *cp = '\0';
            d.messageid = id;
            if (0 == dh_find(&d, FALSE))
               if (0 == strcmp(d.newsgroup, newsgroup))
                  if (d.serial == serial)
                     if (-1 == dh_delete(&d))
                        LOG2("expire:can't delete id %s for %s:%d", id, newsgroup, serial);
            if (report)
               writef(1, "%s %d %s\n", newsgroup, serial, id);
         }
         else
            LOG2("expire:Can't find id for %s:%d", newsgroup, serial);
      }
      /* Delete the file */
      formats(buf, sizeof (buf) - 1, "%s/%d", newsgroup, file);
      if (-1 == unlink(buf))
         LOG("expire:unlink(%s):%m", buf);
   }
   times_expire(newsgroup, (maxfile + 1) * ARTSPERFILE);
   return 0;
}

static void usage (void) { fail(1, "Usage:%s [[-exp] newsgroup]...", progname); }

int main (int argc, char **argv)
{
   char *cp;
   int i, errors;

   progname = ((cp = strrchr(argv[0], '/')) ? cp + 1 : argv[0]);

   while ((i = opt_get(argc, argv, "")) > -1)
      switch (i)
      {
         case 'P': log_with_pid(); break;
         case 'd': debug++; break;
         case 'V': version(); _exit(0);
         case 'v': report = TRUE; break;
         case '0': case '1': case '2': case '3': case '4':
         case '5': case '6': case '7': case '8': case '9':
            goto out;
         default: usage();
      }
out:
   parameters(TRUE);

   if (-1 == chdir(snroot))
      FAIL(2, "Can't chdir(%s):%m", snroot);

   do
   {
      if (0 == times_init())
      {
         if (0 == dh_open(NULL, FALSE))
         {
            if (0 == group_init())
               break;
            dh_close();
         }
         times_fin();
      }
      _exit(2);
   }
   while (0);

   errors = 0;

   for (i = opt_ind; i < argc; i++)
   {
      time_t exp;

      if ('-' == *argv[i])
      {
         exp = parseexp(argv[i] + 1);
         if (-1 == exp)
            usage();
         i++;
      }
      else if (-1 == (exp = readexp(argv[i])))
         exp = DEFAULT_EXP;

      if (!is_valid_group(argv[i]) && strcmp(argv[i], JUNK_GROUP))
      {
         LOG("%s is not a valid newsgroup", argv[i]);
         errors++;
      }
      else
         expire(argv[i], exp);
   }
   group_fin();
   times_fin();
   dh_close();
   _exit(errors ? 1 : 0);
}
