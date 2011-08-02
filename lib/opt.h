/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef OPT_GET_H
#define OPT_GET_H

/*
 * Alternative to getopt(3) for command line parsing.
 * opts is a string consisting of option characters WHICH TAKE AN
 * ARGUMENT.  Returns an option character which caller will have to
 * test if it is recognized, or -1 on end of options.  opt_ind and
 * opt_arg are as for getopt(3).
 * "--" is supported, meaning end of options.  Catted options are
 * ok; if a catted option takes an arg, the arg is assumed to begin
 * immediately after the end of that option character, or is the next
 * argument otherwise.
 */

extern int opt_ind;
extern char *opt_arg;

extern int opt_get (int c, char **v, char *opts);

#endif
