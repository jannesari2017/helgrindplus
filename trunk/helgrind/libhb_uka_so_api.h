/////////////////////////////////////////////////////////
//                                                     //
// Synchronisation objects                             //
//                                                     //
/////////////////////////////////////////////////////////

// (UInt) `echo "Synchronisation object" | md5sum`
#define SO_MAGIC 0x56b3c5b0U

struct _SO {
   VtsID vid; /* vector-clock of sender */
   UInt  magic;
};

static SO* SO__Alloc ( void ) {
   SO* so = HG_(zalloc)( "libhb.SO__Alloc.1", sizeof(SO) );
   so->vid   = VtsID_INVALID;
   so->magic = SO_MAGIC;
   return so;
}
static void SO__Dealloc ( SO* so ) {
   tl_assert(so);
   tl_assert(so->magic == SO_MAGIC);
   if (so->vid != VtsID_INVALID) {
      VtsID__rcdec(so->vid);
   }
   so->magic = 0;
   HG_(free)( so );
}


/////////////////////////////////////////////////////////
//                                                     //
// Top Level API                                       //
//                                                     //
/////////////////////////////////////////////////////////

static void show_thread_state ( HChar* str, Thr* t )
{
   if (1) return;
   VG_(printf)("thr \"%s\" %p has vi* %u==", str, t, t->vid );
   VtsID__pp( t->vid );
   VG_(printf)("%s","\n");
}


Thr* libhb_init (
        void        (*get_stacktrace)( Thr*, Addr*, UWord ),
        ExeContext* (*get_EC)( Thr* ),
        WordSetID   (*get_LockSet) ( Thr*, Bool )
     )
{
   Thr*  thr;
   VtsID vi;
   tl_assert(get_stacktrace);
   tl_assert(get_EC);
   main_get_stacktrace   = get_stacktrace;
   main_get_EC           = get_EC;
   main_get_LockSet      = get_LockSet;

   // No need to initialise hg_wordfm.
   // No need to initialise hg_wordset.

   vts_set_init();
   vts_tab_init();
   event_map_init();
   VtsID__invalidate_caches();

   // initialise shadow memory
   zsm_init( SVal__rcinc, SVal__rcdec );

   thr = Thr__new();
   vi  = VtsID__mk_Singleton( thr, 1 );
   thr->vid = vi;
   VtsID__rcinc(thr->vid);

   show_thread_state("  root", thr);
   return thr;
}

Thr* libhb_create ( Thr* parent )
{
   /* The child's VTSs are copies of the parent's VTSs, but ticked at
      the child's index.  Since the child's index is guaranteed
      unique, it has never been seen before, so the implicit value
      before the tick is zero and after that is one. */
   Thr* child = Thr__new();

   child->vid = VtsID__tick( parent->vid, child );
   VtsID__rcinc(child->vid);

   tl_assert(VtsID__indexAt( child->vid, child ) == 1);

   /* and the parent has to move along too */
   VtsID__rcdec(parent->vid);
   parent->vid = VtsID__tick( parent->vid, parent );
   VtsID__rcinc(parent->vid);

   show_thread_state(" child", child);
   show_thread_state("parent", parent);

   return child;
}


void libhb_uka_shutdown ( Bool show_stats );

//#define DEFINED__libhb_uka_shutdown

#ifndef DEFINED__libhb_uka_shutdown
void libhb_uka_shutdown ( Bool show_stats ) {
	
	VG_(printf)(" UNDEFINED : libhb_uka_shutdown\n");
}
#else

#endif

/* Shut down the library, and print stats (in fact that's _all_
   this is for. */
void libhb_shutdown ( Bool show_stats )
{
	libhb_uka_shutdown (1);
   if (show_stats) {
      VG_(printf)("%s","<<< BEGIN libhb stats >>>\n");
      VG_(printf)(" secmaps: %'10lu allocd (%'12lu g-a-range)\n",
                  stats__secmaps_allocd,
                  stats__secmap_ga_space_covered);
      VG_(printf)("  linesZ: %'10lu allocd (%'12lu bytes occupied)\n",
                  stats__secmap_linesZ_allocd,
                  stats__secmap_linesZ_bytes);
      VG_(printf)("  linesF: %'10lu allocd (%'12lu bytes occupied)\n",
                  stats__secmap_linesF_allocd,
                  stats__secmap_linesF_bytes);
      VG_(printf)(" secmaps: %'10lu iterator steppings\n",
                  stats__secmap_iterator_steppings);
      VG_(printf)(" secmaps: %'10lu searches (%'12lu slow)\n",
                  stats__secmaps_search, stats__secmaps_search_slow);

      VG_(printf)("%s","\n");
      VG_(printf)("   cache: %'lu totrefs (%'lu misses)\n",
                  stats__cache_totrefs, stats__cache_totmisses );
      VG_(printf)("   cache: %'14lu Z-fetch,    %'14lu F-fetch\n",
                  stats__cache_Z_fetches, stats__cache_F_fetches );
      VG_(printf)("   cache: %'14lu Z-wback,    %'14lu F-wback\n",
                  stats__cache_Z_wbacks, stats__cache_F_wbacks );
      VG_(printf)("   cache: %'14lu invals,     %'14lu flushes\n",
                  stats__cache_invals, stats__cache_flushes );
      VG_(printf)("   cache: %'14llu arange_New  %'14llu direct-to-Zreps\n",
                  stats__cache_make_New_arange,
                  stats__cache_make_New_inZrep);

      VG_(printf)("%s","\n");
      VG_(printf)("   cline: %'10lu normalises\n",
                  stats__cline_normalises );
      VG_(printf)("   cline:  rds 8/4/2/1: %'13lu %'13lu %'13lu %'13lu\n",
                  stats__cline_read64s,
                  stats__cline_read32s,
                  stats__cline_read16s,
                  stats__cline_read8s );
      VG_(printf)("   cline:  wrs 8/4/2/1: %'13lu %'13lu %'13lu %'13lu\n",
                  stats__cline_write64s,
                  stats__cline_write32s,
                  stats__cline_write16s,
                  stats__cline_write8s );
      VG_(printf)("   cline: sets 8/4/2/1: %'13lu %'13lu %'13lu %'13lu\n",
                  stats__cline_set64s,
                  stats__cline_set32s,
                  stats__cline_set16s,
                  stats__cline_set8s );
      VG_(printf)("   cline: get1s %'lu, copy1s %'lu\n",
                  stats__cline_get8s, stats__cline_copy8s );
      VG_(printf)("   cline:    splits: 8to4 %'12lu    4to2 %'12lu    2to1 %'12lu\n",
                 stats__cline_64to32splits,
                 stats__cline_32to16splits,
                 stats__cline_16to8splits );
      VG_(printf)("   cline: pulldowns: 8to4 %'12lu    4to2 %'12lu    2to1 %'12lu\n",
                 stats__cline_64to32pulldown,
                 stats__cline_32to16pulldown,
                 stats__cline_16to8pulldown );
      if (0)
      VG_(printf)("   cline: sizeof(CacheLineZ) %ld, covers %ld bytes of arange\n",
                  (Word)sizeof(LineZ), (Word)N_LINE_ARANGE);

      VG_(printf)("%s","\n");

      VG_(printf)("   libhb: %'13llu msm_read  (%'llu changed)\n",
                  stats__msm_read, stats__msm_read_change);
      VG_(printf)("   libhb: %'13llu msm_write (%'llu changed)\n",
                  stats__msm_write, stats__msm_write_change);
      VG_(printf)("   libhb: %'13llu getOrd queries (%'llu misses)\n",
                  stats__getOrdering_queries, stats__getOrdering_misses);
      VG_(printf)("   libhb: %'13llu join2  queries (%'llu misses)\n",
                  stats__join2_queries, stats__join2_misses);

      VG_(printf)("%s","\n");
      VG_(printf)(
         "   libhb: %ld entries in vts_table (approximately %lu bytes)\n",
         VG_(sizeXA)( vts_tab ), VG_(sizeXA)( vts_tab ) * sizeof(VtsTE)
      );
      VG_(printf)( "   libhb: %lu entries in vts_set\n",
                   VG_(sizeFM)( vts_set ) );

      VG_(printf)("%s","\n");
      VG_(printf)( "   libhb: ctxt__rcdec: 1=%lu(%lu eq), 2=%lu, 3=%lu\n",
                   stats__ctxt_rcdec1, stats__ctxt_rcdec1_eq,
                   stats__ctxt_rcdec2,
                   stats__ctxt_rcdec3 );
      VG_(printf)( "   libhb: ctxt__rcdec: calls %lu, discards %lu\n",
                   stats__ctxt_rcdec_calls, stats__ctxt_rcdec_discards);
      VG_(printf)( "   libhb: contextTab: %lu slots, %lu max ents\n",
                   (UWord)N_RCEC_TAB,
                   stats__ctxt_tab_curr );
      VG_(printf)( "   libhb: contextTab: %lu queries, %lu cmps\n",
                   stats__ctxt_tab_qs,
                   stats__ctxt_tab_cmps );
#if 0
      VG_(printf)("sizeof(AvlNode)     = %lu\n", sizeof(AvlNode));
      VG_(printf)("sizeof(WordBag)     = %lu\n", sizeof(WordBag));
      VG_(printf)("sizeof(MaybeWord)   = %lu\n", sizeof(MaybeWord));
      VG_(printf)("sizeof(CacheLine)   = %lu\n", sizeof(CacheLine));
      VG_(printf)("sizeof(LineZ)       = %lu\n", sizeof(LineZ));
      VG_(printf)("sizeof(LineF)       = %lu\n", sizeof(LineF));
      VG_(printf)("sizeof(SecMap)      = %lu\n", sizeof(SecMap));
      VG_(printf)("sizeof(Cache)       = %lu\n", sizeof(Cache));
      VG_(printf)("sizeof(SMCacheEnt)  = %lu\n", sizeof(SMCacheEnt));
      VG_(printf)("sizeof(CountedSVal) = %lu\n", sizeof(CountedSVal));
      VG_(printf)("sizeof(VTS)         = %lu\n", sizeof(VTS));
      VG_(printf)("sizeof(ScalarTS)    = %lu\n", sizeof(ScalarTS));
      VG_(printf)("sizeof(VtsTE)       = %lu\n", sizeof(VtsTE));
      VG_(printf)("sizeof(MSMInfo)     = %lu\n", sizeof(MSMInfo));

      VG_(printf)("sizeof(struct _XArray)     = %lu\n", sizeof(struct _XArray));
      VG_(printf)("sizeof(struct _WordFM)     = %lu\n", sizeof(struct _WordFM));
      VG_(printf)("sizeof(struct _Thr)     = %lu\n", sizeof(struct _Thr));
      VG_(printf)("sizeof(struct _SO)     = %lu\n", sizeof(struct _SO));
#endif

      VG_(printf)("%s","<<< END libhb stats >>>\n");
      VG_(printf)("%s","\n");

   }
}

void libhb_async_exit ( Thr* thr )
{
   /* is there anything we need to do? */
}

/* Both Segs and SOs point to VTSs.  However, there is no sharing, so
   a Seg that points at a VTS is its one-and-only owner, and ditto for
   a SO that points at a VTS. */

SO* libhb_so_alloc ( void )
{
   return SO__Alloc();
}

void libhb_so_dealloc ( SO* so )
{
   tl_assert(so);
   tl_assert(so->magic == SO_MAGIC);
   SO__Dealloc(so);
}

/* See comments in libhb.h for details on the meaning of
   strong vs weak sends.
   UKA: strong and weak receives makes no difference now */
void libhb_so_send ( Thr* thr, SO* so, Bool strong_send )
{
   /* Copy the VTSs from 'thr' into the sync object, and then move
      the thread along one step. */

   tl_assert(so);
   tl_assert(so->magic == SO_MAGIC);

   /* since we're overwriting the VtsIDs in the SO, we need to drop
      any references made by the previous contents thereof */
   if (so->vid == VtsID_INVALID) {
      so->vid = thr->vid;
      VtsID__rcinc(so->vid);
   } else {
      /* In a strong send, we dump any previous VC in the SO and
         install the sending thread's VC instead.  For a weak send we
         must join2 with what's already there. */
      VtsID__rcdec(so->vid);
      so->vid = strong_send ? thr->vid : VtsID__join2( so->vid, thr->vid );
      VtsID__rcinc(so->vid);
   }

   /* move parent clock along */
   VtsID__rcdec(thr->vid);
   thr->vid = VtsID__tick( thr->vid, thr );
   VtsID__rcinc(thr->vid);
   if (strong_send)
      show_thread_state("s-send", thr);
   else
      show_thread_state("w-send", thr);
}

void libhb_so_recv ( Thr* thr, SO* so, Bool strong_recv )
{
   tl_assert(so);
   tl_assert(so->magic == SO_MAGIC);

   if (so->vid != VtsID_INVALID) {
      /* Weak receive (basically, an R-acquisition of a R-W lock).
         This advances the read-clock of the receiver, but not the
         write-clock. */
      VtsID__rcdec(thr->vid);
      thr->vid = VtsID__join2( thr->vid, so->vid );
      VtsID__rcinc(thr->vid);

      if (strong_recv)
         show_thread_state("s-recv", thr);
      else
         show_thread_state("w-recv", thr);

   } else {
      tl_assert(so->vid == VtsID_INVALID);
      /* Deal with degenerate case: 'so' has no vts, so there has been
         no message posted to it.  Just ignore this case. */
      show_thread_state("d-recv", thr);
   }
}

Bool libhb_so_everSent ( SO* so )
{
   if (so->vid == VtsID_INVALID) {
      return False;
   } else {
      return True;
   }
}

void libhb_so_join( SO* so, SO* so_other )
{
   tl_assert(so);
   tl_assert(so->magic == SO_MAGIC);
   tl_assert(so_other);
   tl_assert(so_other->magic == SO_MAGIC);
   
   tl_assert(so_other->vid != VtsID_INVALID);

   if( so->vid == VtsID_INVALID ) {
      so->vid = so_other->vid;
      VtsID__rcinc( so->vid );
   } else {
      // Join them
      VtsID__rcdec( so->vid );
      so->vid = VtsID__join2( so->vid, so_other->vid );
      VtsID__rcinc( so->vid );
   }
}

SO* libhb_so_extract( Thr* thr )
{
   SO* so = SO__Alloc();
   tl_assert(so);
   tl_assert(so->magic == SO_MAGIC);
   so->vid = thr->vid;
   VtsID__rcinc(so->vid);
   return so;
}

#define XXX1 0 // 0x67a106c
#define XXX2 0

static Bool TRACEME(Addr a, SizeT szB) {
   if (XXX1 && a <= XXX1 && XXX1 <= a+szB) return True;
   if (XXX2 && a <= XXX2 && XXX2 <= a+szB) return True;
   return False;
}
static void trace ( Thr* thr, Addr a, SizeT szB, HChar* s ) {
  SVal sv = zsm_read8(a);
  VG_(printf)("thr %p (%#lx,%lu) %s: 0x%016llx ", thr,a,szB,s,sv);
  show_thread_state("", thr);
  VG_(printf)("%s","\n");
}

void libhb_range_new ( Thr* thr, Addr a, SizeT szB )
{
   SVal sv = SVal_NEW;
   //tl_assert(is_sane_SVal_C(sv));
   if(TRACEME(a,szB))trace(thr,a,szB,"nw-before");
   zsm_set_range( a, szB, sv );
   if(TRACEME(a,szB))trace(thr,a,szB,"nw-after ");
}

void libhb_range_noaccess ( Thr* thr, Addr a, SizeT szB )
{
   if(TRACEME(a,szB))trace(thr,a,szB,"NA-before");
   zsm_set_range( a, szB, SVal_NOACCESS );
   if(TRACEME(a,szB))trace(thr,a,szB,"NA-after ");
}

void* libhb_get_Thr_opaque ( Thr* thr ) {
   tl_assert(thr);
   return thr->opaque;
}

void libhb_set_Thr_opaque ( Thr* thr, void* v ) {
   tl_assert(thr);
   thr->opaque = v;
}

void libhb_copy_shadow_state ( Addr dst, Addr src, SizeT len )
{
   zsm_copy_range(dst, src, len);
}

void libhb_maybe_GC ( void )
{
   event_map_maybe_GC();
   /* If there are still freelist entries available, no need for a
      GC. */
   if (vts_tab_freelist != VtsID_INVALID)
      return;
   /* So all the table entries are full, and we're having to expand
      the table.  But did we hit the threshhold point yet? */
   if (VG_(sizeXA)( vts_tab ) < vts_next_GC_at)
      return;
   vts_tab__do_GC( False/*don't show stats*/ );
}
