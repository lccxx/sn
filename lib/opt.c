/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * alternative to getopt.  DOES NOT FOLLOW GETOPT STANDARD.
 * opts is string of flag chars for which an argument is expected.
 * opt_ind is current index to argv.  option flag is returned, unless
 * no options are found, then -1 is returned.  If that option flag
 * is in opts (has an associated value), that value is in opt_arg,
 * or NULL if no value was found.  Caller can set opt_ind to start
 * option checking at any point.
 */

static const char ver_ctrl_id[] = "$Id: opt.c 29 2004-04-24 23:02:38Z patrik $";

int opt_ind = 0;
char *opt_arg = 0;
static int opt_char = 0;

int opt_get (int c, char **v, char *opts)
{
   if (0 == opt_ind) opt_ind = 1;

   while (1)
   {
      if (opt_ind >= c)
         goto done;
      if (0 == opt_char)
      {
         if ('-' != v[opt_ind][opt_char])
            goto done;
         opt_char++;
         if ('-' == v[opt_ind][opt_char])
            if (!v[opt_ind][opt_char + 1])
            {
               opt_arg = 0;
               opt_char = 0;
               return -1;
            }
         if (!v[opt_ind][opt_char]) /* bare dash */
            goto noarg;
      }
      if (!v[opt_ind][opt_char])
      {
         opt_ind++;
         opt_char = 0;
         continue;
      }
      break;
   }

   for (; *opts && *opts != v[opt_ind][opt_char]; opts++) ;
   if (!*opts)
      goto noarg;

   if (!v[opt_ind][opt_char + 1]) /* arg not catted with flag */
   {
      if (opt_ind + 1 == c)
      {
         opt_arg = 0;
         return *opts;
      }
      opt_arg = v[opt_ind + 1];
      opt_ind += 2;
   }
   else /* arg catted with flag */
   {
      opt_arg = v[opt_ind] + opt_char + 1;
      opt_ind++;
   }
   opt_char = 0;
   return *opts;

noarg:
   opt_arg = 0;
   return v[opt_ind][opt_char++];
done:
   opt_arg = 0;
   opt_char = 0;
   return -1;
}
