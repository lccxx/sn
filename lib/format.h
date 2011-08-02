/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef FORMAT_H
#define FORMAT_H

#include <stdarg.h>

/*
 * Formatting like printf(3).  Not all format specs are supported,
 * in particular, no field specs allowed.
 */

/* Print integer into tmp, returns pointer to start of it */

extern char *istr (int i, int base, char *tmp);
extern char *uistr (unsigned int u, int base, char *tmp);



/* 2 forms, like snprintf */

extern int formatv (char *buf, int size, char *fmt, va_list ap);
extern int formats (char *buf, int size, char *fmt, ...);

/* "Returns" (in p) string based on format character c. Length of string,
which may NOT be null terminated, is in len.
 
This is now a macro because it uses va_arg and you can only use that in a
single function call. Macro-ized by Chris Niekel <chris@niekel.net> */

#define vachar(c, ap, len) \
   { \
      int e, i; \
      unsigned u; \
      p = 0; \
      switch(c) \
      { \
         case 'S': len = va_arg(ap, int); p = va_arg(ap, char *); break; \
         case 's': p = va_arg(ap, char *); len = strlen(p); break; \
         case 'i': /* fall through */ \
         case 'd': i = va_arg(ap, int); p = istr(i, 10, tmp); len = strlen(p); break; \
         case 'u': u = va_arg(ap, unsigned); p = uistr(u, 10, tmp); len = strlen(p); break; \
         case 'o': u = va_arg(ap, unsigned); p = uistr(u, 8, tmp); len = strlen(p); break; \
         case 'x': u = va_arg(ap, unsigned); p = uistr(u, 16, tmp); len = strlen(p); break; \
         case 'm': p = strerror(errno); len = strlen(p); break; \
         case '%': len = 1; p = "%"; break; \
         default: len = 0; p = ""; \
      } \
   }

#endif
