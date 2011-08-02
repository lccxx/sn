/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef CONFIG_H
#define CONFIG_H

/*
 *  Before editing this, edit Makefile for SNROOT.
 */

/*
 * How many articles per file.  10 is good.  The theory is:
 * ARTSPERFILE = (system page size in bytes)/(ave. size of news header)
 * This is also the granularity of expiration; a multiple of this
 * many articles will be expired at a time.  Do NOT make it 1;
 * things will break.
 */

#define ARTSPERFILE 10

/*
 * How large the hash table should be.  Currently this size is
 * static.  Figure about 1000 articles per group, so the following
 * should be good for 10-40 groups.
 */

#define DH_SIZE 10240

/*
 * Default expiration age, in seconds.
 */

#define DEFAULT_EXP 60*60*24*7

/*
 * MAX_POST_AGE is the time in seconds a posting can remain unposted
 * before it is deleted.  Refers to global groups only.
 */

#define MAX_POST_AGE 60*60*24*7

/*
 * Nothing more to edit for general configuration.
 */

#define version() writef(2, "%s version " VERSION /*" %s"*/ "\n", progname/*, rcsid*/)
#define log_with_pid() \
do { char * p; int i; \
  if ((p = malloc(i = strlen(progname) + 32))) \
  formats(p, i-1, "%s[%u]", progname, getpid()); \
  progname = p; } while (0)

/*
 * Alignment on your machine.  For i386, leave at 4.  For others, I
 * don't know.
 */

#define ALLO_ALIGNMENT 4

/*
 * You can stop editing now.
 */

#ifdef __sun__   /* For Solaris */
#define DONT_HAVE_DIRFD
#endif

extern int debug;

#define LOG log_
#define LOG1 if (debug >= 1) log_
#define LOG2 if (debug >= 2) log_
#define LOG3 if (debug >= 3) log_
#define FAIL fail

#include <sys/param.h>

#ifndef NAME_MAX   /* For Solaris */
#define NAME_MAX (MAXNAMELEN - 1)
#endif
#if NAME_MAX > 512
#define GROUPNAMELEN 512
#else
#define GROUPNAMELEN NAME_MAX
#endif

#ifndef MAP_FAILED   /* For Linux libc5 and HP-UX 10 */
#define MAP_FAILED ((void *) -1)
#endif

#define JUNK_GROUP "=junk"

#undef FALSE   /* Mac OS X apparently defines these */
#undef TRUE
typedef enum { FALSE, TRUE } bool;

#endif /* CONFIG_H */
