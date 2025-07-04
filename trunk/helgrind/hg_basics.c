
/*--------------------------------------------------------------------*/
/*--- Basic definitions for all of Helgrind.                       ---*/
/*---                                                  hg_basics.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Helgrind, a Valgrind tool for detecting errors
   in threaded programs.

   Copyright (C) 2007-2008 OpenWorks Ltd
      info@open-works.co.uk

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "pub_tool_basics.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_threadstate.h"

#include "hg_basics.h"            /* self */

#include "hg_logging.h"

/*----------------------------------------------------------------*/
/*--- Very basic stuff                                         ---*/
/*----------------------------------------------------------------*/

void* HG_(zalloc) ( HChar* cc, SizeT n )
{
   void* p;
   tl_assert(n > 0);
   p = VG_(malloc)( cc, n );
   tl_assert(p);
   VG_(memset)(p, 0, n);
#ifndef NDEBUG
   //tl_assert2(cc&&cc[0]&&cc[1]&&cc[2],"The cc parameter must have some signification\n");
#endif
#ifdef LOGGING_MALLOC
   HG_(debugPostMalloc) ( p, cc, n );
#endif
   return p;
}

void HG_(free) ( void* p )
{
   tl_assert(p);
#ifdef LOGGING_MALLOC
   HG_(debugPreFree) ( p );
#endif
   VG_(free)(p);
}

Char* HG_(strdup) ( HChar* cc, const Char* s )
{
   return VG_(strdup)( cc, s );
}


/*----------------------------------------------------------------*/
/*--- Command line options                                     ---*/
/*----------------------------------------------------------------*/

/* Description of these flags is in hg_basics.h. */

Bool  HG_(clo_track_lockorders) = True;

Bool  HG_(clo_cmp_race_err_addrs) = False;

Bool  HG_(clo_show_conflicts) = True;

UWord HG_(clo_conflict_cache_size) = 1000000;

Word  HG_(clo_sanity_flags) = 0;


/*--------------------------------------------------------------------*/
/*--- end                                              hg_basics.c ---*/
/*--------------------------------------------------------------------*/
