/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef OPENF_H
#define OPENF_H

/*
 * Open the file specified by the format, with mode mode and open
 * flags specified by flags.
 */

extern int openf (int mode, int flags, char *fmt, ...);

#endif
