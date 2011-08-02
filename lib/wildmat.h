/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef WILDMAT_H
#define WILDMAT_H

/*
 * Case-insensitive globbing.  Returns 1 if pattern matches the
 * candidate string entirely, else 0.  Supported glob features are *,
 * ? [] including - as a range.  Leading . is not special.  Newlines
 * are not special.  Please don't send bad glob patterns!
 */

extern int wildmat (char *candidate, char *pattern);

#endif
