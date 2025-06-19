/* Copyright (C) 2005, 2007, 2008, 2009 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@redhat.com>.

   This file is part of the GNU OpenMP Library (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

/* This file contains prototypes of functions in the external ABI.
   This file is included by files in the testsuite.  */

#ifndef LIBGOMP_G_H 
#define LIBGOMP_G_H 1

#include <stdbool.h>

/* barrier.c */

void hgGOMP_barrier (void);

/* critical.c */

void  hgGOMP_critical_start (void);
void  hgGOMP_critical_end (void);
void  hgGOMP_critical_name_start (void **);
void  hgGOMP_critical_name_end (void **);
void  hgGOMP_atomic_start (void);
void  hgGOMP_atomic_end (void);

/* loop.c */

bool  hgGOMP_loop_static_start (long, long, long, long, long *, long *);
bool  hgGOMP_loop_dynamic_start (long, long, long, long, long *, long *);
bool  hgGOMP_loop_guided_start (long, long, long, long, long *, long *);
bool  hgGOMP_loop_runtime_start (long, long, long, long *, long *);

bool  hgGOMP_loop_ordered_static_start (long, long, long, long,
					    long *, long *);
bool  hgGOMP_loop_ordered_dynamic_start (long, long, long, long,
					     long *, long *);
bool  hgGOMP_loop_ordered_guided_start (long, long, long, long,
					    long *, long *);
bool  hgGOMP_loop_ordered_runtime_start (long, long, long, long *, long *);

bool  hgGOMP_loop_static_next (long *, long *);
bool  hgGOMP_loop_dynamic_next (long *, long *);
bool  hgGOMP_loop_guided_next (long *, long *);
bool  hgGOMP_loop_runtime_next (long *, long *);

bool  hgGOMP_loop_ordered_static_next (long *, long *);
bool  hgGOMP_loop_ordered_dynamic_next (long *, long *);
bool  hgGOMP_loop_ordered_guided_next (long *, long *);
bool  hgGOMP_loop_ordered_runtime_next (long *, long *);

void  hgGOMP_parallel_loop_static_start (void (*)(void *), void *,
					     unsigned, long, long, long, long);
void  hgGOMP_parallel_loop_dynamic_start (void (*)(void *), void *,
					     unsigned, long, long, long, long);
void  hgGOMP_parallel_loop_guided_start (void (*)(void *), void *,
					     unsigned, long, long, long, long);
void  hgGOMP_parallel_loop_runtime_start (void (*)(void *), void *,
					      unsigned, long, long, long);

void  hgGOMP_loop_end (void);
void  hgGOMP_loop_end_nowait (void);

/* loop_ull.c */

bool  hgGOMP_loop_ull_static_start (bool, unsigned long long,
					unsigned long long,
					unsigned long long,
					unsigned long long,
					unsigned long long *,
					unsigned long long *);
bool  hgGOMP_loop_ull_dynamic_start (bool, unsigned long long,
					 unsigned long long,
					 unsigned long long,
					 unsigned long long,
					 unsigned long long *,
					 unsigned long long *);
bool  hgGOMP_loop_ull_guided_start (bool, unsigned long long,
					unsigned long long,
					unsigned long long,
					unsigned long long,
					unsigned long long *,
					unsigned long long *);
bool  hgGOMP_loop_ull_runtime_start (bool, unsigned long long,
					 unsigned long long,
					 unsigned long long,
					 unsigned long long *,
					 unsigned long long *);

bool  hgGOMP_loop_ull_ordered_static_start (bool, unsigned long long,
						unsigned long long,
						unsigned long long,
						unsigned long long,
						unsigned long long *,
						unsigned long long *);
bool  hgGOMP_loop_ull_ordered_dynamic_start (bool, unsigned long long,
						 unsigned long long,
						 unsigned long long,
						 unsigned long long,
						 unsigned long long *,
						 unsigned long long *);
bool  hgGOMP_loop_ull_ordered_guided_start (bool, unsigned long long,
						unsigned long long,
						unsigned long long,
						unsigned long long,
						unsigned long long *,
						unsigned long long *);
bool  hgGOMP_loop_ull_ordered_runtime_start (bool, unsigned long long,
						 unsigned long long,
						 unsigned long long,
						 unsigned long long *,
						 unsigned long long *);

bool  hgGOMP_loop_ull_static_next (unsigned long long *,
				       unsigned long long *);
bool  hgGOMP_loop_ull_dynamic_next (unsigned long long *,
					unsigned long long *);
bool  hgGOMP_loop_ull_guided_next (unsigned long long *,
				       unsigned long long *);
bool  hgGOMP_loop_ull_runtime_next (unsigned long long *,
					unsigned long long *);

bool  hgGOMP_loop_ull_ordered_static_next (unsigned long long *,
					       unsigned long long *);
bool  hgGOMP_loop_ull_ordered_dynamic_next (unsigned long long *,
						unsigned long long *);
bool  hgGOMP_loop_ull_ordered_guided_next (unsigned long long *,
					       unsigned long long *);
bool  hgGOMP_loop_ull_ordered_runtime_next (unsigned long long *,
						unsigned long long *);

/* ordered.c */

void  hgGOMP_ordered_start (void);
void  hgGOMP_ordered_end (void);

/* parallel.c */

void  hgGOMP_parallel_start (void (*) (void *), void *, unsigned);
void  hgGOMP_parallel_end (void);

/* team.c */

void  hgGOMP_task (void (*) (void *), void *, void (*) (void *, void *),
		       long, long, bool, unsigned);
void  hgGOMP_taskwait (void);

/* sections.c */

unsigned  hgGOMP_sections_start (unsigned);
unsigned  hgGOMP_sections_next (void);
void  hgGOMP_parallel_sections_start (void (*) (void *), void *,
					  unsigned, unsigned);
void  hgGOMP_sections_end (void);
void  hgGOMP_sections_end_nowait (void);

/* single.c */

bool  hgGOMP_single_start (void);
void *hgGOMP_single_copy_start (void);
void  hgGOMP_single_copy_end (void *);

#endif /* LIBGOMP_G_H */
