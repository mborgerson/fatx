#include "ext.h"

#include <stdlib.h>
#include <string.h>

/*
 * Copyright (c) 1997 Todd C. Miller <Todd.Miller@courtesan.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* XXX */
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif


char *ext_basename(const char *path)
{
  static char *bname = NULL;
  const char *endp, *startp;

  if (bname == NULL) {
    bname = (char *)malloc(MAXPATHLEN);
    if (bname == NULL)
      return(NULL);
  }

  /* Empty or NULL string gets treated as "." */
  if (path == NULL || *path == '\0') {
    (void)strcpy(bname, ".");
    return(bname);
  }

  /* Strip trailing slashes */
  endp = path + strlen(path) - 1;
  while (endp > path && *endp == '/')
    endp--;

  /* All slashes becomes "/" */
  if (endp == path && *endp == '/') {
    (void)strcpy(bname, "/");
    return(bname);
  }

  /* Find the start of the base */
  startp = endp;
  while (startp > path && *(startp - 1) != '/')
    startp--;

  if (endp - startp + 2 > MAXPATHLEN) {
#if 0
    errno = ENAMETOOLONG;
#endif
    return(NULL);
  }

  (void)strncpy(bname, startp, endp - startp + 1);
  bname[endp - startp + 1] = '\0';
  return(bname);
}


/* memrchr -- find the last occurrence of a byte in a memory block
   Copyright (C) 1991-2018 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Based on strlen implementation by Torbjorn Granlund (tege@sics.se),
   with help from Dan Sahlin (dan@sics.se) and
   commentary by Jim Blandy (jimb@ai.mit.edu);
   adaptation to memchr suggested by Dick Karpinski (dick@cca.ucsf.edu),
   and implemented by Roland McGrath (roland@ai.mit.edu).

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include <stdlib.h>

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined _LIBC
# include <string.h>
# include <memcopy.h>
#endif

#if defined HAVE_LIMITS_H || defined _LIBC
# include <limits.h>
#endif

#define LONG_MAX_32_BITS 2147483647

#ifndef LONG_MAX
# define LONG_MAX LONG_MAX_32_BITS
#endif

// #include <sys/types.h>

#undef __memrchr
#undef memrchr

#ifndef weak_alias
# define __memrchr memrchr
#endif

/* Search no more than N bytes of S for C.  */
static
void *
#ifndef MEMRCHR
__memrchr
#else
MEMRCHR
#endif
     (const void *s, int c_in, size_t n)
{
  const unsigned char *char_ptr;
  const unsigned long int *longword_ptr;
  unsigned long int longword, magic_bits, charmask;
  unsigned char c;

  c = (unsigned char) c_in;

  /* Handle the last few characters by reading one character at a time.
     Do this until CHAR_PTR is aligned on a longword boundary.  */
  for (char_ptr = (const unsigned char *) s + n;
       n > 0 && ((unsigned long int) char_ptr
     & (sizeof (longword) - 1)) != 0;
       --n)
    if (*--char_ptr == c)
      return (void *) char_ptr;

  /* All these elucidatory comments refer to 4-byte longwords,
     but the theory applies equally well to 8-byte longwords.  */

  longword_ptr = (const unsigned long int *) char_ptr;

  /* Bits 31, 24, 16, and 8 of this number are zero.  Call these bits
     the "holes."  Note that there is a hole just to the left of
     each byte, with an extra at the end:

     bits:  01111110 11111110 11111110 11111111
     bytes: AAAAAAAA BBBBBBBB CCCCCCCC DDDDDDDD

     The 1-bits make sure that carries propagate to the next 0-bit.
     The 0-bits provide holes for carries to fall into.  */
  magic_bits = -1;
  magic_bits = magic_bits / 0xff * 0xfe << 1 >> 1 | 1;

  /* Set up a longword, each of whose bytes is C.  */
  charmask = c | (c << 8);
  charmask |= charmask << 16;
#if LONG_MAX > LONG_MAX_32_BITS
  charmask |= charmask << 32;
#endif

  /* Instead of the traditional loop which tests each character,
     we will test a longword at a time.  The tricky part is testing
     if *any of the four* bytes in the longword in question are zero.  */
  while (n >= sizeof (longword))
    {
      /* We tentatively exit the loop if adding MAGIC_BITS to
   LONGWORD fails to change any of the hole bits of LONGWORD.

   1) Is this safe?  Will it catch all the zero bytes?
   Suppose there is a byte with all zeros.  Any carry bits
   propagating from its left will fall into the hole at its
   least significant bit and stop.  Since there will be no
   carry from its most significant bit, the LSB of the
   byte to the left will be unchanged, and the zero will be
   detected.

   2) Is this worthwhile?  Will it ignore everything except
   zero bytes?  Suppose every byte of LONGWORD has a bit set
   somewhere.  There will be a carry into bit 8.  If bit 8
   is set, this will carry into bit 16.  If bit 8 is clear,
   one of bits 9-15 must be set, so there will be a carry
   into bit 16.  Similarly, there will be a carry into bit
   24.  If one of bits 24-30 is set, there will be a carry
   into bit 31, so all of the hole bits will be changed.

   The one misfire occurs when bits 24-30 are clear and bit
   31 is set; in this case, the hole at bit 31 is not
   changed.  If we had access to the processor carry flag,
   we could close this loophole by putting the fourth hole
   at bit 32!

   So it ignores everything except 128's, when they're aligned
   properly.

   3) But wait!  Aren't we looking for C, not zero?
   Good point.  So what we do is XOR LONGWORD with a longword,
   each of whose bytes is C.  This turns each byte that is C
   into a zero.  */

      longword = *--longword_ptr ^ charmask;

      /* Add MAGIC_BITS to LONGWORD.  */
      if ((((longword + magic_bits)

      /* Set those bits that were unchanged by the addition.  */
      ^ ~longword)

     /* Look at only the hole bits.  If any of the hole bits
        are unchanged, most likely one of the bytes was a
        zero.  */
     & ~magic_bits) != 0)
  {
    /* Which of the bytes was C?  If none of them were, it was
       a misfire; continue the search.  */

    const unsigned char *cp = (const unsigned char *) longword_ptr;

#if LONG_MAX > 2147483647
    if (cp[7] == c)
      return (void *) &cp[7];
    if (cp[6] == c)
      return (void *) &cp[6];
    if (cp[5] == c)
      return (void *) &cp[5];
    if (cp[4] == c)
      return (void *) &cp[4];
#endif
    if (cp[3] == c)
      return (void *) &cp[3];
    if (cp[2] == c)
      return (void *) &cp[2];
    if (cp[1] == c)
      return (void *) &cp[1];
    if (cp[0] == c)
      return (void *) cp;
  }

      n -= sizeof (longword);
    }

  char_ptr = (const unsigned char *) longword_ptr;

  while (n-- > 0)
    {
      if (*--char_ptr == c)
  return (void *) char_ptr;
    }

  return 0;
}
#ifndef MEMRCHR
# ifdef weak_alias
weak_alias (__memrchr, memrchr)
# endif
#endif




/* dirname - return directory part of PATH.
   Copyright (C) 1996-2019 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1996.
   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.
   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.
   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */
char *ext_dirname (char *path)
{
  static const char dot[] = ".";
  char *last_slash;
  /* Find last '/'.  */
  last_slash = path != NULL ? strrchr (path, '/') : NULL;
  if (last_slash != NULL && last_slash != path && last_slash[1] == '\0')
    {
      /* Determine whether all remaining characters are slashes.  */
      char *runp;
      for (runp = last_slash; runp != path; --runp)
        if (runp[-1] != '/')
          break;
      /* The '/' is the last character, we have to look further.  */
      if (runp != path)
        last_slash = __memrchr (path, '/', runp - path);
    }
  if (last_slash != NULL)
    {
      /* Determine whether all remaining characters are slashes.  */
      char *runp;
      for (runp = last_slash; runp != path; --runp)
        if (runp[-1] != '/')
          break;
      /* Terminate the path.  */
      if (runp == path)
        {
          /* The last slash is the first character in the string.  We have to
             return "/".  As a special case we have to return "//" if there
             are exactly two slashes at the beginning of the string.  See
             XBD 4.10 Path Name Resolution for more information.  */
          if (last_slash == path + 1)
            ++last_slash;
          else
            last_slash = path + 1;
        }
      else
        last_slash = runp;
      last_slash[0] = '\0';
    }
  else
    /* This assignment is ill-designed but the XPG specs require to
       return a string containing "." in any case no directory part is
       found and so a static and constant string is required.  */
    path = (char *) dot;
  return path;
}
