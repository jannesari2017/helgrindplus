/*
 * hg_loops.h
 *
 * (c) 2009 Univercity of Karlsruhe, Germany
 */

#ifndef HG_LOOPS_H_
#define HG_LOOPS_H_

#include "hg_lock_n_thread.h" // ThrLoopExtends

/*----------------------------------------------------------------*/
/*--- Command line options                                     ---*/
/*----------------------------------------------------------------*/

/* Enable/Disable the control flow graph analysis (cfg)
 * 0 = disable cfg
 * 1 = enable cfg (default)
 */
extern Int HG_(clo_control_flow_graph);

extern Bool HG_(clo_detect_mutex);

extern Bool hg_cfg__clo_verbose_control_flow_graph;
extern Bool hg_cfg__clo_show_control_flow_graph;
extern Bool hg_cfg__clo_ignore_pthread_spins;
extern Bool hg_cfg__clo_show_spin_reads;

extern UWord hg_cfg__clo_show_bb;
extern UWord hg_cfg__clo_test_da;

/*----------------------------------------------------------------*/
/*--- Interface for hg_main                                    ---*/
/*----------------------------------------------------------------*/

typedef
   enum {
      HgL_Pre_Instrumentation,
      HgL_Instrument_Statement,
      HgL_Post_Instrumentation
   }
   HG_LOOPS_INSTR_MODE;
   
/* Called by hg_instrument */

Bool hg_loops_instrument_bb ( HG_LOOPS_INSTR_MODE mode,
                              IRSB* bbIn,
                              IRSB* bbOut,
                              VgCallbackClosure* closure,
                              VexGuestLayout* layout,
                              VexGuestExtents* vge,
                              IRStmt* st);

Bool hg_loops_is_spin_reading ( void );

#define __LOOP_ALGO_ID 1


#include "hg_interval.h"
typedef struct _SpinProperty SpinProperty;

#define SP_PROP_MAGIC ((UInt)0xDA6387FC)
#define SP_MAGIC_ASSERT(__spp) tl_assert((__spp)->sp_magic==SP_PROP_MAGIC);
struct _SpinProperty {
   UInt sp_magic;
   IntervalUnion * codeBlock;

   WordFM * spinVariables;

   Bool isMutex; // default False
   //Lock * lk; Wrong : Many locks possible for the same code block
   //WordFM *
};

SpinProperty * HG_(get_lastSpinProperty)( void );
SpinProperty * HG_(lookupSpinProperty)(IntervalUnion * codeBlock);

/* Thread Extend Management */

inline ThreadLoopExtends* hg_loops__create_ThreadLoopExtends (void);
inline void* hg_loops__get_ThreadLoopExtends_opaque ( ThreadLoopExtends* );
inline void  hg_loops__set_ThreadLoopExtends_opaque ( ThreadLoopExtends*, void* );

/* Initialization / Finalization / Constructors */

void hg_loops_init ( ThreadLoopExtends* (*get_current_LoopExtends) (void) );
void hg_loops_shutdown ( Bool show_stats );

#endif /* HG_LOOPS_H_ */
