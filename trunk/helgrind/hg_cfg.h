/*
 * hg_cfg.h
 *
 *  Created on: 08.06.2009
 *      Author: biin
 */

#ifndef HG_CFG_H_
#define HG_CFG_H_

/*----------------------------------------------------------------*/
/*--- Command line options                                     ---*/
/*----------------------------------------------------------------*/

/* Enable/Disable the control flow graph analysis (cfg)
 * 0 = disable cfg
 * 1 = enable cfg (default)
 */
extern Int HG_(clo_control_flow_graph);

extern Bool hg_cfg__clo_verbose_control_flow_graph;
extern Bool hg_cfg__clo_show_control_flow_graph;
extern Bool hg_cfg__clo_ignore_pthread_spins;
extern Bool hg_cfg__clo_show_spin_reads;

extern UWord hg_cfg__clo_show_bb;
extern UWord hg_cfg__clo_test_da;

/*----------------------------------------------------------------*/
/*---                                                          ---*/
/*----------------------------------------------------------------*/

typedef
   enum {
      HG_CFG_IM_Pre,
      HG_CFG_IM_Instr,
      HG_CFG_IM_Post
   }
   HG_CFG_INSTR_MODE;

/* Called by hg_instrument */
Bool hg_cfg_instrument_bb ( HG_CFG_INSTR_MODE mode,
                            IRSB* bbIn,
                            IRSB* bbOut,
                            VgCallbackClosure* closure,
                            VexGuestLayout* layout,
                            VexGuestExtents* vge,
                            IRStmt* st);

Bool hg_cfg_is_spin_reading ( void );

/* Initialization / Finalization / Constructors */

void hg_cfg_init ( void );
void hg_cfg_shutdown ( Bool show_stats );

#endif /* HG_CFG_H_ */
