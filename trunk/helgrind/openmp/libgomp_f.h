/* Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Jakub Jelinek <jakub@redhat.com>.

   This file is part of the GNU OpenMP Library (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
   more details.

   You should have received a copy of the GNU Lesser General Public License
   along with libgomp; see the file COPYING.LIB.  If not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files, some
   of which are compiled with GCC, to produce an executable, this library
   does not by itself cause the resulting executable to be covered by the
   GNU General Public License.  This exception does not however invalidate
   any other reasons why the executable file might be covered by the GNU
   General Public License.  */

/* This file contains prototypes of functions in the external ABI.
   This file is included by files in the testsuite.  */

#ifndef LIBGOMP_F_H
#define LIBGOMP_F_H 1

#include "libgomp.h"

#if (4 == 4) \
    && (4 <= 4)
# define OMP_LOCK_DIRECT
typedef omp_lock_t *omp_lock_arg_t;
# define omp_lock_arg(arg) (arg)
#else
typedef union { omp_lock_t *lock; uint64_t u; } *omp_lock_arg_t;
# define omp_lock_arg(arg) ((arg)->lock)
# endif

#if (8 == 8) \
    && (4 <= 8)
# define OMP_NEST_LOCK_DIRECT
typedef omp_nest_lock_t *omp_nest_lock_arg_t;
# define omp_nest_lock_arg(arg) (arg)
#else
typedef union { omp_nest_lock_t *lock; uint64_t u; } *omp_nest_lock_arg_t;
# define omp_nest_lock_arg(arg) ((arg)->lock)
# endif

static inline void omp_check_defines (void)
{
  char test[(4 != sizeof (omp_lock_t)
	     || 4 != __alignof (omp_lock_t)
	     || 8 != sizeof (omp_nest_lock_t)
	     || 4 != __alignof (omp_nest_lock_t)
	     || 4 != sizeof (*(omp_lock_arg_t) 0)
	     || 8 != sizeof (*(omp_nest_lock_arg_t) 0))
	    ? 0 : 1] __attribute__ ((__unused__));
}

#endif /* LIBGOMP_F_H */
