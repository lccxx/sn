/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef BODY_H
#define BODY_H

#ifndef USE_ZLIB

static const char body_[] = "$Compiled: without ZLIB $";
#define body(foo,bar) 0

#else

#include <zconf.h>
#include <zlib.h>

static const char body_[] = "$Compiled: with ZLIB_VERSION=" ZLIB_VERSION " $";
extern int body (char **artbod, int *len);

#endif /* USE_ZLIB */

#endif
