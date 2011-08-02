/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Work like the script.
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include "config.h"
#include "addr.h"
#include "parameters.h"
#include "valid.h"
#include <out.h>
#include <opt.h>
#include <format.h>

static const char ver_ctrl_id[] = "$Id: snnewgroup.c 36 2004-04-27 16:52:02Z patrik $";

int debug = 0;

void usage (void) { fail(1, "Usage:%s newsgroup [server [port]]", progname); }

void makedir (char *fn)
{
   struct stat st;

   if (0 == stat(fn, &st))
   {
      if (!S_ISDIR(st.st_mode))
         fail(1, "\"%s\" exists but is not a directory, remove it first", fn);
      LOG1("directory \"%s\" already exists, ok", fn);
      return;
   }
   if (ENOENT != errno)
      fail(2, "Weird error on stat(%s):%m", fn);
   LOG1("Creating directory \"%s\"...", fn);
   if (-1 == mkdir(fn, 0755))
      fail(2, "Can't mkdir(%s):%m", fn);
   LOG1("Done");
}

int touch (char *fn)
{
   int fd;

   LOG1("Creating \"%s\" file...", fn);
   fd = open(fn, O_WRONLY | O_CREAT, 0644);
   if (fd > -1)
   {
      LOG1("Done");
   }
   else
      LOG("Can't create %s:%m", fn);
   return fd;
}

int main (int argc, char **argv)
{
   struct stat st;
   char *outgoing;
   char *groupname;
   char *server;
   char *p;
   int port;
   int i;
   bool z;

   progname = ((p = strrchr(argv[0], '/')) ? p + 1 : argv[0]);

   port = 0;
   z = FALSE;
   outgoing = server = 0;
   while ((i = opt_get(argc, argv, "")) > -1)
      switch (i)
      {
         case 'P': log_with_pid(); break;
         case 'd': debug++; break;
         case 'V': version(); _exit(0);
         case 'x': LOG("Option \"x\" no longer supported, ignored"); break;
         case 'z': z = TRUE; break;
         default: usage();
      }
   if (opt_ind == argc)
      usage();

   groupname = argv[opt_ind++];
   if (opt_ind < argc)
      server = argv[opt_ind++];
   if (opt_ind < argc)
      if ((port = strtoul(argv[opt_ind++], &p, 10)) <= 0 || *p)
         fail(1, "Invalid port number");
   if (argc != opt_ind)
      usage();

   if (0 == strcmp(groupname, JUNK_GROUP))
   {
      server = 0;
      port = 0;
   }
   else
   {
      if (!is_valid_name(groupname)) /* cheat */
         fail(1, "Illegal newsgroup name \"%s\"", groupname);
      for (p = groupname; *p; p++)
         if (*p <= 'Z' && *p >= 'A')
            *p += 'a' - 'A';
   }

   if (z)
      LOG("Option \"z\" ignored"
#ifdef USE_ZLIB
          "  Use \"touch %s/%s/.compress\" instead", snroot, groupname
#endif
          );

   umask(0);
   parameters(TRUE);
   if (-1 == chdir(snroot))
   {
      if (ENOENT == errno)
         fail(1, "\"%s\" doesn't exist, create it first", snroot);
      if (ENOTDIR == errno)
         fail(1, "\"%s\" isn't a directory, remove it or set the SNROOT environment variable", snroot);
      fail(2, "Can't chdir to \"%s\":%m", snroot);
   }

   if (0 == stat(groupname, &st))
   {
      if (S_ISDIR(st.st_mode))
         fail(1, "group %s already exists under %s", groupname, snroot);
      fail(1, "Remove file %s/%s first", snroot, groupname);
   }
   else if (ENOENT != errno)
      fail(2, "odd stat error:%m");

   if (server)
   {
      if (!is_valid_name(server)) /* Cheat again */
         fail(1, "\"%s\" doesn't looks like a valid server name", server);
      if (!*server)
         fail(1, "server name not valid");
      if (!port)
         port = 119;
      makedir(".outgoing/");
      i = sizeof ("../.outgoing/") + strlen(server) + 16;
      if (!(outgoing = malloc(i)))
         fail(2, "No memory");
      formats(outgoing, i - 1, "../.outgoing/%s:%d", server, port);
      makedir(outgoing + 3);
   }

   makedir(groupname);

   if (0 == chdir(groupname))
   {
      int fd;

      if ((fd = touch(".serial")) > -1)
      {
         i = write(fd, "0\n", 2);
         close(fd);
         if (2 == i)
         {
            if ((fd = touch(".created")) > -1)
            {
               close(fd);
               if ((fd = touch(".times")) > -1)
               {
                  if (!outgoing || 0 == symlink(outgoing, ".outgoing"))
                  {
                     LOG("Created %s newsgroup \"%s\" under %s",
                         outgoing ? "remote" : "local", groupname, snroot);
                     _exit(0);
                  }
                  else
                     LOG("Can't create symlink:%m");
                  unlink(".times");
               }
               unlink(".created");
            }
         }
         else
            LOG("Can't write \".serial\" file:%m");
         unlink(".serial");
      }
      chdir("..");
      if (-1 == rmdir(groupname))
         LOG("Can't remove \"%s\" directory to clean up:%m", groupname);
   }
   _exit(2);
}
