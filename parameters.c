/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <out.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include "config.h"

static const char ver_ctrl_id[] = "$Id: parameters.c 29 2004-04-24 23:02:38Z patrik $";

char *snroot = SNROOT;
uid_t snuid;
gid_t sngid;

void parameters (bool wantwriteperms)
{
   struct stat st;

   if (!(snroot = getenv("SNROOT")))
      snroot = SNROOT;

   if (-1 == stat(snroot, &st))
      fail(2, "Can't find spool directory \"%s\":%m", snroot);
   if (!S_ISDIR(st.st_mode))
      fail(2, "\"%s\" is not a directory", snroot);
   snuid = st.st_uid;
   sngid = st.st_gid;
   if (0 == geteuid())
   {
      setgid(sngid);
      setuid(snuid);
   }

   if (wantwriteperms)
      if (snuid != geteuid() || sngid != getegid())
      {
         struct passwd *pwuid;
         struct group *grgid;
         char euid_name[40], egid_name[40];
         char snuid_name[40], sngid_name[40];
         
         pwuid = getpwuid(geteuid());
         grgid = getgrgid(getegid());
         strncpy(euid_name, (pwuid ? pwuid->pw_name : "(unknown)"), 40);
         strncpy(egid_name, (grgid ? grgid->gr_name : "(unknown)"), 40);
         pwuid = getpwuid(snuid);
         grgid = getgrgid(sngid);
         strncpy(snuid_name, (pwuid ? pwuid->pw_name : "(unknown)"), 40);
         strncpy(sngid_name, (grgid ? grgid->gr_name : "(unknown)"), 40);
         
         fail(2, "Can't write in spool directory \"%s\" (I am %s:%s, must be %s:%s)",
              snroot, euid_name, egid_name, snuid_name, sngid_name);
      }
}
