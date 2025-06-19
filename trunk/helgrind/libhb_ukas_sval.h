/////////////////////////////////////////////////////////
//                                                     //
// Threads                                             //
//                                                     //
/////////////////////////////////////////////////////////

struct _Thr {
   /* Current VTSs for this thread.  They change as we go along.
      UKA: discrimination of read and write segments was removed
           as we have another method which deals with rw-locks */
   VtsID vid;
   /* opaque (to us) data we hold on behalf of the library's user. */
   void* opaque;
};

static Thr* Thr__new ( void ) {
   Thr* thr = HG_(zalloc)( "libhb.Thr__new.1", sizeof(Thr) );
   thr->vid = VtsID_INVALID;
   return thr;
}

/////////////////////////////////////////////////////////
//                                                     //
// Shadow Values                                       //
//                                                     //
/////////////////////////////////////////////////////////

// type SVal, SVal_RACE, SVal_NEW, SVal_NOACCESS and SVal_INVALID
// are defined by hb_zsm.h.  We have to do everything else here.

/* Shadow value encodings:
   SVal is 64 bit unsigned int.

   <3> <-------------------------61-------------------->
   <----------32---------->  <-----------32------------>
   111 0--(29)--0            00 0------VtsID:30--------0   SpinVar thread-segment of last write
   110 ...                                                 (Invalid)
   101 0 0--WordSetID:28--0  00 0------VtsID:30--------0   ShM + lock-set
   100 0 0--WordSetID:28--0  00 0------VtsID:30--------0   ShR + lock-set
   011 ...                                                 (Invalid)
   010 0--(29)--0            00 0------VtsID:30--------0   ExclR thread-segment
   001 0--(29)--0            00 0------VtsID:30--------0   ExclW thread-segment
   000 0--(29)--0            0----(21)---0 100 0000 0000   Race
   000 0--(29)--0            0----(21)---0 010 0000 0000   New
   000 0--(29)--0            0----(21)---0 001 0000 0000   NoAccess
   000 0--(29)--0            0----(21)---0 000 0000 0000   Invalid

   The elements in thread sets are Thread*, casted to Word.
   The elements in lock sets are Lock*, casted to Word.
*/

#define SVAL__N_LSID_BITS   28 /* do not change this */
#define SVAL__N_LSID_MASK   ((1UL << (SVAL__N_LSID_BITS)) - 1)
#define SVAL__N_LSID_SHIFT  32

#define SVAL__N_VTSID_BITS  30 /* do not change this */
#define SVAL__N_VTSID_MASK  ((1UL << (SVAL__N_VTSID_BITS)) - 1)
#define SVAL__N_VTSID_SHIFT 0


static inline Bool SVal__is_sane_WordSetID_LSet ( WordSetID wset ) {
   return wset >= 0 && wset <= SVAL__N_LSID_MASK;
}

__attribute__((noinline))
__attribute__((noreturn))
static void SVal__mk_fail ( WordSetID lset, VtsID vtsid, HChar* who ) {
   VG_(printf)("\n");
   VG_(printf)("Helgrind: Fatal internal error -- cannot continue.\n");
   VG_(printf)("Helgrind: mk_SHVAL_ShR/M(lset=%d, vtsid=%d): FAILED\n",
               (Int)lset, (Int)vtsid );
   VG_(printf)("Helgrind: max allowed lset=%d, vtsid=%d\n",
               (Int)SVAL__N_LSID_MASK, (Int)SVAL__N_VTSID_MASK);
   VG_(printf)("Helgrind: program has too many "
              "lock sets to track.\n");
   tl_assert(0);
}

/*
static inline SVal ukas__mk_SHVAL_Race ( WordSetID tset, WordSetID lset ) {
   if (LIKELY(is_sane_WordSetID_TSet(tset)
              && is_sane_WordSetID_LSet(lset))) {
      return (((SVal)7) << 61);
   } else {
      mk_SHVAL_fail(tset, lset, "mk_SHVAL_Race");
   }
}
*/

static inline SVal SVal__mk_SpinVar ( VtsID vtsid ) {
   //tl_assert(VtsID__is_valid(vtsid));
   return (((SVal)7) << 61)
          | (((SVal)vtsid & SVAL__N_VTSID_MASK) << SVAL__N_VTSID_SHIFT);
}

static inline SVal SVal__mk_ShM ( VtsID vtsid,  WordSetID lset ) {
   if (LIKELY(SVal__is_sane_WordSetID_LSet(lset))) {
      return (((SVal)5) << 61)
             | (((SVal)lset)  << SVAL__N_LSID_SHIFT)
             | (((SVal)vtsid) << SVAL__N_VTSID_SHIFT);
   } else {
      SVal__mk_fail(lset, vtsid, "mk_SHVAL_ShM");
   }
}

static inline SVal SVal__mk_ShR ( VtsID vtsid,  WordSetID lset ) {
   if (LIKELY(SVal__is_sane_WordSetID_LSet(lset))) {
      return (((SVal)4) << 61)
             | (((SVal)lset)  << SVAL__N_LSID_SHIFT)
             | (((SVal)vtsid) << SVAL__N_VTSID_SHIFT);
   } else {
      SVal__mk_fail(lset, vtsid, "mk_SHVAL_ShR");
   }
}

static inline SVal SVal__mk_ExclR ( VtsID vtsid ) {
   //tl_assert(VtsID__is_valid(vtsid));
   return (((SVal)2) << 61)
          | (((SVal)vtsid & SVAL__N_VTSID_MASK) << SVAL__N_VTSID_SHIFT);
}

static inline SVal SVal__mk_ExclW ( VtsID vtsid ) {
   //tl_assert(VtsID__is_valid(vtsid));
   return (((SVal)1) << 61)
          | (((SVal)vtsid & SVAL__N_VTSID_MASK) << SVAL__N_VTSID_SHIFT);
}

static inline SVal SVal__mk_Race (void) {
   return SVal_RACE;
}

static inline Bool SVal__is_SpinVar ( SVal sv ) {
   return (sv >> 61) == 7;
}

static inline Bool SVal__is_ShM ( SVal sv ) {
   return (sv >> 61) == 5;
}

static inline Bool SVal__is_ShR ( SVal sv ) {
   return (sv >> 61) == 4;
}

static inline Bool SVal__is_Sh ( SVal sv ) {
   return (SVal__is_ShM(sv) || SVal__is_ShR(sv));
}

static inline Bool SVal__is_ExclR ( SVal sv ) {
   return (sv >> 61) == 2;
}

static inline Bool SVal__is_ExclW ( SVal sv ) {
   return (sv >> 61) == 1;
}

static inline Bool SVal__is_Excl ( SVal sv ) {
   return (SVal__is_ExclW(sv) || SVal__is_ExclR(sv));
}

static inline Bool SVal__is_Race ( SVal sv ) {
   return sv == SVal_RACE;
}

static inline Bool SVal__is_New ( SVal w64 ) {
   return w64 == SVal_NEW;
}

static inline Bool SVal__is_NoAccess ( SVal w64 ) {
   return w64 == SVal_NOACCESS;
}

static inline Bool SVal__is_valid ( SVal sv ) {
   return SVal__is_ExclW(sv) || SVal__is_ExclR(sv)
          || SVal__is_NoAccess(sv) || SVal__is_New(sv)
          || SVal__is_Sh(sv) || SVal__is_Race(sv)
          || SVal__is_SpinVar(sv);
}

static inline Bool SVal__is_tracked ( SVal sv ) {
   return SVal__is_ExclW(sv) || SVal__is_ExclR(sv) || SVal__is_Sh(sv) || SVal__is_New(sv) ;
}

static inline Bool SVal__has_VTS ( SVal sv ) {
   return SVal__is_ExclW(sv) || SVal__is_ExclR(sv)
          || SVal__is_ShM(sv) || SVal__is_ShR(sv)
          || SVal__is_SpinVar(sv);
}

static inline VtsID SVal__get_VTS ( SVal sv ) {
   tl_assert( SVal__has_VTS(sv) );
   return ( sv & SVAL__N_VTSID_MASK ) >> SVAL__N_VTSID_SHIFT;
}

static inline WordSetID SVal__get_lset ( SVal sv ) {
   tl_assert(SVal__is_Sh(sv));
   return (sv >> SVAL__N_LSID_SHIFT) & SVAL__N_LSID_MASK;
}

/* Direct callback from lib_zsm. */
static void SVal__rcinc ( SVal s ) {
   if (SVal__has_VTS(s)) {
      VtsID__rcinc( SVal__get_VTS(s) );
   }
}

/* Direct callback from lib_zsm. */
static void SVal__rcdec ( SVal s ) {
   if (SVal__has_VTS(s)) {
      VtsID__rcdec( SVal__get_VTS(s) );
   }
}
