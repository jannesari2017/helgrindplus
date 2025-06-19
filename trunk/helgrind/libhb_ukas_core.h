/////////////////////////////////////////////////////////
//                                                     //
// Core MSM                                            //
//                                                     //
/////////////////////////////////////////////////////////

// TODO_UKA: Write new memory state machine

/* Logic in msm_read/msm_write updated/verified after re-analysis,
   19 Nov 08. */

// hg_loops_is_spin_reading()
#include "hg_loops.h"
#include "hg_logging.h"
#define MSM_CONFACC 1

#define MSM_CHECK 0

/* 19 Nov 08: it seems that MSM_RACE2ERR == 1 is a bad idea.  When
   nonzero, the effect is that when a race is detected for a location,
   that location is put into a special 'error' state and no further
   checking of it is done until it returns to a 'normal' state, which
   requires it to be deallocated and reallocated.

   This is a bad idea, because of the interaction with suppressions.
   Suppose there is a race on the location, but the error is
   suppressed.  The location now is marked as in-error.  Now any
   subsequent race -- including ones we want to see -- will never be
   detected until the location is deallocated and reallocated.

   Hence set MSM_CHECK to zero.  This causes raced-on locations to
   remain in the normal 'C' (constrained) state, but places on them
   the constraint that the next accesses happen-after both the
   existing constraint and the relevant vector clock of the thread
   doing the racing access.
*/
#define MSM_RACE2ERR 0

static ULong stats__msm_read         = 0;
static ULong stats__msm_read_change  = 0;
static ULong stats__msm_write        = 0;
static ULong stats__msm_write_change = 0;

__attribute__((noinline))
static void record_race_info ( Thr* acc_thr,
                               Addr acc_addr, SizeT szB, Bool isWrite )
{
   /* Call here to report a race.  We just hand it onwards to
      HG_(record_error_Race).  If that in turn discovers that the
      error is going to be collected, then that queries the
      conflicting-event map.  The alternative would be to query it
      right here.  But that causes a lot of pointless queries for
      errors which will shortly be discarded as duplicates, and can
      become a performance overhead; so we defer the query until we
      know the error is not a duplicate. */
   tl_assert(acc_thr->opaque);
   HG_(record_error_Race)( acc_thr->opaque, acc_addr,
                           szB, isWrite, NULL/*mb_lastlock*/ );
}

// TODO_UKA: Delete me

//static Bool is_sane_SVal_C ( SVal sv ) {
//   POrd ord;
//   if (!SVal__isC(sv)) return True;
//   ord = VtsID__getOrdering( SVal__unC_Rmin(sv), SVal__unC_Wmin(sv) );
//   if (ord == POrd_EQ || ord == POrd_LT) return True;
//   return False;
//}

/*----------------------------------------------------------------*/
/*--- UKA short memory state machine (ukas__* functions)       ---*/
/*----------------------------------------------------------------*/

static UWord stats__msm_read_ExclW_nochange  = 0;
static UWord stats__msm_read_ExclW_transfer  = 0;
static UWord stats__msm_read_ExclW_to_ShR    = 0;
static UWord stats__msm_read_ExclW_to_ExclR  = 0;
static UWord stats__msm_read_ExclW_to_ShM    = 0;
static UWord stats__msm_read_ExclW_to_Race   = 0;
static UWord stats__msm_read_ExclR_nochange  = 0;
static UWord stats__msm_read_ExclR_transfer  = 0;
static UWord stats__msm_read_ExclR_to_ShR    = 0;
static UWord stats__msm_read_ShR_to_ShR      = 0;
static UWord stats__msm_read_ShM_to_ShM      = 0;
static UWord stats__msm_read_ShM_to_Race     = 0;
static UWord stats__msm_read_ShM_to_ExclR    = 0;
static UWord stats__msm_read_New_to_ExclR    = 0;
static UWord stats__msm_read_New_to_ExclW    = 0;
static UWord stats__msm_read_NoAccess        = 0;

static UWord stats__msm_write_ExclW_nochange  = 0;
static UWord stats__msm_write_ExclW_transfer  = 0;
static UWord stats__msm_write_ExclW_to_ShM    = 0;
static UWord stats__msm_write_ExclW_to_Race   = 0;
static UWord stats__msm_write_ExclR_to_ShM    = 0;
static UWord stats__msm_write_ExclR_to_ExclW  = 0;
static UWord stats__msm_write_ExclR_to_Race   = 0;
static UWord stats__msm_write_ShR_to_ShM      = 0;
static UWord stats__msm_write_ShR_to_Race     = 0;
static UWord stats__msm_write_ShM_to_ShM      = 0;
static UWord stats__msm_write_ShM_to_Race     = 0;
static UWord stats__msm_write_ShM_to_ExclW    = 0;
static UWord stats__msm_write_New_to_ExclW    = 0;
static UWord stats__msm_write_NoAccess        = 0;

static void ukas__show_shadow_w64 ( /*OUT*/Char* buf, Int nBuf, SVal w64 )
{
   tl_assert(nBuf-1 >= 99);
   VG_(memset)(buf, 0, nBuf);
   if (SVal__is_ShM(w64)) {
      VG_(sprintf)(buf, "ShM(%u,%u)",
                   SVal__get_VTS(w64), SVal__get_lset(w64));
   }
   else
   if (SVal__is_ShR(w64)) {
      VG_(sprintf)(buf, "ShR(%u,%u)",
                   SVal__get_VTS(w64), SVal__get_lset(w64));
   }
   else
   if (SVal__is_ExclW(w64)) {
      VG_(sprintf)(buf, "ExclW(%u)", SVal__get_VTS(w64));
   }
   else
   if (SVal__is_ExclR(w64)) {
      VG_(sprintf)(buf, "ExclR(%u)", SVal__get_VTS(w64));
   }
   else
   if (SVal__is_SpinVar(w64)) {
      VG_(sprintf)(buf, "Spin(%u)", SVal__get_VTS(w64));
   }
   else
   if (SVal__is_Race(w64)) {
      VG_(sprintf)(buf, "%s", "Race");
   }
   else
      if (SVal__is_New(w64)) {
         VG_(sprintf)(buf, "%s", "New");
      }
   else
   if (SVal__is_NoAccess(w64)) {
      VG_(sprintf)(buf, "%s", "NoAccess");
   }
   else {
      VG_(sprintf)(buf, "Invalid-shadow-word(%llu)", w64);
   }
}

static
void ukas__show_shadow_w64_for_user ( /*OUT*/Char* buf, Int nBuf, SVal w64 )
{
   tl_assert(nBuf-1 >= 99);
   VG_(memset)(buf, 0, nBuf);
   if (SVal__is_ShM(w64)) {
      VtsID vts  = SVal__get_VTS(w64);
      WordSetID lset = SVal__get_lset(w64);
      VG_(sprintf)(buf, "ShMod(VtsID=%u,#L=%ld)",
                   vts,
                   HG_(cardinalityWS)(univ_lsets, lset));
   }
   else
   if (SVal__is_ShR(w64)) {
      VtsID vts  = SVal__get_VTS(w64);
      WordSetID lset = SVal__get_lset(w64);
      VG_(sprintf)(buf, "ShRO(VtsID=%u,#L=%ld)",
                   vts,
                   HG_(cardinalityWS)(univ_lsets, lset));
   }
   else
   if (SVal__is_ExclW(w64)) {
      VtsID vts  = SVal__get_VTS(w64);
      VG_(sprintf)(buf, "ExclW(VtsID=%u)", vts);
   }
   else
   if (SVal__is_ExclR(w64)) {
      VtsID vts  = SVal__get_VTS(w64);
      VG_(sprintf)(buf, "ExclR(VtsID=%u)", vts);
   }
   else
   if (SVal__is_SpinVar(w64)) {
      VtsID vts  = SVal__get_VTS(w64);
      VG_(sprintf)(buf, "SpinVar(VtsID=%u)", vts);
   }
   else
   if (SVal__is_New(w64)) {
      VG_(sprintf)(buf, "%s", "New");
   }
   else
   if (SVal__is_Race(w64)) {
      VG_(sprintf)(buf, "%s", "Race");
   }
   else
   if (SVal__is_NoAccess(w64)) {
      VG_(sprintf)(buf, "%s", "NoAccess");
   }
   else {
      VG_(sprintf)(buf, "Invalid-shadow-word(%llu)", w64);
   }
}

static void msm__show_state_change ( Thr* acc_thr, Addr a, Int szB,
                                     Char howC,
                                     SVal sv_old, SVal sv_new )
{
   ThreadId tid = ((Thread*)acc_thr->opaque)->coretid; // FIXME: bad hack!!
   UChar txt_old[100], txt_new[100];
   Char* how = "";
   Char* hb = "--";

   switch (howC) {
      case 'r': how = "rd"; break;
      case 'w': how = "wr"; break;
      case 'p': how = "pa"; break;
      default: tl_assert(0);
   }
   ukas__show_shadow_w64_for_user(txt_old, sizeof(txt_old), sv_old);
   ukas__show_shadow_w64_for_user(txt_new, sizeof(txt_new), sv_new);
   txt_old[sizeof(txt_old)-1] = 0;
   txt_new[sizeof(txt_new)-1] = 0;
   
   if( SVal__has_VTS(sv_old) ) {
      VtsID oldVTS = SVal__get_VTS(sv_old);
      VtsID newVTS = acc_thr->vid;
      POrd  ord = VtsID__getOrdering(oldVTS,newVTS);
      if( ord == POrd_EQ )
         hb = "==";
      else if ( ord == POrd_LT )
         hb = ">>";
      else if ( ord == POrd_GT )
         hb = "<<";
      else
         hb = "||";
   }
   
//      /* show everything */
//      VG_(message)(Vg_UserMsg, "");
//      announce_one_thread( thr_acc );
//      VG_(message)(Vg_UserMsg,
//                   "TRACE: %#lx %s %d thr#%d :: %s --> %s",
//                   a, how, szB, thr_acc->errmsg_index, txt_old, txt_new );
//      tid = map_threads_maybe_reverse_lookup_SLOW(thr_acc);
//      if (tid != VG_INVALID_THREADID) {
//         VG_(get_and_pp_StackTrace)( tid, 8 );
//      }
//   } else {
      VG_(message)(Vg_UserMsg,
                   "TRACE: %#lx %s %d thr#%d vts#%d :: %22s -%s-> %22s",
                   a, how, szB, tid, acc_thr->vid, txt_old, hb, txt_new );
      if (tid != VG_INVALID_THREADID) {
         VG_(get_and_pp_StackTrace)( tid, 8 );
      }
//   }
}

//
/* Compute new state following a read */
static inline SVal msm_read ( SVal svOld,
                              /* The following are only needed for
                                 creating error reports. */
                              Thr* acc_thr, Bool atomic,
                              Addr acc_addr, SizeT szB )
{
   SVal  svNew = SVal_INVALID;
   VtsID tvid  = acc_thr->vid;

   VtsID__rcinc(tvid); // SCHMALTZ : logging

   //if(HG_(clo_detect_mutex)) TODO
      HG_(onMSM)(acc_thr,atomic,acc_addr,szB,False);

   if (0) VG_(printf)("read thr=%p %#lx\n", acc_thr, acc_addr);

   if( hg_loops_is_spin_reading() ) {
      VtsID last_access;

      if(HG_(clo_detect_mutex)) {
         Bool wasALock=HG_(onSpinRead)(acc_thr,atomic,acc_addr,szB);
         if(wasALock) {
            if(PRINT_spin_lockset_msm) VG_(printf)("%s:msm_read use lockset for %#lx\n",__FILE__,acc_addr);
            if( SVal__has_VTS(svOld) )
               svNew = SVal__mk_SpinVar(svOld);
            else
               svNew = SVal__mk_SpinVar(tvid);
            goto out;
         } else {
            if(PRINT_spin_lockset_msm) VG_(printf)("%s:msm_read use happens-before for %#lx\n",__FILE__,acc_addr);
         }
         if(PRINT_spin_lockset_msm) {

            VG_(printf)("%s:msm_read (%#lx) acc_thr->vid=",__FILE__,acc_addr);VtsID__pp(tvid);VG_(printf)("\n");
            if( SVal__has_VTS(svOld) ) {
               VG_(printf)("%s:msm_read (%#lx) svOld=",__FILE__,acc_addr);VtsID__pp(svOld);VG_(printf)("\n");
            }

            ExeContext* ec=VG_(record_ExeContext)( VG_(get_running_tid)(), 0 );
            VG_(pp_ExeContext)(ec);
         }
      }

      if( SVal__has_VTS(svOld) ) {
         /** TODO SCHMALTZ: did I understand what we did ?
          * Get the last thread who accessed this memory address (last_access),
          * and make a join ; build happens-before.
          * Last access hb this read...
          *   I suppose last_access is the last write.
          */
         last_access = SVal__get_VTS(svOld);

         VtsID__rcdec(tvid); // decrement reference counter
         if(0 && (tvid!=last_access)) { //TODO SCHMALTZ : delete code
			 VG_(printf)("hg_loops_is_spin_reading : acc_thr->vid=");
			 VtsID__pp(tvid);
			 VG_(printf)(" & last_access=");
			 VtsID__pp(last_access );
			 VG_(printf)("\n");
			 if(0) tl_assert2(tvid!=last_access,"That isn't a spin loop variable, as it was written by the 'spinning' thread.");
         }
         acc_thr->vid = VtsID__join2( tvid, last_access );
         if(0) {
            if((tvid!=last_access)) {
               VG_(printf)("hg_loops_is_spin_reading : acc_thr->vid-post-join=");
               VtsID__pp(acc_thr->vid);
               if(acc_thr->vid!=tvid) VG_(printf)(" VID-changed!");
               VG_(printf)("\n");
            } else {
               VtsID__pp(acc_thr->vid);
               VG_(printf)("--VID--\n");
            }
         }
         VtsID__rcinc(acc_thr->vid); // increment reference counter
      } else {
         /** TODO SCHMALTZ: did I understand what we did ?
          * We are the first who access this memory.
          * (the signaler thread comes after ?)
          */
         last_access = tvid;
      }
      svNew = SVal__mk_SpinVar(last_access);
      goto out;
   }
   
   if( atomic && !SVal__is_SpinVar(svOld) ) {
      svNew = svOld;
      goto out;
   }
   
   /* Exclusive Write */
   if (LIKELY(SVal__is_ExclW(svOld))) {
      /* read ExclW(segid)
           |  segid_old == segid-of-thread
           -> no change
           |  segid_old `happens_before` segid-of-this-thread
           -> Excl(segid-of-this-thread)
           |  otherwise
           -> ShR
      */
      VtsID vtsid_old = SVal__get_VTS(svOld);
      POrd  ord = VtsID__getOrdering(vtsid_old,tvid);
      if (ord == POrd_EQ || ord == POrd_LT) {
         /* Transfer to Exclusive Read (ExclR) state. */
         svNew = SVal__mk_ExclR(tvid);
         stats__msm_read_ExclW_to_ExclR++;
         goto out;
      }
      else {
         /* Enter the shared-readonly (ShR) state. */
         WordSetID lset_acc = main_get_LockSet( acc_thr, /*allLocks*/ True );

         // Thread does not hold any locks by a parallel read after write
         //  or List of candidates is null
         if ( HG_(isEmptyWS)(univ_lsets, lset_acc) ) {
            svNew = SVal__mk_Race();
            record_race_info( acc_thr, acc_addr, szB, False/*!isWrite*/ );
            stats__msm_read_ExclW_to_Race++;
            goto out;
         }
         else {
            svNew = SVal__mk_ShM( vtsid_old, lset_acc );
            stats__msm_read_ExclW_to_ShM++;
            goto out;
         }
         /*NOTREACHED*/
      }
      /*NOTREACHED*/
   }

   /* Exclusive Read */
   else if (LIKELY(SVal__is_ExclR(svOld))) {
      /* read ExclR(segid)
           |  segid_old == segid-of-thread
           -> no change
           |  segid_old `happens_before` segid-of-this-thread
           -> Excl(segid-of-this-thread)
           |  otherwise
           -> ShR
      */
      VtsID vtsid_old = SVal__get_VTS(svOld);
      POrd  ord = VtsID__getOrdering(vtsid_old,tvid);
      if (ord == POrd_EQ) {
         /* no change */
         stats__msm_read_ExclR_nochange++;
         svNew = svOld;
         goto out;
      }
      else if (ord == POrd_LT) {
         /* -> Excl(segid-of-this-thread) */
         svNew = SVal__mk_ExclR(tvid);
         stats__msm_read_ExclR_transfer++;
         goto out;
      }
      else {
         /* Enter the shared-readonly (ShR) state. */
         WordSetID lset_acc = main_get_LockSet( acc_thr, /*allLocks*/ True );
         svNew = SVal__mk_ShR( vtsid_old, lset_acc );
         stats__msm_read_ExclR_to_ShR++;
         goto out;
      }
      /*NOTREACHED*/
   }

   /* Shared-Readonly */
   else if (SVal__is_ShR(svOld)) {
     /* read Shared-Readonly(threadset, lockset)
        We remain in ShR state, but add this thread to the
        threadset and refine the lockset accordingly.  Do not
        complain if the lockset becomes empty -- that's ok. */

      WordSetID lset_old = SVal__get_lset(svOld);
      WordSetID lset_acc = main_get_LockSet( acc_thr, /*allLocks*/ True );
      WordSetID lset_new = HG_(intersectWS)( univ_lsets, lset_old, lset_acc );
      VtsID vtsid_old = SVal__get_VTS(svOld);
      
      svNew = SVal__mk_ShR( vtsid_old, lset_new );
// TODO_UKA: write the function for record_last_lock_lossage
//      if (lset_old != lset_new)
//         record_last_lock_lossage(a,lset_old,lset_new);
      stats__msm_read_ShR_to_ShR++;
      goto out;
   }

   /* Shared-Modified */
   else if (SVal__is_ShM(svOld)) {
      //TODO: Comment

      WordSetID lset_old = SVal__get_lset(svOld);
      WordSetID lset_acc = main_get_LockSet( acc_thr, /*allLocks*/ True );
      WordSetID lset_new = HG_(intersectWS)( univ_lsets, lset_old, lset_acc );
      VtsID vtsid_old = SVal__get_VTS(svOld);
      
      /* lockset */
//      if (lset_old != lset_new)
//         record_last_lock_lossage(a, lset_old, lset_new);

      if ( HG_(isEmptyWS)(univ_lsets, lset_new) ) {
         POrd  ord;
         ord = VtsID__getOrdering(vtsid_old,tvid);
         if (ord == POrd_EQ || ord == POrd_LT) {
            /* happens-before */
            svNew = SVal__mk_ExclR(tvid);
            stats__msm_read_ShM_to_ExclR++;
            goto out;
         }
         else {
            svNew = SVal__mk_Race();
            record_race_info( acc_thr, acc_addr, szB, False/*!isWrite*/ );
            stats__msm_read_ShM_to_Race++;
            goto out;
         }
         
      }
      else {
         svNew = SVal__mk_ShM( tvid, lset_new );
         stats__msm_read_ShM_to_ShM++;
         goto out;
      }
   }
   
   /* SpinVar */
   else if (SVal__is_SpinVar(svOld)) {
      VtsID last_write = SVal__get_VTS(svOld);
      if( last_write != VtsID_INVALID ) {
         VtsID__rcdec(tvid);
         acc_thr->vid = VtsID__join2( tvid, last_write );
         VtsID__rcinc(acc_thr->vid);
      }
      svNew = svOld;
      goto out;
   }

   /* Race */
   else if (SVal__is_Race(svOld)) {
      /* read New -> ExclR(segid) */
      svNew = SVal_RACE;
      goto out;
   }

   /* New */
   else if (SVal__is_New(svOld)) {
      /* read New -> ExclR( Vts_ID ) */
      svNew = SVal__mk_ExclR( tvid );
      stats__msm_read_New_to_ExclR++;
      goto out;
   }

   /* NoAccess */
   else if (SVal__is_NoAccess(svOld)) {
      // FIXME: complain if accessing here
      // FIXME: transition to Excl?
      if (0)
      VG_(printf)(
         "msm__handle_read_aligned_32(thr=%p, addr=%p): NoAccess\n",
         acc_thr, (void*)acc_addr );
      stats__msm_read_NoAccess++;
      svNew = SVal_NOACCESS; /*NOCHANGE*/
      goto out;
   }

   /* hmm, bogus state */
   tl_assert(0);

  out:
   if ( svNew != svOld ) {
      if ( HG_(clo_show_conflicts) ) {
         if (SVal__is_tracked(svOld) && SVal__is_tracked(svNew)) {
            event_map_bind( acc_addr, szB, False/*!isWrite*/, acc_thr );
            stats__msm_read_change++;
         }
      }
      if ( HG_(clo_trace_addr) == acc_addr ) {
         msm__show_state_change(acc_thr, acc_addr, szB, 'r', svOld, svNew );
         if(PRINT_spin_lockset_msm) {
            if( SVal__has_VTS(svOld) ) {
               VG_(printf)("TRACE: (%#lx) svOld=",acc_addr);VtsID__pp(svOld);VG_(printf)("\n");
            }
            if( SVal__has_VTS(svNew) ) {
               VG_(printf)("TRACE: (%#lx) svNew=",acc_addr);VtsID__pp(svNew);VG_(printf)("\n");
            }
         }
      }
   }
   if( tvid != acc_thr->vid ) {
      if(PRINT_spin_lockset_msm) {
         {
            VG_(printf)("TRACE-rd: PRE  : acc_thr->vid=");VtsID__pp(tvid);VG_(printf)("\n");
         }
         {
            VG_(printf)("TRACE-rd: POST : acc_thr->vid=");VtsID__pp(acc_thr->vid);VG_(printf)("\n");
         }
      }
   }
   VtsID__rcdec(tvid); // SCHMALTZ : logging
   return svNew;
}



/* Compute new state following a write */
static inline SVal msm_write ( SVal svOld,
                              /* The following are only needed for
                                 creating error reports. */
                              Thr* acc_thr, Bool atomic,
                              Addr acc_addr, SizeT szB )
{
   SVal  svNew = SVal_INVALID;
   VtsID tvid  = acc_thr->vid;

   VtsID__rcinc(tvid); // SCHMALTZ : logging
   //if(HG_(clo_detect_mutex))TODO
      HG_(onMSM)(acc_thr,atomic,acc_addr,szB,True);

   if (0) VG_(printf)("write32 thr=%p %#lx\n", acc_thr, acc_addr);

   if( atomic && !SVal__is_SpinVar(svOld) ) {
      svNew = svOld;
      goto out;
   }
   
   /* New */
   if (SVal__is_New(svOld)) {
      /* write New -> ExclW(segid) */
      svNew = SVal__mk_ExclW( tvid );
      stats__msm_write_New_to_ExclW++;
      goto out;
   }

   /* ExclusiveWrite */
   else if (SVal__is_ExclW(svOld)) {
      // I believe is identical to case for read Excl
      // apart from enters ShM rather than ShR
      /* read ExclW(segid)
           |  segid_old == segid-of-thread
           -> no change
           |  segid_old `happens_before` segid-of-this-thread
           -> Excl(segid-of-this-thread)
           |  otherwise
           -> ShM
      */
      POrd  ord;
      VtsID vtsid_old = SVal__get_VTS(svOld);
      ord = VtsID__getOrdering(vtsid_old,tvid);
      if (ord == POrd_EQ) {
         /* no change */
         svNew = SVal__mk_ExclW(tvid);
         stats__msm_write_ExclW_nochange++;
         goto out;
      }
      else if (ord == POrd_LT) {
         /* -> Excl(segid-of-this-thread) */
         svNew = SVal__mk_ExclW(tvid);
         stats__msm_write_ExclW_transfer++;
         goto out;
      }
      else {
         /* This location has been accessed by precisely two threads.
            Make an appropriate tset. */

         WordSetID lset_acc = main_get_LockSet( acc_thr, False /*!allLocks, only write locks*/ );

         // Thread does not hold any locks by parallel write
         if (HG_(isEmptyWS)(univ_lsets, lset_acc)) {
            svNew = SVal__mk_Race();
            record_race_info( acc_thr, acc_addr, szB, True/*isWrite*/ );
            stats__msm_write_ExclW_to_Race++;
            goto out;
         }
         else {
            /* Enter the shared-modified (ShM) state. */
            svNew = SVal__mk_ShM( vtsid_old, lset_acc );
            stats__msm_write_ExclW_to_ShM++;
            goto out;
         }
         /*NOTREACHED*/
      }
      /*NOTREACHED*/
   }

   /* ExclusiveRead */
   else if (SVal__is_ExclR(svOld)) {
      // I believe is identical to case for read Excl
      // apart from enters ShM rather than ShR
      /* read Excl(segid)
           |  segid_old == segid-of-thread
           -> no change
           |  segid_old `happens_before` segid-of-this-thread
           -> Excl(segid-of-this-thread)
           |  otherwise
           -> ShM
      */
      POrd  ord;
      VtsID vtsid_old = SVal__get_VTS(svOld);
      ord = VtsID__getOrdering(vtsid_old,tvid);

      if (ord == POrd_EQ || ord == POrd_LT) {
          /* -> Excl(segid-of-this-thread) */
          svNew = SVal__mk_ExclW(tvid);
          stats__msm_write_ExclR_to_ExclW++;
          goto out;
      }
      else {
         /* Enter the shared-modified (ShM) state. */
         WordSetID lset_acc = main_get_LockSet( acc_thr, /*only w-held locks*/ False ); /* False ==> use only w-held locks */
         /* This location has been accessed by precisely two threads.
            Make an appropriate tset. */

         //Thread does not hold any locks by parallel write
         if (HG_(isEmptyWS)(univ_lsets, lset_acc)) {
            svNew = SVal__mk_Race();
            record_race_info( acc_thr, acc_addr, szB, True/*isWrite*/ );
            stats__msm_write_ExclR_to_Race++;
            goto out;
         }
         else {
            svNew = SVal__mk_ShM( vtsid_old, lset_acc );
            stats__msm_write_ExclR_to_ShM++;
            goto out;
         }
         /*NOTREACHED*/
      }
      /*NOTREACHED*/
   }

   /* Shared-Readonly */
   else if (SVal__is_ShR(svOld)) {
      /* write Shared-Readonly(threadset, lockset)
         We move to ShM state, add this thread to the
         threadset and refine the lockset accordingly.
         If the lockset becomes empty, complain. */
      VtsID vtsid_old = SVal__get_VTS(svOld);

      WordSetID lset_old = SVal__get_lset(svOld);
      WordSetID lset_acc = main_get_LockSet( acc_thr, /*only w-held locks*/ False ); /* False ==> use only w-held locks */
      WordSetID lset_new = HG_(intersectWS)( univ_lsets, lset_old, lset_acc );

      if (HG_(isEmptyWS)(univ_lsets, lset_new)) {
         POrd  ord;
         ord = VtsID__getOrdering(vtsid_old,tvid);

         if (ord == POrd_EQ || ord == POrd_LT ) {
            /* -> Excl(segid-of-this-thread) */
            svNew = SVal__mk_ShM(tvid, lset_new);
            stats__msm_write_ShR_to_ShM++;
            goto out;
         }
         else {
            svNew = SVal__mk_Race();
            record_race_info( acc_thr, acc_addr, szB, True/*isWrite*/);
            stats__msm_write_ShR_to_Race++;
            goto out;
         }
         /*NOTREACHED*/
      }
      else {
         svNew = SVal__mk_ShM( vtsid_old, lset_new );
         stats__msm_write_ShR_to_ShM++;
         goto out;
      }
      /*NOTREACHED*/
   }

   /* Shared-Modified*/
   else if (SVal__is_ShM(svOld)) {
      // TODO: Describe what we are doing here
      WordSetID lset_old = SVal__get_lset(svOld);
      WordSetID lset_acc = main_get_LockSet( acc_thr, /*only w-held locks*/ False ); /* False ==> use only w-held locks */
      WordSetID lset_new = HG_(intersectWS)( univ_lsets,
                                             lset_old,
                                             lset_acc );

      VtsID vtsid_old = SVal__get_VTS(svOld);

//      /* lockset */
//      if (lset_old != lset_new)
//         record_last_lock_lossage(a, lset_old, lset_new);

      if ( HG_(isEmptyWS)(univ_lsets, lset_new) ) {
         POrd  ord;
         ord = VtsID__getOrdering(vtsid_old,tvid);

         if (ord == POrd_EQ || ord == POrd_LT ) {
            /* -> Excl(segid-of-this-thread) */
            svNew = SVal__mk_ExclW(tvid);
            stats__msm_write_ShM_to_ExclW++;
            goto out;
         }
         else {
            svNew = SVal__mk_Race();
            record_race_info( acc_thr, acc_addr, szB, True/*!isWrite*/ );
            stats__msm_write_ShM_to_Race++;
            goto out;
         }
         /*NOTREACHED*/
      }
      else {
         svNew = SVal__mk_ShM( tvid, lset_new );
         stats__msm_write_ShM_to_ShM++;
         goto out;         
      }
      /*NOTREACHED*/
   }
   
   /* SpinVar */
   else if (SVal__is_SpinVar(svOld)) {
      Bool wasALock=False;

      if(HG_(clo_detect_mutex)) wasALock=HG_(onSpinWrite) ( acc_thr,  atomic, acc_addr, szB );

      if(wasALock) {
         if(PRINT_spin_lockset_msm) VG_(printf)("%s:msm_write use lockset for %#lx\n",__FILE__,acc_addr);

         //svNew = SVal__mk_SpinVar( SVal__get_VTS(svOld) ); //TODO TODO : is it right ?
         //svNew = SVal__mk_SpinVar( svOld ); //TODO TODO : is it right ?
         svNew = svOld;

      } else {
         if(PRINT_spin_lockset_msm) VG_(printf)("%s:msm_write use happens-before for %#lx\n",__FILE__,acc_addr);

         svNew = SVal__mk_SpinVar(tvid);

         VtsID__rcdec(tvid);
         acc_thr->vid = VtsID__tick(tvid, acc_thr);
         VtsID__rcinc(acc_thr->vid);
      }
      if(PRINT_spin_lockset_msm) {
         VG_(printf)("%s:msm_write (%#lx) acc_thr->vid=",__FILE__,acc_addr);VtsID__pp(tvid);VG_(printf)("\n");
         if( SVal__has_VTS(svOld) ) {
            VG_(printf)("%s:msm_write (%#lx) svOld=",__FILE__,acc_addr);VtsID__pp(svOld);VG_(printf)("\n");
         }
         if( SVal__has_VTS(svNew) ) {
            VG_(printf)("%s:msm_write (%#lx) svNew=",__FILE__,acc_addr);VtsID__pp(svNew);VG_(printf)("\n");
         }
      }
      goto out;
   }

   /* Race */
   else if (SVal__is_Race(svOld)) {
      svNew = SVal_RACE;
      goto out;
   }

   /* NoAccess */
   if (SVal__is_NoAccess(svOld)) {
      // FIXME: complain if accessing here
      // FIXME: transition to Excl?
      if (0)
      VG_(printf)(
         "msm__handle_write_aligned_32(thr=%p, addr=%p): NoAccess\n",
         acc_thr, (void*)acc_addr );
      stats__msm_write_NoAccess++;      
      svNew = SVal_NOACCESS; /*NOCHANGE*/
      goto out;
   }

   /* hmm, bogus state */
   VG_(printf)("msm__handle_write_aligned_32: bogus old state 0x%llx\n",
               svOld);
   tl_assert(0);

  out:
   if ( svNew != svOld ) {
      if ( HG_(clo_show_conflicts) ) {
         if (SVal__is_tracked(svOld) && SVal__is_tracked(svNew)) {
            event_map_bind( acc_addr, szB, True/*isWrite*/, acc_thr );
            stats__msm_read_change++;
         }
      }
      if ( HG_(clo_trace_addr) == acc_addr ) {
         msm__show_state_change(acc_thr, acc_addr, szB, 'w', svOld, svNew );
         if(PRINT_spin_lockset_msm) {
            if( SVal__has_VTS(svOld) ) {
               VG_(printf)("TRACE: (%#lx) svOld=",acc_addr);VtsID__pp(svOld);VG_(printf)("\n");
            }
            if( SVal__has_VTS(svNew) ) {
               VG_(printf)("TRACE: (%#lx) svNew=",acc_addr);VtsID__pp(svNew);VG_(printf)("\n");
            }
         }
      }
   }
   if( tvid != acc_thr->vid ) {
      if(PRINT_spin_lockset_msm) {
         {
            VG_(printf)("TRACE-wr: PRE  : acc_thr->vid=");VtsID__pp(tvid);VG_(printf)("\n");
         }
         {
            VG_(printf)("TRACE-wr: POST : acc_thr->vid=");VtsID__pp(acc_thr->vid);VG_(printf)("\n");
         }
      }
   }
   VtsID__rcdec(tvid); // SCHMALTZ : logging
   return svNew;
}


#define DEFINED__libhb_uka_shutdown
void libhb_uka_shutdown ( Bool show_stats )
{
   if (show_stats) {
      VG_(printf)("%s","<<< BEGIN libhb stats >>>\n");
/*
stats__msm_read_ExclW_nochange  
stats__msm_read_ExclW_transfer  
stats__msm_read_ExclW_to_ShR    
stats__msm_read_ExclW_to_ExclR  
stats__msm_read_ExclW_to_ShM    
stats__msm_read_ExclW_to_Race   
stats__msm_read_ExclR_nochange  
stats__msm_read_ExclR_transfer  
stats__msm_read_ExclR_to_ShR    
stats__msm_read_ShR_to_ShR      
stats__msm_read_ShM_to_ShM      
stats__msm_read_ShM_to_Race     
stats__msm_read_ShM_to_ExclR    
stats__msm_read_New_to_ExclR    
stats__msm_read_New_to_ExclW    
stats__msm_read_NoAccess        

stats__msm_write_ExclW_nochange  
stats__msm_write_ExclW_transfer  
stats__msm_write_ExclW_to_ShM    
stats__msm_write_ExclW_to_Race   
stats__msm_write_ExclR_to_ShM    
stats__msm_write_ExclR_to_ExclW  
stats__msm_write_ExclR_to_Race   
stats__msm_write_ShR_to_ShM      
stats__msm_write_ShR_to_Race     
stats__msm_write_ShM_to_ShM      
stats__msm_write_ShM_to_Race     
stats__msm_write_ShM_to_ExclW    
stats__msm_write_New_to_ExclW    
stats__msm_write_NoAccess        
*/
      VG_(printf)( "stats__msm_read_ExclW_nochange  = %'12lu\n", stats__msm_read_ExclW_nochange  );
      VG_(printf)( "stats__msm_read_ExclW_transfer  = %'12lu\n", stats__msm_read_ExclW_transfer  );
      VG_(printf)( "stats__msm_read_ExclW_to_ShR    = %'12lu\n", stats__msm_read_ExclW_to_ShR    );
      VG_(printf)( "stats__msm_read_ExclW_to_ExclR  = %'12lu\n", stats__msm_read_ExclW_to_ExclR  );
      VG_(printf)( "stats__msm_read_ExclW_to_ShM    = %'12lu\n", stats__msm_read_ExclW_to_ShM    );
      VG_(printf)( "stats__msm_read_ExclW_to_Race   = %'12lu\n", stats__msm_read_ExclW_to_Race   );
      VG_(printf)( "stats__msm_read_ExclR_nochange  = %'12lu\n", stats__msm_read_ExclR_nochange  );
      VG_(printf)( "stats__msm_read_ExclR_transfer  = %'12lu\n", stats__msm_read_ExclR_transfer  );
      VG_(printf)( "stats__msm_read_ExclR_to_ShR    = %'12lu\n", stats__msm_read_ExclR_to_ShR    );
      VG_(printf)( "stats__msm_read_ShR_to_ShR      = %'12lu\n", stats__msm_read_ShR_to_ShR      );
      VG_(printf)( "stats__msm_read_ShM_to_ShM      = %'12lu\n", stats__msm_read_ShM_to_ShM      );
      VG_(printf)( "stats__msm_read_ShM_to_Race     = %'12lu\n", stats__msm_read_ShM_to_Race     );
      VG_(printf)( "stats__msm_read_ShM_to_ExclR    = %'12lu\n", stats__msm_read_ShM_to_ExclR    );
      VG_(printf)( "stats__msm_read_New_to_ExclR    = %'12lu\n", stats__msm_read_New_to_ExclR    );
      VG_(printf)( "stats__msm_read_New_to_ExclW    = %'12lu\n", stats__msm_read_New_to_ExclW    );
      VG_(printf)( "stats__msm_read_NoAccess        = %'12lu\n", stats__msm_read_NoAccess        );
      VG_(printf)( "\n" );
      VG_(printf)( "stats__msm_write_ExclW_nochange  = %'12lu\n", stats__msm_write_ExclW_nochange  );
      VG_(printf)( "stats__msm_write_ExclW_transfer  = %'12lu\n", stats__msm_write_ExclW_transfer  );
      VG_(printf)( "stats__msm_write_ExclW_to_ShM    = %'12lu\n", stats__msm_write_ExclW_to_ShM    );
      VG_(printf)( "stats__msm_write_ExclW_to_Race   = %'12lu\n", stats__msm_write_ExclW_to_Race   );
      VG_(printf)( "stats__msm_write_ExclR_to_ShM    = %'12lu\n", stats__msm_write_ExclR_to_ShM    );
      VG_(printf)( "stats__msm_write_ExclR_to_ExclW  = %'12lu\n", stats__msm_write_ExclR_to_ExclW  );
      VG_(printf)( "stats__msm_write_ExclR_to_Race   = %'12lu\n", stats__msm_write_ExclR_to_Race   );
      VG_(printf)( "stats__msm_write_ShR_to_ShM      = %'12lu\n", stats__msm_write_ShR_to_ShM      );
      VG_(printf)( "stats__msm_write_ShR_to_Race     = %'12lu\n", stats__msm_write_ShR_to_Race     );
      VG_(printf)( "stats__msm_write_ShM_to_ShM      = %'12lu\n", stats__msm_write_ShM_to_ShM      );
      VG_(printf)( "stats__msm_write_ShM_to_Race     = %'12lu\n", stats__msm_write_ShM_to_Race     );
      VG_(printf)( "stats__msm_write_ShM_to_ExclW    = %'12lu\n", stats__msm_write_ShM_to_ExclW    );
      VG_(printf)( "stats__msm_write_New_to_ExclW    = %'12lu\n", stats__msm_write_New_to_ExclW    );
      VG_(printf)( "stats__msm_write_NoAccess        = %'12lu\n", stats__msm_write_NoAccess        );


      
      VG_(printf)("%s","<<< END libhb stats >>>\n");
      VG_(printf)("%s","\n");

   }
}
