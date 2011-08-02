/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */


#ifndef UNFOLD_H
#define UNFOLD_H

struct readln;

extern int unfold (struct readln *in, int (*putline) (char *, int));

#endif /* UNFOLD_H */
