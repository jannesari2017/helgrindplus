/**
 * This file was includes in hg_main.c
 */


#include "hg_interval.h"

#include "pub_tool_threadstate.h"

#define HANDLE_BARRIER 0

typedef struct _SpinLock {
   Lock * sLk;
   //ThreadId lastReader;

#if HANDLE_BARRIER
   /*
    *  necessary to build hb-relation between all Threads of a Barrier,
    *  not only (last Thread) -hb-> (first Threads)
    *
    *  The first Threads are only Readers (no write => no HB-Relation)
    *
    *  TAKE CARE : it should not handle conditions as Barriers.
    */
   WordFM* lastSpinReaders;
#endif

   ThreadId lastSpinWriter;  // in spin loop

   ThreadId lastReleaseWriter; // last writer not in spin loop
   Bool wasReleasedBefore;

   Bool wasALock;

   SpinProperty * sp;

   Addr writeInstruction;
} SpinLock;

/**
 * Addr -> SpinLock
 */
static WordFM * map_spin_locks = NULL;

/**
 * Code instruction list where lock were acquired
 */
static WordFM * map_locks_instructions = NULL;

static void init_spin_locks( void )
{  // TODO : call it in the right place (main initialisation)
   if(map_spin_locks==NULL) {
      map_spin_locks=VG_(newFM)(HG_(zalloc), MALLOC_CC(init_spin_locks), HG_(free), NULL);

      map_locks_instructions=VG_(newFM)(HG_(zalloc), MALLOC_CC(map_locks_instructions), HG_(free), NULL);
   }
}

static Bool lastWriterHasExited( SpinLock * lk )
{
   return False; // TODO : disabled

   if(lk->lastSpinWriter!=0 && lk->sp && (VG_(get_running_tid)()==lk->lastSpinWriter /*HG_(onExitSpinLoop) has to be run by the right thread*/))
   {
      if(HG_(ivUnionContains)(lk->sp->codeBlock, VG_(get_IP)(lk->lastSpinWriter))==NULL) {
         VG_(printf)("lastWriterHasExited : %d\n",lk->lastSpinWriter);
         HG_(onExitSpinLoop)(VG_(get_IP)(lk->lastSpinWriter),lk->sp->codeBlock);
         return True;
      }
   }
   return False;
}

static
void set_lastReleaseWriter(SpinLock * lk, ThreadId tid)
{
   Bool b;
   Thread* thr;
   thr = map_threads_maybe_lookup( tid );

   if(lk->lastReleaseWriter==tid) {
      // TODO: check if it is normal that a release happens in two writes
      return;
   }

   lk->lastReleaseWriter=tid;
   tl_assert(thr);
   /**
    * True if the current Thread is holding 'lk'.
    * In this case, it was supposed to be a lock-release instruction.
    */
   lk->wasReleasedBefore=
         //HG_(elemWS)( univ_lsets, thr->locksetA, (Word)lk->sLk->guestaddr );
         HG_(elemWS)( univ_lsets, thr->locksetA, (Word)map_locks_maybe_lookup( lk->sLk->guestaddr ) );
   //TODO :dirty

   if(PRINT_spin_lockset_log)
      VG_(printf)("wasReleasedBefore (tid=%d) : %lx : %s\n",tid,lk->sLk->guestaddr,(lk->wasReleasedBefore?"TRUE":"FALSE"));

   if(PRINT_spin_lockset_ev && !(lk->wasReleasedBefore)) {
      {
         Char fnName[100];
         UWord ip=VG_(get_IP)(tid);
         Bool hasFnName=VG_(get_fnname_w_offset)(ip, fnName, sizeof(fnName));
         if(!hasFnName) {
            //VG_(memcpy)(fnName,"??",3);
            fnName[0]=0;
         }
         /*
          * Invalidated : cannot be a lock ; written outside a spin-loop
          */
         VG_(printf)("%lx was invalidated by write in %lx: %s\n",lk->sLk->guestaddr,ip,fnName);
      }
   }
}


static
SpinLock* mk_SpinLock( Addr ga, ThreadId tid )
{
   Lock* lock = mk_LockN(LK_mbRec, ga);
   SpinLock* spinLock = HG_(zalloc)(MALLOC_CC(mk_SpinLock),sizeof(*spinLock));
   lock->appeared_at = VG_(record_ExeContext)( tid, 0 );
   tl_assert(HG_(is_sane_LockN)(lock));
   VG_(addToFM)( map_spin_locks, (Word)ga, (Word)spinLock );

#if HANDLE_BARRIER
   spinLock->lastSpinReaders=VG_(newFM)(HG_(zalloc), MALLOC_CC(spinLock->lastSpinReaders), HG_(free), NULL);
#endif

   spinLock->lastReleaseWriter=0;
   spinLock->wasALock=False;
   spinLock->lastSpinWriter=0;

   spinLock->sLk=lock;
   spinLock->wasALock=False;
   return spinLock;
}

static
SpinLock * map_spin_locks_lookup_or_create ( Addr ga, ThreadId tid )
{
   Bool  found;
   SpinLock* oldlock = NULL;
   tl_assert(HG_(is_sane_ThreadId)(tid));
   found = VG_(lookupFM)( map_spin_locks,
                          NULL, (Word*)&oldlock, (Word)ga );
   if (!found) {
      tl_assert(oldlock==NULL);
      return mk_SpinLock(ga,tid);
   } else {
      tl_assert(oldlock != NULL);
      //tl_assert(HG_(is_sane_LockN)(oldlock));
      //tl_assert(oldlock->guestaddr == ga);
      return oldlock;
   }
}

static void pp_ExeContext( ThreadId tid )
{
   ExeContext* ec=VG_(record_ExeContext)( tid, 0 );
   VG_(pp_ExeContext)(ec);
   /*tid=(tid==1?2:1);
   if(VG_(is_valid_tid)(tid)) {
      VG_(printf)("pp_ExeContext( %d )\n",tid);
      ec=VG_(record_ExeContext)( tid, 0 );
      VG_(pp_ExeContext)(ec);
   }*/
}

static
SpinLock * map_spin_locks_lookup ( Addr ga )
{
   Bool  found;
   SpinLock* oldlock = NULL;
   //tl_assert(HG_(is_sane_ThreadId)(tid));
   found = VG_(lookupFM)( map_spin_locks,
                          NULL, (Word*)&oldlock, (Word)ga );
   if (!found) {
      tl_assert(oldlock==NULL);
      return oldlock;
   } else {
      tl_assert(oldlock != NULL);
      return oldlock;
   }
}

Bool HG_(onSpinRead) ( Thr* acc_thr, Bool atomic,
                       Addr acc_addr, SizeT szB )
{
   SpinProperty * sp = HG_(get_lastSpinProperty)();
   ThreadId tid = VG_(get_running_tid)();
   init_spin_locks();

   if(0)if(acc_addr==0x807a2c0) { // Test 11, should be TN
      /**
       * Test code used to skip variables thought to be locks at first,
       * but corrected later.
       */
      VG_(printf)("RD~~~~Skip %lx\n",acc_addr);
      return False;
   }

   if(PRINT_spin_lockset_log) {
      VG_(printf)("RD-tid==%d && acc_addr=0x%lx && IP==0x%lx\n",tid,acc_addr,VG_(get_IP)(tid));
   }

   // TODO : sp->spinVariables unused !?
   VG_(addToFM)( sp->spinVariables, (UWord)acc_addr, (UWord)0 );

   if(1) {
      SpinLock * lk = map_spin_locks_lookup_or_create( (Addr)acc_addr, tid );
      lastWriterHasExited(lk);
      //lk->lastReader=tid;

#if HANDLE_BARRIER
      VG_(addToFM)( lk->lastSpinReaders, (UWord)tid, (UWord)sp->codeBlock );
#endif

      lk->sp=sp;

      return lk->wasALock;
   }

   //lockN_acquire_writer( lk, thr );
}

Bool HG_(onSpinWrite) ( Thr* acc_thr, Bool atomic,
                       Addr acc_addr, SizeT szB )
{
   SpinProperty * sp = HG_(get_lastSpinProperty)();
   ThreadId tid = VG_(get_running_tid)();
   Bool isALock=False;
   Thread* thr;

   if(PRINT_spin_lockset_log)
      VG_(printf)("WR-enter:adr=%lx sp=%p\n",acc_addr,sp);

   if(sp&&!HG_(ivUnion_isSane)(sp->codeBlock)) {
      SP_MAGIC_ASSERT(sp);
      tl_assert2(0,"sp->codeBlock=%p\n",sp->codeBlock);
   }

   thr = map_threads_maybe_lookup( tid );
   tl_assert(thr); /* cannot fail - Thread* must already exist */

   if(!(atomic&&hg_loops_is_spin_reading())&& // may acquire in a atomic instruction before the spin
         (
         ((sp==NULL)||(HG_(ivUnion_cntParts)(sp->codeBlock)==0)/*TODO : OK?*/) // not in a spin
         // ||(!HG_(ivUnionContains)(sp->codeBlock, VG_(get_IP)(tid))
         //|| (!VG_(lookupFM)(sp->spinVariables,NULL,NULL,acc_addr)) //(!atomic)
         ||(!atomic&&HG_(isEmptyWS)( univ_lsets, thr->locksetA )) // not atomic && not protected => no lock
         )) {
      // we are not in a spin loop (releasing lock ?)
      SpinLock * lk = map_spin_locks_lookup( acc_addr );
      Int n;


      //tl_assert(!(hg_loops_is_spin_reading()));

      if(!lk) return isALock;

      // TODO : check if set_lastReleaseWriter conflict with lastWriterHasExited below
      lastWriterHasExited(lk);

      set_lastReleaseWriter(lk,tid);
      if(!(lk->wasReleasedBefore)) {
         lk->wasALock=False;
      }

      if(PRINT_spin_lockset_ev) {
         Char fnName[100];
         Bool hasFnName=VG_(get_fnname_w_offset)(VG_(get_IP)(tid), fnName, sizeof(fnName));
         VG_(printf)("WR-%d %s %lx @code:%lx-%s ?\n",
               tid,
               (lk->wasALock?"releasing lock":"writing non lock"),
               acc_addr,
               VG_(get_IP)(tid),
               (char*)(hasFnName?fnName:"??"));
         if(sp) {
            //tl_assert(HG_(ivUnion_isSane)(sp->codeBlock));
            HG_(ivUnionDump_ext)(sp->codeBlock,True);
         }
      }
      {
         if(PRINT_spin_lockset_log) {
            pp_ExeContext(tid);
         }

         /// ----------------------- unlocked

         if(!lk->sLk->heldBy) {
            if(PRINT_spin_lockset_log) VG_(printf)("WR-ERR:releasing not locked lock (tid=%d, acc_addr=%p)\n",tid,(void*)acc_addr);
            if(lk->lastReleaseWriter==tid && lk->wasALock) {
               isALock=True; // exception
            } else {
               lk->wasALock=False;
            }
            goto error;
         }

         n=VG_(elemBag)( lk->sLk->heldBy, (Word)thr );
         if(n==0) {
            Thread* realOwner = (Thread*)VG_(anyElementOfBag)( lk->sLk->heldBy );

            if(PRINT_spin_lockset_ev) VG_(printf)("WR-ERR~~locked by another thread : self.tid=%d, realOwner=%d (acc_addr=%p)\n",tid,realOwner->coretid,(void*)acc_addr);
            //hg_loops_lastSpinProperty
            // it's not a lock, so their is no use to hold it
            lockN_release( lk->sLk, realOwner );
            evhH__pre_thread_releases_lock (realOwner, (Addr)acc_addr, False/*!isRDWR*/ );

            lk->wasALock=False;

            goto error;
         }


         lockN_release( lk->sLk, thr );

         evhH__pre_thread_releases_lock (thr, (Addr)acc_addr, False/*!isRDWR*/ );

         if(PRINT_spin_lockset_ev) VG_(printf)("WR ~~~ %d released %lx !!!\n",tid,acc_addr);

         isALock=True; // it's a lock release

     error:
         tl_assert(HG_(is_sane_LockN)(lk->sLk));
      }
   } else {
      //Char fnName[100];
      //Bool hasFnName=VG_(get_fnname)(VG_(get_IP)(tid), fnName, sizeof(fnName));
      // a lock was detected in pthread_create, but never released

      //grep "trying to acquire lock" a | grep -v -E "pthread_.*_destroy"
      //if(!(hasFnName&&VG_(string_match)("pthread_create*", fnName)))
      //if(!(hasFnName&&VG_(string_match)("pthread_cond_sign*", fnName)))
      if(HG_(ivUnion_cntParts)(sp->codeBlock)>0||atomic)
         //their is no use in empty spin blocks
         // exception : atomic locks not in spin-loop
      {
         // we are inhg_loops_lastSpinProperty a spin loop (acquiring lock ?)
         SpinLock * lk = map_spin_locks_lookup_or_create( (Addr)acc_addr, tid );
         lastWriterHasExited(lk);

         if(lk->lastReleaseWriter&&!(lk->wasReleasedBefore)) {
            if(PRINT_spin_lockset_ev) {
               VG_(printf)("WR-NOT A LOCK %d @mem %lx !!!\n",tid,acc_addr);
               pp_ExeContext(tid);
            }
            lk->lastSpinWriter=0;
            lk->wasALock=False;
         } else {
            isALock=True;
            lk->lastSpinWriter=tid;
            //hg_loops_lastSpinProperty
            lk->sp=sp;
            lk->wasALock=True;

            lk->writeInstruction=VG_(get_IP)(tid);

            if(PRINT_spin_lockset_ev) {
               Char fnName[100];
               Bool hasFnName=VG_(get_fnname)(VG_(get_IP)(tid), fnName, sizeof(fnName));
               VG_(printf)("WR-%d trying to acquire lock %lx in %s@%lx ?\n",tid,acc_addr,(char*)(hasFnName?fnName:"??"),VG_(get_IP)(tid));
               if(!lk->lastReleaseWriter) VG_(printf)("WR-%d : was first access on %lx (@%lx)\n",tid,acc_addr,VG_(get_IP)(tid));
               VG_(printf)("WR-(%d,%lx)",tid,acc_addr);HG_(ivUnionDump_ext)(lk->sp->codeBlock,True);
            }
            if(PRINT_spin_lockset_log) {
               pp_ExeContext(tid);
            }
         }

         if(atomic&&HG_(ivUnion_cntParts)(sp->codeBlock)==0) {
            /**
             * Atomic lock not in spin loop
             */
            HG_(onExitSpinLoop)(lk->writeInstruction,lk->sp->codeBlock);
         }
      } else {
         if(PRINT_spin_lockset_ev) VG_(printf)("WR-Empty spin block ignored %d @mem %lx !!!\n",tid,acc_addr);
      }
   }

   if(PRINT_spin_lockset_log) VG_(printf)("WR-exit: sp=%p\n",sp);

   if(PRINT_spin_lockset_ev) VG_(printf)("WR-exit: sp=%p, ip=%lx / %s\n",sp,VG_(get_IP)(tid),(isALock?"lockset":"happens-before"));

   return isALock;
}

void HG_(onExitSpinLoop) ( HWord destination, IntervalUnion * ivu )
{
   ThreadId tid = VG_(get_running_tid)();
   Bool lockAcquired = False;

   if(!map_spin_locks) return;

   if(PRINT_spin_lockset_log) VG_(printf)("EX-enter HG_(onExitSpinLoop)\n");

   tl_assert(HG_(ivUnionContains)(ivu, destination)==NULL);
   if(000000000000000000000000000000000000000000) // What to do ?
   {
      Addr ips[VG_(clo_backtrace_size)+1];
      UInt n_ips,i;
      n_ips = VG_(get_StackTrace)( tid, ips, VG_(clo_backtrace_size),
                                         NULL/*array to dump SP values in*/,
                                         NULL/*array to dump FP values in*/,
                                         0 );

      for(i=1/* i=0 not in the stack if destination is a return */;i<n_ips;i++)
      {
         if(HG_(ivUnionContains)(ivu, ips[i])!=NULL)
         {
            Char fnName[100];
            Bool hasFnName=VG_(get_fnname_w_offset)(ips[i], fnName, sizeof(fnName));
            VG_(printf)("EX-Wrong HG_(onExitSpinLoop) : return (ips[%d]=%lx:%s in spin)\n",i,ips[i],(hasFnName?fnName:"??"));
            HG_(ivUnionDump_ext)(ivu,True);
            tl_assert(0);
            return;
         }
      }
   }

   if(PRINT_spin_lockset_log) {
      Char fnName[100];
      Bool hasFnName=VG_(get_fnname)(destination, fnName, sizeof(fnName));
      VG_(printf)("EX-%d acquired some lock at code@%lx : %s ?",tid,destination,(hasFnName?fnName:"??"));
      if(ivu) {
         VG_(printf)("\tivu=");
         HG_(ivUnionDump_ext)( ivu, True );
      }
      VG_(printf)("\n");
   }

   if(ivu) {
      //SpinProperty * sp = HG_(lookupSpinProperty)(ivu);

      SpinLock * lk;
      Addr adr;

      //VG_(printf)(__AT__"\n");
      VG_(initIterFM)( map_spin_locks );
      while( VG_(nextIterFM)( map_spin_locks, (UWord*)&adr, (UWord*)&lk ) ) {
         //tl_assert(lk);
         if(lk->lastSpinWriter==0) continue;

         if(PRINT_spin_lockset_log) {
            VG_(printf)("EX--codeBlock=H%lx ivu=H%lx",HG_(ivUnionHash)(lk->sp->codeBlock),HG_(ivUnionHash)(ivu));
            VG_(printf)(" tid=%d adr=%lx wasReleasedBefore=%d\tcodeBlock=",tid,adr,(Int)lk->wasReleasedBefore);
            HG_(ivUnionDump_ext)(lk->sp->codeBlock,True);
         }

         if(PRINT_spin_lockset_ev) {
            Char fnName[100];
            Bool hasFnName=VG_(get_fnname_w_offset)(lk->writeInstruction, fnName, sizeof(fnName));
            if(!hasFnName) {
               //VG_(memcpy)(fnName,"??",3);
               fnName[0]=0;
            }
            VG_(printf)("Code instruction which write %lx was %lx: %s\n",adr,lk->writeInstruction,fnName);
         }

         /*
          * A spin can contain another spin in itself
          */
         if(lk->sp->codeBlock!=ivu) {
            continue;
         }

         if(lk->lastReleaseWriter&&!(lk->wasReleasedBefore)) {
            if(PRINT_spin_lockset_log) {
               VG_(printf)("EX-NOT A LOCK %d @mem %lx !!!\n",tid,adr);
               pp_ExeContext(tid);

               lk->lastSpinWriter = 0; //invalidate
               lk->wasALock=False;

               zsm_apply32___msm_write(map_threads_maybe_lookup( tid )->hbthr,False,adr);
            }
         } else
         if(lk->lastSpinWriter==tid) {
            Thread* thr;
            if(PRINT_spin_lockset_log) VG_(printf)("EX-\tlock is %lx\n",adr);

            /// ----------------------- locked

            thr = map_threads_maybe_lookup( tid );
            tl_assert(thr); /* cannot fail - Thread* must already exist */

            if (lk->sLk->heldBy != NULL)
            {
               tl_assert(lk->sLk->heldW);
               /* assert: lk is only held by one thread .. */
               tl_assert(VG_(sizeUniqueBag(lk->sLk->heldBy)) == 1);
               /* .. and if t
            isInLoop=HG_(ivUnionContaihat thread isn't 'thr'. */
               if(VG_(elemBag)(lk->sLk->heldBy, (Word)thr)
                         != VG_(sizeTotalBag)(lk->sLk->heldBy)) {
                  // .. then this isn't a lock in fact
                  if(PRINT_spin_lockset_ev) VG_(printf)("EX ~~~ %d try to acquire %lx !!! cannot be a lock (already held)\n",tid,adr);
                  pp_ExeContext(tid);
                  lk->lastSpinWriter = 0; //invalidate
                  lk->wasALock=False;

                  zsm_apply32___msm_write(thr->hbthr,False,adr);
                  goto __onExit_wrong_lock;
               }
               goto __onExit_already_locked;
            }
            evhH__post_thread_w_acquires_lock(thr,LK_mbRec,(Addr)adr);
            lockN_acquire_writer( lk->sLk, thr );

            lockAcquired = True;

            if(PRINT_spin_lockset_ev && !VG_(addToFM)(map_locks_instructions,lk->writeInstruction,0)) {
               Char fnName[100];
               Bool hasFnName=VG_(get_fnname_w_offset)(lk->writeInstruction, fnName, sizeof(fnName));
               if(!hasFnName) {
                  //VG_(memcpy)(fnName,"??",3);
                  fnName[0]=0;
               }
               VG_(printf)("%lx was a lock acquiring instruction (in %s)\n",lk->writeInstruction,fnName);
            }

            if(PRINT_spin_lockset_ev) {
               VG_(printf)("EX ~~~ %d acquired %lx !!!\n",tid,adr);
            }
            if(PRINT_spin_lockset_log) {
               pp_ExeContext(tid);
            }
         __onExit_already_locked:
            lk->lastSpinWriter=0; //avoid relock
         __onExit_wrong_lock: // EXIT
            ;
         } else { //lastSpinWriter!=tid

         }
      }
      VG_(doneIterFM)( map_spin_locks );

#if HANDLE_BARRIER
      if(!lockAcquired) {
         VG_(initIterFM)( map_spin_locks );
         while( VG_(nextIterFM)( map_spin_locks, (UWord*)&adr, (UWord*)&lk ) ) {
            IntervalUnion * iLoop;
            if(VG_(lookupFM)( lk->lastSpinReaders, NULL, (UWord*)&iLoop, tid)) {
               thr = map_threads_maybe_lookup( tid );
               lk->sLk->guestaddr;

               VtsID__rcdec(tvid);
               acc_thr->vid = VtsID__tick(tvid, acc_thr);
               VtsID__rcinc(acc_thr->vid);
            }
         }
         VG_(doneIterFM)( map_spin_locks );
      }
#endif


     //VG_(printf)(__AT__"\n");
   } else tl_assert(ivu);
}


Bool HG_(onMSM) ( Thr* acc_thr, Bool atomic,
                       Addr acc_addr, SizeT szB, Bool isAWrite )
{
   //ThreadId tid = VG_(get_running_tid)();
   SpinLock * sp;
   //VG
   //VG_(addToFM)( map_spin_locks, (Word)ga, (Word)spinLock );

   /*
   if(map_spin_locks) {
      VG_(initIterFM) ( map_spin_locks );
      while(VG_(nextIterFM) ( map_spin_locks, NULL, (UWord)&sp)) {
         lastWriterHasExited(sp);
      }
      VG_(doneIterFM) ( map_spin_locks );
   }
   //*/
#ifndef NDEBUG
   if(0){
      Char fnName[100];
      ThreadId tid = VG_(get_running_tid)();
      Addr ip=VG_(get_IP)(tid);
      Bool hasFnName=VG_(get_fnname)(ip, fnName, sizeof(fnName));
      if((hasFnName&&( VG_(string_match)("*pthread_mutex*", fnName)||VG_(string_match)("_L_lock*", fnName) )))
      {
         VG_(printf)("**Tid=%d, ip=%p, fnName=%s\n",tid,(void*)ip,fnName);
      }
   }
   if(hg_loops_is_spin_reading()){
      ThreadId tid = VG_(get_running_tid)();
      Addr ips[40+1];
      UInt n_ips,i;
      Char fnName0[100];
      Bool hasFnName0;
      n_ips = VG_(get_StackTrace)( tid, ips, 40,
                                         NULL/*array to dump SP values in*/,
                                         NULL/*array to dump FP values in*/,
                                         0 );
      hasFnName0=VG_(get_fnname_w_offset)(ips[0], fnName0, sizeof(fnName0));

      if(hasFnName0) {
         //VG_(printf)("##**%s Tid=%d, ip=%p, acc_addr=0x%lx, fnName=%s\n",isAWrite?"W":"R",tid,(void*)ips[0],acc_addr,fnName0);
         //tl_assert(VG_(string_match)("*ProducerConsumerQueue*", "ProducerConsumerQueue"));
         //tl_assert(VG_(string_match)("*ProducerConsumerQueue*", "ProducerConsumerQueue::~ProducerConsumerQueue()"));
         //break;
      }
      if(hasFnName0&&VG_(string_match)("*WaitLoop*", fnName0)) {
         VG_(printf)("##**%s Tid=%d, ip=%p, acc_addr=0x%lx, fnName=%s\n",isAWrite?"W":"R",tid,(void*)ips[0],acc_addr,fnName0);
      }
      if(hasFnName0&&!(0
            ||VG_(string_match)("*print*", fnName0)
            ||VG_(string_match)("*IO*", fnName0)
            ||VG_(string_match)("*mem*", fnName0)
            ||VG_(string_match)("*write*", fnName0)
            ||VG_(string_match)("Scope*", fnName0)
            ))
      for(i=0/* i=0 not in the stack if destination is a return */;i<n_ips;i++)
      {
         Char fnName[100];
         Bool hasFnName=VG_(get_fnname_w_offset)(ips[i], fnName, sizeof(fnName));

         if(hasFnName&&( 0
               //||VG_(string_match)("*::Get*", fnName)
               //||VG_(string_match)("*ProducerConsumerQueue::Get*", fnName)
               //||VG_(string_match)("*pthread_cond*", fnName)
               ||VG_(string_match)("*pthread_barrier*", fnName)
               //||VG_(string_match)("*WaitLoop*", fnName)
               ))
         {
            VG_(printf)("**%s Tid=%d, acc_addr=0x%lx, fnName[%s@%p] by fnName(%d)=%s@%p\n",
                  isAWrite?"W":"R",tid,acc_addr,fnName0,(void*)ips[0],i,fnName,(void*)ips[i]);
            break;
         }
      }
   }
#endif

   return True;
}

static
void mutex_detect_exitThread( ThreadId quit_tid )
{
   if(map_spin_locks) {
      SpinLock * sp;
      Addr addr;
      Thread * thr = map_threads_maybe_lookup( quit_tid );
      VG_(initIterFM) ( map_spin_locks );
      while(VG_(nextIterFM) ( map_spin_locks, (UWord*)&addr, (UWord*)&sp)) {


         if(sp->sLk->heldBy && !VG_(isEmptyBag)(sp->sLk->heldBy)) {
            Thread* realOwner = (Thread*)VG_(anyElementOfBag)( sp->sLk->heldBy );

            if(thr!=realOwner) continue;

            lockN_release( sp->sLk, realOwner );
            evhH__pre_thread_releases_lock (realOwner, addr, False/*!isRDWR*/ );
         }
      }
      VG_(doneIterFM) ( map_spin_locks );
   }
}
