
/*--------------------------------------------------------------------*/
/*--- Format-neutral storage of and querying of info acquired from ---*/
/*--- ELF/XCOFF stabs/dwarf1/dwarf2 debug info.                    ---*/
/*---                                               priv_storage.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2000-2008 Julian Seward 
      jseward@acm.org

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
/*
   Stabs reader greatly improved by Nick Nethercote, Apr 02.
   This module was also extensively hacked on by Jeremy Fitzhardinge
   and Tom Hughes.
*/
/* See comment at top of debuginfo.c for explanation of
   the _svma / _avma / _image / _bias naming scheme.
*/
/* Note this is not freestanding; needs pub_core_xarray.h and
   priv_tytypes.h to be included before it. */

#ifndef __PRIV_STORAGE_H
#define __PRIV_STORAGE_H

/* --------------------- SYMBOLS --------------------- */

/* A structure to hold an ELF/XCOFF symbol (very crudely). */
typedef 
   struct { 
      Addr  addr;   /* lowest address of entity */
      Addr  tocptr; /* ppc64-linux only: value that R2 should have */
      UChar *name;  /* name */
      UInt  size;   /* size in bytes */
      Bool  isText;
   }
   DiSym;

/* --------------------- RELOCS  --------------------- */

/* A structure to hold a relocation entry. */
typedef
   struct {
      Addr  addr;   /* lowest address of entity */
      UChar *name;  /* name */
   }
   DiRel;

/* --------------------- SRCLOCS --------------------- */

/* Line count at which overflow happens, due to line numbers being
   stored as shorts in `struct nlist' in a.out.h. */
#define LINENO_OVERFLOW (1 << (sizeof(short) * 8))

#define LINENO_BITS     20
#define LOC_SIZE_BITS  (32 - LINENO_BITS)
#define MAX_LINENO     ((1 << LINENO_BITS) - 1)

/* Unlikely to have any lines with instruction ranges > 4096 bytes */
#define MAX_LOC_SIZE   ((1 << LOC_SIZE_BITS) - 1)

/* Number used to detect line number overflows; if one line is
   60000-odd smaller than the previous, it was probably an overflow.
 */
#define OVERFLOW_DIFFERENCE     (LINENO_OVERFLOW - 5000)

/* A structure to hold addr-to-source info for a single line.  There
  can be a lot of these, hence the dense packing. */
typedef
   struct {
      /* Word 1 */
      Addr   addr;               /* lowest address for this line */
      /* Word 2 */
      UShort size:LOC_SIZE_BITS; /* # bytes; we catch overflows of this */
      UInt   lineno:LINENO_BITS; /* source line number, or zero */
      /* Word 3 */
      UChar*  filename;          /* source filename */
      /* Word 4 */
      UChar*  dirname;           /* source directory name */
   }
   DiLoc;

/* --------------------- CF INFO --------------------- */

/* A structure to summarise DWARF2/3 CFA info for the code address
   range [base .. base+len-1].  In short, if you know (sp,fp,ip) at
   some point and ip is in the range [base .. base+len-1], it tells
   you how to calculate (sp,fp) for the caller of the current frame
   and also ra, the return address of the current frame.

   First off, calculate CFA, the Canonical Frame Address, thusly:

     cfa = case cfa_how of
              CFIC_SPREL -> sp + cfa_off
              CFIC_FPREL -> fp + cfa_off
              CFIR_EXPR  -> expr whose index is in cfa_off

   Once that is done, the previous frame's sp/fp values and this
   frame's ra value can be calculated like this:

      old_sp/fp/ra
         = case sp/fp/ra_how of
              CFIR_UNKNOWN   -> we don't know, sorry
              CFIR_SAME      -> same as it was before (sp/fp only)
              CFIR_CFAREL    -> cfa + sp/fp/ra_off
              CFIR_MEMCFAREL -> *( cfa + sp/fp/ra_off )
              CFIR_EXPR      -> expr whose index is in sp/fp/ra_off
*/

#define CFIC_SPREL     ((UChar)1)
#define CFIC_FPREL     ((UChar)2)
#define CFIC_EXPR      ((UChar)3)

#define CFIR_UNKNOWN   ((UChar)4)
#define CFIR_SAME      ((UChar)5)
#define CFIR_CFAREL    ((UChar)6)
#define CFIR_MEMCFAREL ((UChar)7)
#define CFIR_EXPR      ((UChar)8)

typedef
   struct {
      Addr  base;
      UInt  len;
      UChar cfa_how; /* a CFIC_ value */
      UChar ra_how;  /* a CFIR_ value */
      UChar sp_how;  /* a CFIR_ value */
      UChar fp_how;  /* a CFIR_ value */
      Int   cfa_off;
      Int   ra_off;
      Int   sp_off;
      Int   fp_off;
   }
   DiCfSI;


typedef
   enum {
      Cop_Add=0x321,
      Cop_Sub,
      Cop_And,
      Cop_Mul
   }
   CfiOp;

typedef
   enum {
      Creg_SP=0x213,
      Creg_FP,
      Creg_IP
   }
   CfiReg;

typedef
   enum {
      Cex_Undef=0x123,
      Cex_Deref,
      Cex_Const,
      Cex_Binop,
      Cex_CfiReg,
      Cex_DwReg
   }
   CfiExprTag;

typedef 
   struct {
      CfiExprTag tag;
      union {
         struct {
         } Undef;
         struct {
            Int ixAddr;
         } Deref;
         struct {
            UWord con;
         } Const;
         struct {
            CfiOp op;
            Int ixL;
            Int ixR;
         } Binop;
         struct {
            CfiReg reg;
         } CfiReg;
         struct {
            Int reg;
         } DwReg;
      }
      Cex;
   }
   CfiExpr;

extern Int ML_(CfiExpr_Undef) ( XArray* dst );
extern Int ML_(CfiExpr_Deref) ( XArray* dst, Int ixAddr );
extern Int ML_(CfiExpr_Const) ( XArray* dst, UWord con );
extern Int ML_(CfiExpr_Binop) ( XArray* dst, CfiOp op, Int ixL, Int ixR );
extern Int ML_(CfiExpr_CfiReg)( XArray* dst, CfiReg reg );
extern Int ML_(CfiExpr_DwReg) ( XArray* dst, Int reg );

extern void ML_(ppCfiExpr)( XArray* src, Int ix );

/* --------------------- VARIABLES --------------------- */

typedef
   struct {
      Addr    aMin;
      Addr    aMax;
      XArray* /* of DiVariable */ vars;
   }
   DiAddrRange;

typedef
   struct {
      UChar* name;  /* in DebugInfo.strchunks */
      UWord  typeR; /* a cuOff */
      GExpr* gexpr; /* on DebugInfo.gexprs list */
      GExpr* fbGX;  /* SHARED. */
      UChar* fileName; /* where declared; may be NULL. in
                          DebugInfo.strchunks */
      Int    lineNo;   /* where declared; may be zero. */
   }
   DiVariable;

Word 
ML_(cmp_for_DiAddrRange_range) ( const void* keyV, const void* elemV );

/* --------------------- DEBUGINFO --------------------- */

/* This is the top-level data type.  It's a structure which contains
   information pertaining to one mapped ELF object.  This type is
   exported only abstractly - in pub_tool_debuginfo.h. */

#define SEGINFO_STRCHUNKSIZE (64*1024)

struct _DebugInfo {

   /* Admin stuff */

   struct _DebugInfo* next;   /* list of DebugInfos */
   Bool               mark;   /* marked for deletion? */

   /* An abstract handle, which can be used by entities outside of
      m_debuginfo to (in an abstract datatype sense) refer to this
      struct _DebugInfo.  A .handle of zero is invalid; valid handles
      are 1 and above.  The same handle is never issued twice (in any
      given run of Valgrind), so a handle becomes invalid when the
      associated struct _DebugInfo is discarded, and remains invalid
      forever thereafter.  The .handle field is set as soon as this
      structure is allocated. */
   ULong handle;

   /* Used for debugging only - indicate what stuff to dump whilst
      reading stuff into the seginfo.  Are computed as early in the
      lifetime of the DebugInfo as possible -- at the point when it is
      created.  Use these when deciding what to spew out; do not use
      the global VG_(clo_blah) flags. */

   Bool trace_symtab; /* symbols, our style */
   Bool trace_cfi;    /* dwarf frame unwind, our style */
   Bool ddump_syms;   /* mimic /usr/bin/readelf --syms */
   Bool ddump_line;   /* mimic /usr/bin/readelf --debug-dump=line */
   Bool ddump_frames; /* mimic /usr/bin/readelf --debug-dump=frames */

   /* Fields that must be filled in before we can start reading
      anything from the ELF file.  These fields are filled in by
      VG_(di_notify_mmap) and its immediate helpers. */

   UChar* filename; /* in mallocville (VG_AR_DINFO) */
   UChar* memname;  /* also in VG_AR_DINFO.  AIX5 only: .a member name */

   Bool  have_rx_map; /* did we see a r?x mapping yet for the file? */
   Bool  have_rw_map; /* did we see a rw? mapping yet for the file? */

   Addr  rx_map_avma; /* these fields record the file offset, length */
   SizeT rx_map_size; /* and map address of the r?x mapping we believe */
   OffT  rx_map_foff; /* is the .text segment mapping */

   Addr  rw_map_avma; /* ditto, for the rw? mapping we believe is the */
   SizeT rw_map_size; /* .data segment mapping */
   OffT  rw_map_foff;

   /* Once both a rw? and r?x mapping for .filename have been
      observed, we can go on to read the symbol tables and debug info.
      .have_dinfo flags when that has happened. */
   /* If have_dinfo is False, then all fields except "*rx_map*" and
      "*rw_map*" are invalid and should not be consulted. */
   Bool  have_dinfo; /* initially False */

   /* All the rest of the fields in this structure are filled in once
      we have committed to reading the symbols and debug info (that
      is, at the point where .have_dinfo is set to True). */

   /* The file's soname.  FIXME: ensure this is always allocated in
      VG_AR_DINFO. */
   UChar* soname;

   /* Description of some important mapped segments.  The presence or
      absence of the mapping is denoted by the _present field, since
      in some obscure circumstances (to do with data/sdata/bss) it is
      possible for the mapping to be present but have zero size.
      Certainly text_ is mandatory on all platforms; not sure about
      the rest though. 

      Comment_on_IMPORTANT_CFSI_REPRESENTATIONAL_INVARIANTS: we require that
 
      either (rx_map_size == 0 && cfsi == NULL) (the degenerate case)

      or the normal case, which is the AND of the following:
      (0) rx_map_size > 0
      (1) no two DebugInfos with rx_map_size > 0 
          have overlapping [rx_map_avma,+rx_map_size)
      (2) [cfsi_minavma,cfsi_maxavma] does not extend 
          beyond [rx_map_avma,+rx_map_size); that is, the former is a 
          subrange or equal to the latter.
      (3) all DiCfSI in the cfsi array all have ranges that fall within
          [rx_map_avma,+rx_map_size).
      (4) all DiCfSI in the cfsi array are non-overlapping

      The cumulative effect of these restrictions is to ensure that
      all the DiCfSI records in the entire system are non overlapping.
      Hence any address falls into either exactly one DiCfSI record,
      or none.  Hence it is safe to cache the results of searches for
      DiCfSI records.  This is the whole point of these restrictions.
      The caching of DiCfSI searches is done in VG_(use_CF_info).  The
      cache is flushed after any change to debugInfo_list.  DiCfSI
      searches are cached because they are central to stack unwinding
      on amd64-linux.

      Where are these invariants imposed and checked?

      They are checked after a successful read of debuginfo into
      a DebugInfo*, in check_CFSI_related_invariants.

      (1) is not really imposed anywhere.  We simply assume that the
      kernel will not map the text segments from two different objects
      into the same space.  Sounds reasonable.

      (2) follows from (4) and (3).  It is ensured by canonicaliseCFI.
      (3) is ensured by ML_(addDiCfSI).
      (4) is ensured by canonicaliseCFI.
   */
   /* .text */
   Bool   text_present;
   Addr   text_avma;
   Addr   text_svma;
   SizeT  text_size;
   OffT   text_bias;
   /* .data */
   Bool   data_present;
   Addr   data_svma;
   Addr   data_avma;
   SizeT  data_size;
   OffT   data_bias;
   /* .sdata */
   Bool   sdata_present;
   Addr   sdata_svma;
   Addr   sdata_avma;
   SizeT  sdata_size;
   OffT   sdata_bias;
   /* .rodata */
   Bool   rodata_present;
   Addr   rodata_svma;
   Addr   rodata_avma;
   SizeT  rodata_size;
   OffT   rodata_bias;
   /* .bss */
   Bool   bss_present;
   Addr   bss_svma;
   Addr   bss_avma;
   SizeT  bss_size;
   OffT   bss_bias;
   /* .sbss */
   Bool   sbss_present;
   Addr   sbss_svma;
   Addr   sbss_avma;
   SizeT  sbss_size;
   OffT   sbss_bias;
   /* .plt */
   Bool   plt_present;
   Addr	  plt_avma;
   SizeT  plt_size;
   /* .got */
   Bool   got_present;
   Addr   got_avma;
   SizeT  got_size;
   /* .got.plt */
   Bool   gotplt_present;
   Addr   gotplt_avma;
   SizeT  gotplt_size;
   /* .opd -- needed on ppc64-linux for finding symbols */
   Bool   opd_present;
   Addr   opd_avma;
   SizeT  opd_size;
   /* .ehframe -- needed on amd64-linux for stack unwinding */
   Bool   ehframe_present;
   Addr   ehframe_avma;
   SizeT  ehframe_size;

   /* Sorted tables of stuff we snarfed from the file.  This is the
      eventual product of reading the debug info.  All this stuff
      lives in VG_AR_DINFO. */

   /* An expandable array of symbols. */
   DiSym*  symtab;
   UWord   symtab_used;
   UWord   symtab_size;
   /* An expandable array of relocation entries. */
   DiRel*  reltab;
   UInt    reltab_used;
   UInt    reltab_size;
   /* An expandable array of locations. */
   DiLoc*  loctab;
   UWord   loctab_used;
   UWord   loctab_size;
   /* An expandable array of CFI summary info records.  Also includes
      summary address bounds, showing the min and max address covered
      by any of the records, as an aid to fast searching.  And, if the
      records require any expression nodes, they are stored in
      cfsi_exprs. */
   DiCfSI* cfsi;
   UWord   cfsi_used;
   UWord   cfsi_size;
   Addr    cfsi_minavma;
   Addr    cfsi_maxavma;
   XArray* cfsi_exprs; /* XArray of CfiExpr */

   /* Expandable arrays of characters -- the string table.  Pointers
      into this are stable (the arrays are not reallocated). */
   struct strchunk {
      UInt   strtab_used;
      struct strchunk* next;
      UChar  strtab[SEGINFO_STRCHUNKSIZE];
   } *strchunks;

   /* Variable scope information, as harvested from Dwarf3 files.

      In short it's an

         array of (array of PC address ranges and variables)

      The outer array indexes over scopes, with Entry 0 containing
      information on variables which exist for any value of the program
      counter (PC) -- that is, the outermost scope.  Entries 1, 2, 3,
      etc contain information on increasinly deeply nested variables.

      Each inner array is an array of (an address range, and a set
      of variables that are in scope over that address range).  

      The address ranges may not overlap.
 
      Since Entry 0 in the outer array holds information on variables
      that exist for any value of the PC (that is, global vars), it
      follows that Entry 0's inner array can only have one address
      range pair, one that covers the entire address space.
   */
   XArray* /* of OSet of DiAddrRange */varinfo;

   /* These are arrays of the relevant typed objects, held here
      partially for the purposes of visiting each object exactly once
      when we need to delete them. */

   /* An array of TyEnts.  These are needed to make sense of any types
      in the .varinfo.  Also, when deleting this DebugInfo, we must
      first traverse this array and throw away malloc'd stuff hanging
      off it -- by calling ML_(TyEnt__make_EMPTY) on each entry. */
   XArray* /* of TyEnt */ admin_tyents;

   /* An array of guarded DWARF3 expressions. */
   XArray* admin_gexprs;
};

/* --------------------- functions --------------------- */

/* ------ Adding ------ */

/* Add a symbol to si's symbol table. */
extern void ML_(addSym) ( struct _DebugInfo* di, DiSym* sym );

/* Add a symbol to si's relocation table. */
extern void ML_(addRel) ( struct _DebugInfo* di, DiRel* rel );

/* Add a line-number record to a DebugInfo. */
extern
void ML_(addLineInfo) ( struct _DebugInfo* di, 
                        UChar*   filename, 
                        UChar*   dirname,  /* NULL is allowable */
                        Addr this, Addr next, Int lineno, Int entry);

/* Add a CFI summary record.  The supplied DiCfSI is copied. */
extern void ML_(addDiCfSI) ( struct _DebugInfo* di, DiCfSI* cfsi );

/* Add a string to the string table of a DebugInfo.  If len==-1,
   ML_(addStr) will itself measure the length of the string. */
extern UChar* ML_(addStr) ( struct _DebugInfo* di, UChar* str, Int len );

extern void ML_(addVar)( struct _DebugInfo* di,
                         Int    level,
                         Addr   aMin,
                         Addr   aMax,
                         UChar* name,
                         UWord  typeR, /* a cuOff */
                         GExpr* gexpr,
                         GExpr* fbGX, /* SHARED. */
                         UChar* fileName, /* where decl'd - may be NULL */
                         Int    lineNo, /* where decl'd - may be zero */
                         Bool   show );

/* Canonicalise the tables held by 'di', in preparation for use.  Call
   this after finishing adding entries to these tables. */
extern void ML_(canonicaliseTables) ( struct _DebugInfo* di );

/* ------ Searching ------ */

/* Find a symbol-table index containing the specified pointer, or -1
   if not found.  Binary search.  */
extern Word ML_(search_one_symtab) ( struct _DebugInfo* di, Addr ptr,
                                     Bool match_anywhere_in_sym,
                                     Bool findText );

/* Find a relocation-table index containing the specified pointer, or -1
   if not found.  Binary search.  */
extern Int ML_(search_one_reltab) ( struct _DebugInfo* si, Addr ptr );

/* Find a location-table index containing the specified pointer, or -1
   if not found.  Binary search.  */
extern Word ML_(search_one_loctab) ( struct _DebugInfo* di, Addr ptr );

/* Find a CFI-table index containing the specified pointer, or -1 if
   not found.  Binary search.  */
extern Word ML_(search_one_cfitab) ( struct _DebugInfo* di, Addr ptr );

/* ------ Misc ------ */

/* Show a non-fatal debug info reading error.  Use vg_panic if
   terminal.  'serious' errors are always shown, not 'serious' ones
   are shown only at verbosity level 2 and above. */
extern 
void ML_(symerr) ( struct _DebugInfo* di, Bool serious, HChar* msg );

/* Print a symbol. */
extern void ML_(ppSym) ( Int idx, DiSym* sym );

/* Print a call-frame-info summary. */
extern void ML_(ppDiCfSI) ( XArray* /* of CfiExpr */ exprs, DiCfSI* si );


#define TRACE_SYMTAB(format, args...) \
   if (di->trace_symtab) { VG_(printf)(format, ## args); }


#endif /* ndef __PRIV_STORAGE_H */

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
