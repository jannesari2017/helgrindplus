/*
 * hg_lsd.h
 *
 *  Created on: 08.12.2008
 *      Author: biin
 */

#ifndef __HG_LSD_H_
#define __HG_LSD_H_

#include "hg_lock_n_thread.h" // SO, Thr, ThrLSD

/*----------------------------------------------------------------*/
/*--- Command line options                                     ---*/
/*----------------------------------------------------------------*/

/* Enable/Disable the lost signal detector (lsd)
 * 0 = disable lsd (default)
 * 1 = enable lsd
 * 2 = enable lsd w/ write / read - relations 
 */
extern UInt HG_(clo_lost_signal_detector);

extern Bool hg_lsd__clo_verbose_lost_signal_catcher;
extern Bool hg_lsd__clo_verbose_lost_instr_details;
extern Bool hg_lsd__clo_verbose_write_read_relation;

/*----------------------------------------------------------------*/
/*---                                                          ---*/
/*----------------------------------------------------------------*/


typedef
   enum {
      HG_IM_BBStart,
      HG_IM_Statement,
      HG_IM_BBEnd
   }
   HG_LSD_INSTR_MODE;

/* Called by hg_instrument */
Bool hg_lsd_instrument_bb ( HG_LSD_INSTR_MODE mode,
                            IRSB* bbIn,
                            IRSB* bbOut,
                            VgCallbackClosure* closure,
                            VexGuestLayout* layout,
                            VexGuestExtents* vge,
                            IRStmt* st );

/*----------------------------------------------------------------*/
/*--- Event handler                                            ---*/
/*----------------------------------------------------------------*/

#define HG_LSD__WRITE_1(_thr,_a)    hg_lsd__rec_write((_thr),(_a))
#define HG_LSD__WRITE_2(_thr,_a)    hg_lsd__rec_write((_thr),(_a))
#define HG_LSD__WRITE_4(_thr,_a)    hg_lsd__rec_write((_thr),(_a))
#define HG_LSD__WRITE_8(_thr,_a)    hg_lsd__rec_write((_thr),(_a))
#define HG_LSD__WRITE_N(_thr,_a,_n) hg_lsd__rec_write((_thr),(_a))

#define HG_LSD__READ_1(_thr,_a)    hg_lsd__rec_read((_thr),(_a))
#define HG_LSD__READ_2(_thr,_a)    hg_lsd__rec_read((_thr),(_a))
#define HG_LSD__READ_4(_thr,_a)    hg_lsd__rec_read((_thr),(_a))
#define HG_LSD__READ_8(_thr,_a)    hg_lsd__rec_read((_thr),(_a))
#define HG_LSD__READ_N(_thr,_a,_n) hg_lsd__rec_read((_thr),(_a))

void hg_lsd__rec_write ( ThrLSD* thr, Addr a );
void hg_lsd__rec_read  ( ThrLSD* thr, Addr a );

void hg_lsd__cond_signal_pre   ( ThrLSD* thr, void* cond, SO* so );
void hg_lsd__cond_wait_pre     ( ThrLSD* thr, void* cond );
Bool hg_lsd__cond_wait_post    ( ThrLSD* thr, void* cond, void* mutex );
SO*  hg_lsd__cond_direct_so    ( ThrLSD* thr, void* cond, Bool annotation );
void hg_lsd__cond_wait_timeout ( ThrLSD* thr, void* cond, SO* so );


void hg_lsd__mutex_lock   ( ThrLSD*, LockLSD* );
void hg_lsd__mutex_unlock ( ThrLSD*, LockLSD* );

/* Initialization / Finalization / Constructors */

inline LockLSD* hg_lsd__mutex_create ( LockKind );
inline void hg_lsd__mutex_free(LockLSD* lock);

inline ThrLSD* hg_lsd__create_ThrLSD (void);
inline void* hg_lsd__get_ThrLSD_opaque ( ThrLSD* );
inline void  hg_lsd__set_ThrLSD_opaque ( ThrLSD*, void* );

void hg_lsd_init( ThrLSD* (*get_current_ThrLSD)( void ),
                  void    (*record_error_Misc) (ThrLSD*, char*),
                  void    (*annotate_wait) (void*, void*),
                  void    (*annotate_signal) (void*) );
void hg_lsd_shutdown ( Bool show_stats );

#endif /* __HG_LSD_H_ */
