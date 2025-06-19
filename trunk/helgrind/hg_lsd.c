 /*
 * hg_lsd.c
 *
 *  Created on: 08.12.2008
 *      Author: biin
 */

#include "pub_tool_basics.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_hashtable.h"
#include "pub_tool_replacemalloc.h"
#include "pub_tool_machine.h"
#include "pub_tool_options.h"
#include "pub_tool_xarray.h"
#include "pub_tool_stacktrace.h"
#include "pub_tool_debuginfo.h"  /* VG_(get_data_description) */
#include "pub_tool_wordfm.h"
#include "pub_tool_oset.h"
#include "pub_tool_seqmatch.h"   /* VG_(string_match) */
#include "pub_tool_aspacemgr.h"  /* VG_(am_is_valid_for_client) */
#include "pub_tool_vki.h"        /* VKI_PROT_READ */
#include "../coregrind/pub_core_translate.h"  /* VG_(translate_ahead) */
#include "../coregrind/pub_core_stacks.h"     /* VG_(is_stack) */

#include "libvex_guest_offsets.h"

#include "hg_basics.h"
#include "hg_wordset.h"
#include "hg_lock_n_thread.h"
#include "hg_lsd.h"
#include "hg_errors.h"

#include "libhb.h"

#include "hg_logging.h"

static ThrLSD* (*main_get_current_ThrLSD)( void ) = NULL;
static void    (*main_record_error_Misc)(ThrLSD*, char*) = NULL;
static void    (*main_annotate_wait) (void*, void*) = NULL;
static void    (*main_annotate_signal) (void*) = NULL;

UInt HG_(clo_lost_signal_detector) = 0;

Bool hg_lsd__clo_verbose_lost_signal_catcher = False;
Bool hg_lsd__clo_verbose_lost_instr_details  = False;
Bool hg_lsd__clo_verbose_write_read_relation = False;

static const UInt simstack_size = 100;

/*----------------------------------------------------------------*/
/*--- Duplicated functions                                     ---*/
/*----------------------------------------------------------------*/

static void space ( Int n )
{
   Int  i;
   Char spaces[128+1];
   tl_assert(n >= 0 && n < 128);
   if (n == 0)
      return;
   for (i = 0; i < n; i++)
      spaces[i] = ' ';
   spaces[i] = 0;
   tl_assert(i < 128+1);
   VG_(printf)("%s", spaces);
}

/*----------------------------------------------------------------*/
/*--- Lost signal catcher                                      ---*/
/*----------------------------------------------------------------*/

/* ------ Statistics ------ */

static UWord stats__instrumented_jumps   = 0;
static UWord stats__condition_signaled   = 0;
static UWord stats__signals_received     = 0;
static UWord stats__signals_lost         = 0;
static UWord stats__signals_catched      = 0;
static UWord stats__signals_direct       = 0;
static UWord stats__barriers             = 0;
static UWord stats__pthread_mutex_lock   = 0;
static UWord stats__pthread_mutex_unlock = 0;
static UWord stats__pthread_cond_waits   = 0;
static UWord stats__pthread_cond_signals = 0;
static UWord stats__pthread_barriers     = 0;

/* ------ Data structures and helper functions ------ */

typedef struct _AccessList AccessList;
typedef struct _CVList CVList;

struct _ThrLSD {
   LockLSD*    current_lock;
   AccessList* writes;
   AccessList* reads;
   Bool        record_writes;
   Bool        record_reads;

   Bool        waiting;
   
   UChar*      simstack;
   Addr        simstack_base;

   Bool        in_waiting_loop;
   Addr        expect_cond;
   Addr        expect_mutex;
   CVList      *cvlist;
   
   /* opaque (to us) data we hold on behalf of the library's user. */
   void* opaque;
};

/* Stores information about a condition variable as used in the
   pthreads library */
typedef
   struct _ConditionVariable {
      /* ADMIN */
      struct _ConditionVariable* admin;
      /* EXPOSITION */
      /* pthread's condition variable */
      Addr cond;
      /* FLAGS */
      Bool signaled;  /* Pending signal */
      /* last sent signals */
      AccessList** recorded_signals;
      UInt         recorded_signals_size;
   }
   CondVar;

/* This is used to record accessed variables in an lock-enclosed
   block. This way you can estimate W/R-Relations */
struct _LockLSD {
   struct _LockLSD *parent;

   /* additional info */
   Addr  mutex;
};

/* This is used to record accessed variables in an lock-enclosed
   block. This way you can estimate W/R-Relations */
#define ACCESSLIST_SIZE 100
struct _AccessList {
   Addr addrs[ACCESSLIST_SIZE];
   UInt size;
   UInt next;
   Bool iterating;
   SO*  so;
};

struct _CVList {
   void*    cond;
   void*    mutex;
   struct _CVList *next;
};

/* Table to store special instruction addresses:
 *   Maps base address of loop body -> address of conditional jump
 *   Maps end address of whole loop -> LOOP_END_ADDRESS (0) */
static WordFM* map_loop_addrs = NULL;
#define LOOP_END_ADDRESS 0

/* Admin linked list of ConditionVariable */
static CondVar* admin_condvars = NULL;

/* pthread_cond_t* -> CondVar* */
static WordFM* map_cond_to_CondVar = NULL;

/* ------ Constructors and helper functions ------ */

static CondVar* mk_CondVar( Addr cond, LockLSD* lock ) {
   CondVar* cv = HG_(zalloc)( "mk_CondVar.1", sizeof(CondVar) );
   cv->cond     = cond;
   cv->signaled = False;
   cv->admin    = admin_condvars;
   cv->recorded_signals_size = 8;
   cv->recorded_signals = HG_(zalloc)( "hg.lsd.mk_CondVar.1", sizeof(AccessList*)*cv->recorded_signals_size );
   admin_condvars = cv;
   tl_assert(cv->recorded_signals);
   return cv;
}

static
CondVar* lookup_CondVar( Addr cond ) {
   CondVar* cv;
   if ( VG_(lookupFM)( map_cond_to_CondVar, NULL, (Word*)&cv, (Word)cond ) == False ) {
      cv = mk_CondVar( cond, (Addr)0 );
      VG_(addToFM)( map_cond_to_CondVar, (Word)cond, (Word)cv );
   }
   return cv;
}

static void pp_AccessList ( Int d, AccessList *al )
{
   Int      i;
   space(d); VG_(printf)("AccessList (%d entries) SO:%p {\n",
                         al->size, (void*)al->so);
   for( i = 0; i < al->size; ) {
      space(d+3);
      VG_(printf)("0x%08lx   ", al->addrs[i] );
      i++; if( i >= al->size ) break;
      VG_(printf)("0x%08lx   ", al->addrs[i] );
      i++; if( i >= al->size ) break;
      VG_(printf)("0x%08lx   ", al->addrs[i] );
      i++; if( i >= al->size ) break;
      VG_(printf)("0x%08lx   ", al->addrs[i] );
      i++; if( i >= al->size ) break;

      VG_(printf)("0x%08lx   ", al->addrs[i] );
      i++; if( i >= al->size ) break;
      VG_(printf)("0x%08lx   ", al->addrs[i] );
      i++; if( i >= al->size ) break;
      VG_(printf)("0x%08lx   ", al->addrs[i] );
      i++; if( i >= al->size ) break;
      VG_(printf)("0x%08lx\n", al->addrs[i] );
      i++; if( i >= al->size ) break;      
   }
   space(d); VG_(printf)("}\n");
}

static void pp_CondVar ( Int d, CondVar* cv )
{
   Int i;
   space(d+0); VG_(printf)("CondVar %p {\n", cv);
   /*space(d+3); VG_(printf)("admin    %p\n",   cv->admin); */
   space(d+3); VG_(printf)("cond     %p\n",   (void*)cv->cond);
   space(d+3); VG_(printf)("signaled   %s\n",   cv->signaled?"yes":" no");
   for( i = 0; i < cv->recorded_signals_size; i++ ) {
      if( cv->recorded_signals[i] )
         pp_AccessList( d+3, cv->recorded_signals[i] );
   }
   space(d+0); VG_(printf)("}\n");
}

static void pp_admin_condvars ( Int d )
{
   Int     i, n;
   CondVar* cv;
   for (n = 0, cv = admin_condvars;  cv;  n++, cv = cv->admin) {
      /* nothing */
   }
   space(d); VG_(printf)("admin_condvars (%d records) {\n", n);
   for (i = 0, cv = admin_condvars;  cv;  i++, cv = cv->admin) {
      if (0) {
         space(n);
         VG_(printf)("admin_condvars record %d of %d:\n", i, n);
      }
      pp_CondVar(d+3, cv);
   }
   space(d); VG_(printf)("}\n");
}

static void pp_map_loop_addrs ( Int d )
{
   Addr ip;
   Addr mark;
   space(d); VG_(printf)("map_loop_addrs (%d entries) {\n",
                         (Int)VG_(sizeFM)( map_loop_addrs ));
   VG_(initIterFM)( map_loop_addrs );
   while (VG_(nextIterFM)( map_loop_addrs, (Word*)&ip,
                                           (Word*)&mark )) {
      space(d+3);
      if( mark == 0 )
         VG_(printf)("0x%lx -> END;\n\n", ip);
      else
         VG_(printf)("0x%lx -> BEGIN; 0x%lx\n", ip, mark);
   }
   VG_(doneIterFM)( map_loop_addrs );
   space(d); VG_(printf)("}\n");
}

/*----------------------------------------------------------------*/
/*--- AccessList Stuff                                         ---*/
/*----------------------------------------------------------------*/

static
void alist_clear( AccessList *al ) {
   al->size = 0;
   al->iterating = False;
}

static inline
void alist_add( AccessList *al, Addr addr ) {
   if ( al->size < ACCESSLIST_SIZE ) {
      al->addrs[al->size] = addr;
      al->size++;
   }
}

static
Bool alist_iterate_begin( AccessList *al ) {
   if( al->iterating )
      return False;

   al->iterating = True;
   al->next = 0;

   return True;
}

static
Bool alist_iterate_next( AccessList *al, Addr *addr ) {
   tl_assert( al->iterating );

   if ( al->next >= al->size )
      return False;
   *addr = al->addrs[al->next];
   al->next++;

   return True;
}

static
void alist_iterate_end( AccessList *al ) {
   tl_assert( al->iterating );
   al->iterating = False;
}

/*----------------------------------------------------------------*/
/*--- Helper Functions                                         ---*/
/*----------------------------------------------------------------*/

// Extract immediate value from IR 
static
Addr64 value_of ( IRExpr* e ) {
   switch( e->tag ) {
      case Iex_Const: {
         IRConst* con = e->Iex.Const.con;
         switch ( con->tag ) {
            case Ico_V128:
               return con->Ico.V128;
            case Ico_U1:
               return con->Ico.U1;
            case Ico_U8:
               return con->Ico.U8;
            case Ico_U16:
               return con->Ico.U16;
            case Ico_U32:
               return con->Ico.U32;
            case Ico_U64:
            default:
               return con->Ico.U64;
         }
      } break;
      case Iex_Load: {
         return value_of( e->Iex.Load.addr );
      } break;
      default:
         return 0;
   }
}

/*----------------------------------------------------------------*/
/*--- Loop instrumentation                                     ---*/
/*----------------------------------------------------------------*/

static IRSB*   real_bbOut = NULL;

/* -------- stack simulation -------- */

static VG_REGPARM(1)
void simstack_init( Addr stack_pointer ) {
   ThrLSD* thr = main_get_current_ThrLSD();
   thr->simstack_base = stack_pointer-(simstack_size/2);
}

static VG_REGPARM(2)
void simstack_put_4( Addr sp, UInt w32 ) {
   ThrLSD* thr = main_get_current_ThrLSD();
   Addr    rel = sp - thr->simstack_base;
   if( rel + 4 < simstack_size )
      *((UInt*)(thr->simstack+rel)) = w32;
}

static VG_REGPARM(2)
void simstack_put_8( Addr sp, ULong w64 ) {
   ThrLSD* thr = main_get_current_ThrLSD();
   Addr    rel = sp - thr->simstack_base;
   if( rel + 8 < simstack_size )
      *((ULong*)(thr->simstack+rel)) = w64;
}


static VG_REGPARM(1)
UInt simstack_get_4( Addr sp ) {
   ThrLSD* thr = main_get_current_ThrLSD();
   Addr    rel = sp - thr->simstack_base;
   if( rel + 4 < simstack_size )
      return *((UInt*)(thr->simstack+rel));
   else
      return 0;
}

static VG_REGPARM(1)
ULong simstack_get_8( Addr sp ) {
   ThrLSD* thr = main_get_current_ThrLSD();
   Addr    rel = sp - thr->simstack_base;
   if( rel + 8 < simstack_size )
      return *((ULong*)(thr->simstack+rel));
   else
      return 0;
}

static
void simstack_dump( Addr sp ) {
   ThrLSD* thr = main_get_current_ThrLSD();
   UInt i;
   
   VG_(printf)("\nStack:\n");
   i = 0;
   while ( (i + thr->simstack_base)%4 != 0 ) // align
      i++;
   while ( i < simstack_size ) {
      VG_(printf)( "%s0x%lx: %08x\n", (sp == i + thr->simstack_base)?">":" ",
                                       i + thr->simstack_base,
                                       *((UInt*)&thr->simstack[i]) );
      i += 4;
   }
   VG_(printf)("------------\n");
}

/* -------- temp map -------- */

struct _MapTemp {
   UInt* map;
   UInt size;
};

static struct _MapTemp map_temps = {NULL, 0};
static struct _MapTemp map_registers = {NULL, 0};

static
IRTemp maptemp_lookup( struct _MapTemp* m, IRTemp virt ) {
   if( virt >= m->size )
      return IRTemp_INVALID;
   else
      return m->map[virt];
}

static
IRTemp maptemp_add( struct _MapTemp* m, IRTemp virt, IRTemp real ) {
   IRTemp old;
   
   if( virt >= m->size ) {
      // enlarge mapping
      UInt  new_size = 16;
      UInt  i;
      UInt* new_map = NULL;
      
      while( new_size <= virt )
         new_size <<= 1;
      
      new_map = HG_(zalloc)( "hg.lsd.maptemp.add.1", sizeof(UInt)*new_size );
      tl_assert( new_map );
      
      for( i = 0; i < m->size; i++ ) {
         new_map[i] = m->map[i];
      }
      
      for( i = m->size; i < new_size; i++ ) {
         new_map[i] = IRTemp_INVALID;
      }
      
      if( m->map )
         HG_(free)(m->map);
      m->map = new_map;
      m->size = new_size;
      
      old = IRTemp_INVALID;
   } else {
      old = m->map[virt];
   }

   m->map[virt] = real;
   return old;
}

static
void maptemp_clear( struct _MapTemp* m ) {
   if( m->size >= 256 ) { // threshold
      tl_assert( m->map );
      HG_(free)( m->map );
      m->map = NULL;
      m->size = 0;
   } else {
      UInt i;
      for( i = 0; i < m->size; i++ ) {
         m->map[i] = IRTemp_INVALID;
      }
   }
}

/* -------- safe load ops -------- */

static
Bool addr_is_valid( Addr a, SizeT len ) {
   return VG_(am_is_valid_for_client)( a, len, VKI_PROT_READ );
}

static VG_REGPARM(1)
UChar safe_load_1( Addr a ) {
   if( addr_is_valid(a, 1) ) {
      if( hg_lsd__clo_verbose_lost_instr_details )
         VG_(printf)( "loading %p:I8 = 0x%lx\n", (void*)a, (long)(*(UChar*)a) );
      return *(UChar*)a;
   }
   else {
      if( hg_lsd__clo_verbose_lost_instr_details )
         VG_(printf)( "loading %p:I8 = ?\n", (void*)a );
      return 0;
   }
}

static VG_REGPARM(1)
UShort safe_load_2( Addr a ) {
   if( addr_is_valid(a, 2) ) {
      if( hg_lsd__clo_verbose_lost_instr_details )
         VG_(printf)( "loading %p:I16 = 0x%lx\n", (void*)a, (long)(*(UShort*)a) );
      return *(UShort*)a;
   }
   else {
      if( hg_lsd__clo_verbose_lost_instr_details )
         VG_(printf)( "loading %p:I16 = ?\n", (void*)a );
      return 0;
   }
}

static VG_REGPARM(1)
UInt safe_load_4( Addr a ) {
   if( addr_is_valid(a, 4) ) {
      if( hg_lsd__clo_verbose_lost_instr_details )
         VG_(printf)( "loading %p:I32 = 0x%lx\n", (void*)a, (long)(*(UInt*)a) );
      return *(UInt*)a;
   }
   else {
      if( hg_lsd__clo_verbose_lost_instr_details )
         VG_(printf)( "loading %p:I32 = ?\n", (void*)a );
      return 0;
   }
}

static VG_REGPARM(1)
ULong safe_load_8( Addr a ) {
   if( addr_is_valid(a, 8) ) {
      if( hg_lsd__clo_verbose_lost_instr_details )
         VG_(printf)( "loading %p:I64 = 0x%llx\n", (void*)a, *(ULong*)a );
      return *(ULong*)a;
   }
   else {
      if( hg_lsd__clo_verbose_lost_instr_details )
         VG_(printf)( "loading %p:I32 = ?\n", (void*)a );
      return 0;
   }
}

static
void instrument_safe_load( IRTemp tmp, IRType ty, IRExpr* addr ) {
   IRExpr** argv     = NULL;
   IRDirty* di       = NULL;
   
   argv = mkIRExprVec_1( addr );
   
   switch( ty ) {
   case Ity_I8:
      di = unsafeIRDirty_1_N( tmp, 1,
                              "safe_load_1",
                              VG_(fnptr_to_fnentry)( &safe_load_1 ),
                              argv );
      break;
   case Ity_I16:
      di = unsafeIRDirty_1_N( tmp, 1,
                              "safe_load_2",
                              VG_(fnptr_to_fnentry)( &safe_load_2 ),
                              argv );
      break;
   case Ity_I32:
      di = unsafeIRDirty_1_N( tmp, 1,
                              "safe_load_4",
                              VG_(fnptr_to_fnentry)( &safe_load_4 ),
                              argv );
      break;
   case Ity_I64:
      di = unsafeIRDirty_1_N( tmp, 1,
                              "safe_load_8",
                              VG_(fnptr_to_fnentry)( &safe_load_8 ),
                              argv );
      break;
   default:
      /* thats odd ?! */
      break;
   }
   
   addStmtToIRSB( real_bbOut, IRStmt_Dirty(di) );
}

static
IRExpr* virtualize_expr ( IRExpr* e ) {
   switch( e->tag ) {
   case Iex_Const:
      return e;

   case Iex_Load:
   	/* load operations are handled safely */
      tl_assert(False);
      return NULL;
   
   case Iex_RdTmp: {
      IRTemp real = maptemp_lookup( &map_temps, e->Iex.RdTmp.tmp );
      if( real == IRTemp_INVALID )
         return NULL;
      else
         return IRExpr_RdTmp( real );
   }
   
   case Iex_Get: {
#if defined(VGP_x86_linux)
      if( e->Iex.Get.offset == OFFSET_x86_ESP ) {
         if( e->Iex.Get.ty == Ity_I32 ) {
            IRTemp real = maptemp_lookup( &map_registers, OFFSET_x86_ESP );
            return IRExpr_RdTmp( real );
         }
      }
#endif
      return e;
   }

   case Iex_Unop: {
      IRExpr *arg = virtualize_expr( e->Iex.Unop.arg );
      if( arg == NULL )
         return NULL;
      else
         return IRExpr_Unop( e->Iex.Unop.op, arg );
   }
   
   case Iex_Binop: {
      IRExpr *arg1 = virtualize_expr( e->Iex.Binop.arg1 );
      IRExpr *arg2 = virtualize_expr( e->Iex.Binop.arg2 );
      if( arg1 == NULL || arg2 == NULL )
         return NULL;
      else
         return IRExpr_Binop( e->Iex.Binop.op, arg1, arg2 );
   }
   
   case Iex_Triop: {
      IRExpr *arg1 = virtualize_expr( e->Iex.Triop.arg1 );
      IRExpr *arg2 = virtualize_expr( e->Iex.Triop.arg2 );
      IRExpr *arg3 = virtualize_expr( e->Iex.Triop.arg3 );
      if( arg1 == NULL || arg2 == NULL || arg3 == NULL )
         return NULL;
      else
         return IRExpr_Triop( e->Iex.Triop.op, arg1, arg2, arg3 );
   }
   
   case Iex_Qop: {
      IRExpr *arg1 = virtualize_expr( e->Iex.Qop.arg1 );
      IRExpr *arg2 = virtualize_expr( e->Iex.Qop.arg2 );
      IRExpr *arg3 = virtualize_expr( e->Iex.Qop.arg3 );
      IRExpr *arg4 = virtualize_expr( e->Iex.Qop.arg4 );
      if( arg1 == NULL || arg2 == NULL || arg3 == NULL || arg4 == NULL )
         return NULL;
      else
         return IRExpr_Qop( e->Iex.Qop.op, arg1, arg2, arg3, arg4 );
   }
   
   case Iex_Mux0X: {
      IRExpr *cond = virtualize_expr( e->Iex.Mux0X.cond );
      IRExpr *expr0 = virtualize_expr( e->Iex.Mux0X.expr0 );
      IRExpr *exprX = virtualize_expr( e->Iex.Mux0X.exprX );
      if( cond == NULL || expr0 == NULL || exprX == NULL )
         return NULL;
      else
         return IRExpr_Mux0X( cond, expr0, exprX );
   }
   
   default:
      return NULL;
   }
}

static
IRSB* look_ahead_parameters ( VgCallbackClosure* closure,
                              IRSB* bbIn,
                              VexGuestLayout* layout,
                              VexGuestExtents* vge,
                              IRType gWordTy, IRType hWordTy )
{
   Int     i;
   IRSB*   bbOut;
   IRTypeEnv* real_tyenv = real_bbOut->tyenv;
   IRStmt*    real_stmt;

   /* Output of this func is not stored anywhere, so we try to create a
      minimal but consistent basic block. */
   bbOut           = emptyIRSB();
   bbOut->tyenv    = emptyIRTypeEnv();
   bbOut->next     = mkIRExpr_HWord(0);
   bbOut->jumpkind = Ijk_Boring;

   // Debug
   if( hg_lsd__clo_verbose_lost_instr_details ) {
      VG_(printf)(">>> Simulating this ");
      ppIRSB(bbIn); VG_(printf)("\n");
   }

   // Ignore any IR preamble preceding the first IMark
   i = 0;
   while (i < bbIn->stmts_used && bbIn->stmts[i]->tag != Ist_IMark) {
      //addStmtToIRSB( bbOut, bbIn->stmts[i] );
      i++;
   }

   /* Do just as much as needed to determine whether there is a call to
      pthread_cond_wait and perhaps some parameters */
   for (/*use current i*/; i < bbIn->stmts_used; i++) {
      IRStmt* st = bbIn->stmts[i];
      tl_assert(st);
      tl_assert(isFlatIRStmt(st));
      switch (st->tag) {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_PutI:
         case Ist_MBE:
         case Ist_Exit:
         case Ist_IMark:
         case Ist_Dirty:
            /* None of these can contain any stack references. */
            break;

         case Ist_Put: {
            Int     offset = st->Ist.Put.offset;
            IRExpr* data = virtualize_expr(st->Ist.Put.data);

            if( data == NULL )
               break;

#if defined(VGP_x86_linux)
            // sim stack pointer through temp
            if( offset == OFFSET_x86_ESP ) {
               IRTemp temp = newIRTemp( real_tyenv, Ity_I32 );
               real_stmt = IRStmt_WrTmp( temp , data );
               addStmtToIRSB( real_bbOut, real_stmt );
               maptemp_add( &map_registers, offset, temp );
               break;
            }
#endif
#if defined(VGP_amd64_linux)
            // don't touch the stack pointer 
            if( offset == OFFSET_amd64_RSP )
               break;
#endif

            // save registercontent 
            if( maptemp_lookup( &map_registers, offset ) == IRTemp_INVALID )
            {
               IRTemp temp = newIRTemp( real_tyenv, gWordTy );
               real_stmt = IRStmt_WrTmp( temp, IRExpr_Get( offset, gWordTy ) );
               addStmtToIRSB( real_bbOut, real_stmt );
               maptemp_add( &map_registers, offset, temp );
            }
               
            real_stmt = IRStmt_Put( offset, data );
            addStmtToIRSB( real_bbOut, real_stmt );
            
            break;
         }

         case Ist_WrTmp: {
            IRExpr* data = st->Ist.WrTmp.data;
            IRTemp  res_temp;
            IRType  res_type = typeOfIRTemp(bbIn->tyenv,st->Ist.WrTmp.tmp);

            res_temp = newIRTemp( real_tyenv, res_type );
            tl_assert( maptemp_add( &map_temps, st->Ist.WrTmp.tmp, res_temp ) == IRTemp_INVALID );

            if( data->tag == Iex_Load ) {
               IRExpr* addr = virtualize_expr(data->Iex.Load.addr);
               if( addr != NULL ) {
                  tl_assert( data->Iex.Load.ty == res_type );
                  instrument_safe_load( res_temp, res_type, addr );
               }
            } else {
               data = virtualize_expr( data );
            
               if( data ) {
                  real_stmt = IRStmt_WrTmp( res_temp, data );
                  addStmtToIRSB( real_bbOut, real_stmt );
               }
	    }
            break;
         }

         case Ist_Store: {
#if defined(VGP_x86_linux)
            IRExpr* addr = st->Ist.Store.addr;
            IRExpr* data = st->Ist.Store.data;
            Int     size = sizeofIRType(typeOfIRExpr( bbIn->tyenv, data ));
            IRExpr** argv     = NULL;
            IRDirty* di       = NULL;
            switch( size ) {
            case 4:
               argv = mkIRExprVec_2( virtualize_expr(addr),
                                     virtualize_expr(data) );
               di = unsafeIRDirty_0_N( 2,
                                       "simstack_put_4",
                                       VG_(fnptr_to_fnentry)( &simstack_put_4 ),
                                       argv );
               addStmtToIRSB( real_bbOut, IRStmt_Dirty(di) );
               break;
            case 8:
               argv = mkIRExprVec_2( virtualize_expr(addr),
                                     virtualize_expr(data) );
               di = unsafeIRDirty_0_N( 2,
                                       "simstack_put_8",
                                       VG_(fnptr_to_fnentry)( &simstack_put_8 ),
                                       argv );
               addStmtToIRSB( real_bbOut, IRStmt_Dirty(di) );
               break;
            default:
               break;
            }
#endif
            break;
         }

         default:
            tl_assert(0);

      } /* switch (st->tag) */
      //addStmtToIRSB( bbOut, st );
   } /* iterate over bbIn->stmts */

   return bbOut;
}

static
void instrument_parameters ( IRSB*   bbOut,
                             Addr    blk_base,
                             Addr    blk_last,
                             IRExpr **evh_arg1,
                             IRExpr **evh_arg2 )
{
   Addr64 pos  = blk_base;
   Addr64 ret  = 0;
   UInt i;
   IRTypeEnv* tyenv = bbOut->tyenv;
   IRType hword_type;
   IRStmt* st;
   IRTemp t_saved_sp;
   IRTemp temp;

   tl_assert(sizeof(void*) == sizeof(HWord));
   
#if defined(VGP_x86_linux)
   tl_assert(sizeof(HWord) == 4);
   hword_type = Ity_I32;
#elif defined(VGP_amd64_linux)
   tl_assert(sizeof(HWord) == 8);
   hword_type = Ity_I64;
#else
#error sorry: unsupported platform!
#endif

   // virtual temp mapping
   maptemp_clear( &map_registers );
   
#if defined(VGP_x86_linux)
   // save stack pointer
   t_saved_sp = newIRTemp( tyenv, hword_type );
   st = IRStmt_WrTmp( t_saved_sp, IRExpr_Get(OFFSET_x86_ESP, Ity_I32) );
   addStmtToIRSB( bbOut, st );
   maptemp_add( &map_registers, OFFSET_x86_ESP, t_saved_sp );

   // initiate simulated stack
   {
      IRExpr** argv     = NULL;
      IRDirty* di       = NULL;
      
      argv = mkIRExprVec_1(IRExpr_RdTmp(t_saved_sp));
      di = unsafeIRDirty_0_N( 1,
                              "simstack_init",
                              VG_(fnptr_to_fnentry)( &simstack_init ),
                              argv );

      addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
   }
#endif

   tl_assert( real_bbOut == NULL );
   real_bbOut = bbOut;

   while ( pos <= blk_last ) {
      maptemp_clear( &map_temps );
      ret = VG_(translate_ahead)( pos, look_ahead_parameters );
      if( ret == 0 ) {
         // Ignore errors during ahead translation and go on normally
         break;
      }

      pos += ret;
   }

#if defined(VGP_x86_linux)
   // get simulated stack pointer
   temp = maptemp_lookup( &map_registers, OFFSET_x86_ESP );
   // keep simulated stack pointer from being writen back
   maptemp_add( &map_registers, OFFSET_x86_ESP, IRTemp_INVALID );
   
   // pass over to event-handler
   *evh_arg1 = IRExpr_RdTmp( temp );
   *evh_arg2 = IRExpr_Const( IRConst_U32(0) );
#elif defined(VGP_amd64_linux)
   temp = newIRTemp( tyenv, hword_type );
   st   = IRStmt_WrTmp( temp, IRExpr_Get(OFFSET_amd64_RDI, hword_type) );
   addStmtToIRSB( bbOut, st );
   *evh_arg1 = IRExpr_RdTmp( temp );
   
   temp = newIRTemp( tyenv, hword_type );
   st   = IRStmt_WrTmp( temp, IRExpr_Get(OFFSET_amd64_RSI, hword_type) );
   addStmtToIRSB( bbOut, st );
   *evh_arg2 = IRExpr_RdTmp( temp );
#endif
   
   // restore saved registers
   for( i = 0; i < map_registers.size; i++ ) {
      if( map_registers.map[i] != IRTemp_INVALID ) {
         st = IRStmt_Put( i, IRExpr_RdTmp( map_registers.map[i] ) );
         addStmtToIRSB( bbOut, st );
      }
   }
   
   real_bbOut = NULL;
}

/*----------------------------------------------------------------*/
/*--- Event Handler                                            ---*/
/*----------------------------------------------------------------*/

static VG_REGPARM(2)
void evh__waiting_loop(Word arg1, Word arg2) {
   struct _ThrLSD *thr = main_get_current_ThrLSD();
   Word cond = arg1;
   Word mutex = arg2;

#if defined(VGP_x86_linux)
   Addr sp = arg1;
   
   if( hg_lsd__clo_verbose_lost_instr_details )
      simstack_dump(sp);
   
   if( sizeof(Word) == 4 ) {
      sp += 4; cond  = simstack_get_4(sp);
      sp += 4; mutex = simstack_get_4(sp);
   } else
   if( sizeof(Word) == 8 ) {
      sp += 8; cond  = simstack_get_8(sp);
      sp += 8; mutex = simstack_get_8(sp);
   } else
      VG_(tool_panic)("unexpected word length");
#endif

   tl_assert( sizeof(void*) == sizeof(Word) );
   
   thr->expect_cond = cond;
   thr->expect_mutex = mutex;
}

static VG_REGPARM(2)
void evh__signal(Word arg1) {
   Word cond = arg1;

#if defined(VGP_x86_linux)
   Addr sp = arg1;
   
   if( hg_lsd__clo_verbose_lost_instr_details )
      simstack_dump(sp);
   
   if( sizeof(Word) == 4 ) {
      sp += 4; cond  = simstack_get_4(sp);
   } else
   if( sizeof(Word) == 8 ) {
      sp += 8; cond  = simstack_get_8(sp);
   } else
      VG_(tool_panic)("unexpected word length");
#endif

   tl_assert( sizeof(void*) == sizeof(Word) );

   if( hg_lsd__clo_verbose_lost_signal_catcher )
      VG_(printf)("####### annotate_signal 0x%lx \n\n", cond);

   main_annotate_signal( (void*)cond );
}

static VG_REGPARM(0)
void evh__waiting_loop_begin(void) {
   struct _ThrLSD *thr = main_get_current_ThrLSD();
   
   thr->in_waiting_loop = True;
   thr->record_reads = False;
}

static VG_REGPARM(0)
void evh__waiting_loop_end(void) {
   struct _ThrLSD *thr = main_get_current_ThrLSD();
   CondVar *cv;
   CVList  *cvlist_iter;
   CVList  *cvlist_prev;
   Bool     expected_params_found = False;

   thr->in_waiting_loop = False;

   if( hg_lsd__clo_verbose_lost_signal_catcher )
      VG_(printf)("####### annotate_wait { \n");

   cvlist_iter = thr->cvlist;
   while( cvlist_iter ) {
      void* cond  = cvlist_iter->cond;
      void* mutex = cvlist_iter->mutex;
      
      if( (Word)cond == thr->expect_cond ) {
         tl_assert( (Word)mutex == thr->expect_mutex );
         expected_params_found = True;
      }

      if( hg_lsd__clo_verbose_lost_signal_catcher )
         VG_(printf)("#######          %s %p %p ", expected_params_found?">":" ", cond, mutex);
      
      if ( VG_(lookupFM)( map_cond_to_CondVar, NULL, (Word*)&cv, (Word)cond ) ) {
         main_annotate_wait( cond, mutex );
      }
      else if (hg_lsd__clo_verbose_lost_signal_catcher) {
         VG_(printf)("FAILED!!! \n");
         VG_(get_and_pp_StackTrace)( ((Thread*)thr->opaque)->coretid, 8 );
      }

      if( hg_lsd__clo_verbose_lost_signal_catcher )
         VG_(printf)("\n");
      
      cvlist_prev = cvlist_iter; 
      cvlist_iter = cvlist_iter->next;
      HG_(free)(cvlist_prev);
   }
   
   if( !expected_params_found ) {
      void *cond  = (void*)thr->expect_cond;
      void *mutex = (void*)thr->expect_mutex;

      if( hg_lsd__clo_verbose_lost_signal_catcher )
         VG_(printf)("#######      lost: %p %p \n", cond, mutex);
      
      if ( VG_(lookupFM)( map_cond_to_CondVar, NULL, (Word*)&cv, (Word)cond ) ) {
         main_annotate_wait( cond, mutex );
      }
      else
         if( hg_lsd__clo_verbose_lost_signal_catcher ) {
            VG_(printf)("FAILED \n");
            VG_(get_and_pp_StackTrace)( ((Thread*)thr->opaque)->coretid, 8 );
      }
   }

   thr->cvlist = NULL;
   
   if( hg_lsd__clo_verbose_lost_signal_catcher )
      VG_(printf)("####### }\n\n");
}

static
void instrument_jump( IRSB* bbOut,
                      Addr iaddr,
                      Addr blk_base,
                      Addr blk_last,
                      IRExpr* guard,
                      IRType guard_type,
                      Bool is_signal ) {
   IRExpr** argv     = NULL;
   IRDirty* di       = NULL;

   IRExpr   *evh_arg1, *evh_arg2;
   
   tl_assert( guard_type == Ity_I1 );

#if 0
   IRTemp   temp;
   IRExpr*  ex_invert;
   IRStmt*  st_invert;

   /* new temp <-- inverted jumping condition */
   if ( invert_guard ) {
      temp = newIRTemp( bbOut->tyenv, Ity_I1 );
      ex_invert = IRExpr_Unop( Iop_Not1, guard );
      st_invert = IRStmt_WrTmp( temp, ex_invert );
      addStmtToIRSB( bbOut, st_invert );
   }
#endif

   // get params of pthread_cond_wait
   instrument_parameters( bbOut, blk_base, blk_last, &evh_arg1, &evh_arg2 );

   // instrumentation
   if( is_signal ) {
      argv = mkIRExprVec_1(evh_arg1);
      di = unsafeIRDirty_0_N( 1,
                              "evh__signal",
                              VG_(fnptr_to_fnentry)( &evh__signal ),
                              argv );
   } else {
      argv = mkIRExprVec_2(evh_arg1, evh_arg2);
      di = unsafeIRDirty_0_N( 2,
                              "evh__waiting_loop",
                              VG_(fnptr_to_fnentry)( &evh__waiting_loop ),
                              argv );
   }

#if 0
   if( invert_guard ) {
      di->guard = IRExpr_RdTmp( temp );
   } else {
      di->guard = guard;
   }
#endif

   addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
}

/*----------------------------------------------------------------*/
/*--- Look ahead into loop body                                ---*/
/*----------------------------------------------------------------*/

static Bool la_found_wait = False;
static Addr la_wait_addr = 0;
static Bool la_found_jump = False;
static Bool la_found_signal = False;

static
IRSB* look_ahead ( VgCallbackClosure* closure,
                   IRSB* bbIn,
                   VexGuestLayout* layout,
                   VexGuestExtents* vge,
                   IRType gWordTy, IRType hWordTy )
{
   Int     i;
   IRSB*   bbOut;

   static Addr   iaddr,
                 ilen;
   
   IRTemp  jumpdest_tmp = IRTemp_INVALID;
   Addr64  jumpdest     = 0;
   static Char    fnname[100];

   // Assert clean static values
   tl_assert(la_found_wait == False);
   tl_assert(la_found_jump == False);
   tl_assert(la_found_signal == False);
   tl_assert(la_wait_addr == 0);

   /* Output of this func is not stored anywhere, so we try to create a
      minimal but consistent basic block. */
   bbOut           = emptyIRSB();
   bbOut->tyenv    = emptyIRTypeEnv();
   bbOut->next     = mkIRExpr_HWord(0x1010);
   bbOut->jumpkind = Ijk_Boring;
   
   switch( bbIn->next->tag ) {
   case Iex_Const:
      jumpdest = value_of(bbIn->next);
      break;
   case Iex_RdTmp:
      jumpdest_tmp = bbIn->next->Iex.RdTmp.tmp;      
      break;
   default:
      tl_assert(0);
   }

   // Ignore any IR preamble preceding the first IMark
   i = 0;
   while (i < bbIn->stmts_used && bbIn->stmts[i]->tag != Ist_IMark) {
      i++;
   }

   /* Do just as much as needed to determine whether there is a call to
      pthread_cond_wait */
   for (/*use current i*/; i < bbIn->stmts_used; i++) {
      IRStmt* st = bbIn->stmts[i];
      tl_assert(st);
      tl_assert(isFlatIRStmt(st));
      switch (st->tag) {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_PutI:
         case Ist_MBE:
         case Ist_Dirty:
         case Ist_Put:
         case Ist_Store:
            /* None of these can contain any stack references. */
            break;

         case Ist_IMark:
            iaddr = st->Ist.IMark.addr;
            ilen  = st->Ist.IMark.len;
            break;

         case Ist_Exit: {
            if ( st->Ist.Exit.jk == Ijk_Boring ) // standard (conditional) jump
               la_found_jump = True;
            break;
         }

         case Ist_WrTmp: {
            IRExpr* data = st->Ist.WrTmp.data;
            /* if an immediate value is loaded into an temp and
               we are watching this temp, watch_tmp_value is set
               to the immediate value */
            if ( st->Ist.WrTmp.tmp == jumpdest_tmp )
               jumpdest = value_of(data);
            break;
         }

         default:
            tl_assert(0);

      } /* switch (st->tag) */
   } /* iterate over bbIn->stmts */
   
   /* see if next block is a jump to pthread_cond_wait */
   if ( jumpdest == 0 )
      return bbOut;

   if ( VG_(get_jumpslot_fnname)(jumpdest, fnname, sizeof(fnname)) ||
        VG_(get_datasym_and_offset)(jumpdest, fnname, sizeof(fnname), NULL) )
   {
      Char *fnpart = NULL;

      if ( VG_(strncmp)(fnname, "g_cond_", VG_(strlen)("g_cond_")) == 0 ) {
         fnpart = fnname + VG_(strlen)("g_cond_");
      } else
      if ( VG_(strncmp)(fnname, "pthread_cond_", VG_(strlen)("pthread_cond_")) == 0 ) {
         fnpart = fnname + VG_(strlen)("pthread_cond_");
      }
      
      if( fnpart ) {
         if ( VG_(strncmp)(fnpart, "wait", VG_(strlen)("wait")) == 0 ||
               VG_(strncmp)(fnpart, "timedwait", VG_(strlen)("timedwait")) == 0 ) {
            la_found_wait = True;
            la_wait_addr = iaddr;
         }
/*
         else
         if ( VG_(strncmp)(fnpart, "signal", VG_(strlen)("signal")) == 0 )
            la_found_signal = True;
         else
         if ( VG_(strncmp)(fnpart, "broadcast", VG_(strlen)("broadcast")) == 0 )
            la_found_signal = True;
            */
      }
   }

   return bbOut;
}

static UWord stats__total_jumps               = 0;
static UWord stats__loop_jumps                = 0;
static UWord stats__loopbodies_reoccurred     = 0;
static UWord stats__loopbodies_reinstrumented = 0;
static UWord stats__instrumented_loops        = 0;
static UWord stats__instrumented_signals      = 0;

/*
 * Detect loops by scanning for unconditional jumps.
 * Extract address and size of loop body for
 *  analysis of loop body. 
 */
static inline
void analyse_and_instrument_jump ( Addr    iaddr,
                                   Addr    ilen,
                                   IRStmt* st,
                                   IRSB*   bbIn,
                                   IRSB*   bbOut,
                                   VgCallbackClosure* closure,
                                   VexGuestExtents* vge,
                                   Bool*   print_IRSB )
{
   Addr dst = 0;
   Addr blk_base;
   Addr blk_len;
   Bool condition_inverted;
   UInt clo_loop_size_min = 12;
   UInt clo_loop_size_max = 2000;

   stats__total_jumps ++;

   // Ignore unconditional jumps and function calls
   if ( st->Ist.Exit.jk != Ijk_Boring || !isIRAtom( st->Ist.Exit.guard ) )
      return;

   // Get IR-jump destination
   if ( st->Ist.Exit.dst->tag == Ico_U32 )
      dst = st->Ist.Exit.dst->Ico.U32;
   else if ( st->Ist.Exit.dst->tag == Ico_U64 )
      dst = st->Ist.Exit.dst->Ico.U64;
   else
      VG_(tool_panic)("unexpected word length");

   // Valgrind may "invert" the jump, i.e. jump destination
   // and address of the next superblock is exchanged.
   condition_inverted = (dst == iaddr + ilen);

   if ( condition_inverted ) {
      if ( bbIn->next->tag == Iex_Const ) {
         if ( bbIn->next->Iex.Const.con->tag == Ico_U32 )
            dst = bbIn->next->Iex.Const.con->Ico.U32;
         else if ( bbIn->next->Iex.Const.con->tag == Ico_U64 )
            dst = bbIn->next->Iex.Const.con->Ico.U64;
         else
            VG_(tool_panic)("unexpected word length");
      } else {
         return;
      }
   }

   // Get dimensions of loop body
   if ( dst < iaddr ) {
      /* jump is headed backwards */
      blk_base = dst;
      blk_len  = iaddr + ilen - blk_base;
   } else if ( dst > iaddr ) {
      /* jump is headed forwards */
      blk_base = iaddr + ilen;
      blk_len  = dst - blk_base;
   } else {
      /* jump to same instr. ?! */
      return;
   }

   // Ignore loop bodies that are too small or too big to be a waiting loop
   if ( blk_len > clo_loop_size_max ||
        blk_len < clo_loop_size_min )
      return;
   
   tl_assert( closure->mode == TM_Normal );
   
   {
      /* Initiate ahead translation */
      Addr64 pos = blk_base;
      Addr64 end = blk_base + blk_len;
      Addr64 ret = 0;
      UInt blocks = 0;
      
      stats__loop_jumps ++;
      
      { // see if we already "know" this loop
        Addr jaddr;
      
        if( VG_(lookupFM)( map_loop_addrs, NULL, (Word*)&jaddr, (Word)blk_base ) ) {
           stats__loopbodies_reoccurred ++;
           if ( jaddr == iaddr ) {
              // do instrumentation again!
              stats__loopbodies_reinstrumented ++;
           } else {
              // we already recognized this loop 
              return;
           }
        }
      }
      
      // look ahead into loop body
      while ( blocks < 10 && pos < end ) {
        la_found_wait = False;
        la_found_jump = False;
        la_found_signal = False;
        la_wait_addr = 0;
      
        ret = VG_(translate_ahead)( pos, look_ahead );
        if( ret == 0 ) {
           // Ignore errors during ahead translation and go on normally
           break;
        }
      
        if( //la_found_jump == True ||
            la_found_signal == True ||
            la_found_wait == True )
           break;
      
        pos += ret;
        blocks ++;
      }

      if( la_found_wait ) {
         if( hg_lsd__clo_verbose_lost_instr_details ) {
           VG_(printf)("####################################\n");
           VG_(printf)("# found waiting loop! \n" );
           VG_(printf)("# * body from  0x%lx to 0x%lx \n", blk_base, blk_base+blk_len);
           VG_(printf)("# * jump in BB    0x%llx to 0x%llx next: 0x%x \n", vge->base[0], vge->base[0]+vge->len[0], bbIn->next->Iex.Const.con->Ico.U32);
           VG_(printf)("#           imark 0x%lx dst 0x%x \n"
                       "# * simul.  from  0x%lx to 0x%lx\n"
                       "####################################\n", iaddr, st->Ist.Exit.dst->Ico.U32, blk_base, la_wait_addr );
         }
         
         // remember loop
         tl_assert( iaddr != LOOP_END_ADDRESS );
         VG_(addToFM)( map_loop_addrs, blk_base, iaddr );
         VG_(addToFM)( map_loop_addrs, end, LOOP_END_ADDRESS );
         
         instrument_jump( bbOut,
                          iaddr,
                          blk_base,
                          pos,
                          st->Ist.Exit.guard,
                          typeOfIRExpr(bbIn->tyenv, st->Ist.Exit.guard),
                          /*!isSignal*/False );
         
         stats__instrumented_loops ++;
         
         // Debugging
         if( hg_lsd__clo_verbose_lost_instr_details )
           *print_IRSB = True;
      }
      else if( la_found_signal ) {
         if( hg_lsd__clo_verbose_lost_instr_details ) {
           VG_(printf)("####################################\n");
           VG_(printf)("# found pthread_cond_signal! \n" );
           VG_(printf)("#    in  0x%lx to 0x%lx \n", blk_base, blk_base+blk_len);
           VG_(printf)("#    BB  0x%llx to 0x%llx next: 0x%x \n", vge->base[0], vge->base[0]+vge->len[0], bbIn->next->Iex.Const.con->Ico.U32);
           VG_(printf)("#  imark 0x%lx dst 0x%x \n"
                       "####################################\n", iaddr, st->Ist.Exit.dst->Ico.U32 );
         }

         // remember loop
         tl_assert( iaddr != LOOP_END_ADDRESS );
         VG_(addToFM)( map_loop_addrs, blk_base, iaddr );
         
         instrument_jump( bbOut,
                          iaddr,
                          blk_base,
                          pos,
                          st->Ist.Exit.guard,
                          typeOfIRExpr(bbIn->tyenv, st->Ist.Exit.guard),
                          /*isSignal*/True );
         
         stats__instrumented_signals ++;
         
         // Debugging
         if( hg_lsd__clo_verbose_lost_instr_details )
           *print_IRSB = True;
      }
   }
}

static
void instrument_loop_begin( IRSB* bbOut ) {
   IRExpr** argv     = NULL;
   IRDirty* di       = NULL;

   argv = mkIRExprVec_0();
   di = unsafeIRDirty_0_N( 0,
                           "evh__waiting_loop_begin",
                           VG_(fnptr_to_fnentry)( &evh__waiting_loop_begin ),
                           argv );

   addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
}

static
void instrument_loop_end( IRSB* bbOut ) {
   IRExpr** argv     = NULL;
   IRDirty* di       = NULL;

   argv = mkIRExprVec_0();
   di = unsafeIRDirty_0_N( 0,
                           "evh__waiting_loop_end",
                           VG_(fnptr_to_fnentry)( &evh__waiting_loop_end ),
                           argv );

   addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
}

/*----------------------------------------------------------------*/
/*--- This is the hook into helgrind's normal                  ---*/
/*--- instrumentation process                                  ---*/
/*----------------------------------------------------------------*/

Bool hg_lsd_instrument_bb ( HG_LSD_INSTR_MODE mode,
                            IRSB* bbIn,
                            IRSB* bbOut,
                            VgCallbackClosure* closure,
                            VexGuestLayout* layout,
                            VexGuestExtents* vge,
                            IRStmt* st )
{
   /* Address and length of the current binary instruction */
   static Addr   iaddr,
                 ilen;

   /* A Temp can be defined here which is tried to be
         evaluated during instrumentation process
      This used by lost signal detection where an indirect
         jump to the jump slot of pthread_cond_wait() is traced */
   static IRTemp  watch_tmp;
   static Addr    watch_tmp_value;
   
   // Debugging output of final superblock
   static Bool    print_IRSB = False;

   if ( LIKELY(mode == HG_IM_Statement) ) {
      switch (st->tag) {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_Put:
         case Ist_PutI:
         case Ist_MBE:
         case Ist_Store:
         case Ist_Dirty:
            /* None of these can contain any memory references. */
            break;

         case Ist_WrTmp: {
            IRExpr* data = st->Ist.WrTmp.data;
            if (data->tag == Iex_Load) {
               IRExpr* addr = data->Iex.Load.addr;

               /* if an immediate value is loaded into an temp and
                  we are watching this temp, watch_tmp_value is set
                  to the immediate value */
               if ( st->Ist.WrTmp.tmp == watch_tmp &&
                    addr->tag == Iex_Const ) {
                  watch_tmp_value = (sizeof(Addr) == 4) ?
                        addr->Iex.Const.con->Ico.U32 :
                        addr->Iex.Const.con->Ico.U64;
               }
            }
            break;
         }

         /* this is used to detect loop-blocks for the lost signal detection */
         case Ist_IMark: {
            // see if we stepped on any special marks
            Addr mark;
         
            iaddr = st->Ist.IMark.addr;
            ilen  = st->Ist.IMark.len;
            
            if( VG_(lookupFM)( map_loop_addrs, NULL, (Word*)&mark, (Word)iaddr ) ) {
               if( mark == LOOP_END_ADDRESS ) {
                  instrument_loop_end( bbOut );
               } else {
                  instrument_loop_begin( bbOut );
               }
            }
            break;
         }

         case Ist_Exit:
            tl_assert(iaddr != 0);
            tl_assert(ilen  != 0);
            if( HG_(clo_lost_signal_detector) >= 1 ) {
               analyse_and_instrument_jump( iaddr, ilen, st, bbIn, bbOut, closure, vge, &print_IRSB );
            }
            break;

         default:
            tl_assert(0);

      } /* switch (st->tag) */
   }
   else if ( UNLIKELY(mode == HG_IM_BBStart) ) {
      iaddr = 0;
      ilen  = 0;
      watch_tmp = IRTemp_INVALID;
      watch_tmp_value = 0;

      // Detect indirect jump
      if( bbIn->next->tag == Iex_RdTmp ) {
         watch_tmp = bbIn->next->Iex.RdTmp.tmp;      
      }
      
#if 0
      // Print some superblocks
      if( vge->base[0] >= 0x8048554 && vge->base[0] <= 0x80485ff ) {
         VG_(printf)(">>> Debug ");
         ppIRSB( bbIn );
         VG_(printf)("\n");
      }
#endif
   }
   else if ( UNLIKELY(mode == HG_IM_BBEnd) ) {
      /* do some stuff if we detect an indirect jump ?! */
      if ( HG_(clo_lost_signal_detector) >= 1 && watch_tmp != IRTemp_INVALID ) {
         
      }
      
      // Debugging
      if( print_IRSB == True ) {
         VG_(printf)(">>> Resulting ");
         ppIRSB( bbOut );
         VG_(printf)("\n");
         print_IRSB = False;
      }
   } else {
      // Should not happen
      tl_assert(0);
   }

   return True;
}

/*----------------------------------------------------------------*/
/*--- Interface                                                ---*/
/*----------------------------------------------------------------*/

inline
LockLSD* hg_lsd__mutex_create (LockKind kind)
{
   LockLSD* lock;
   if( kind == LK_rdwr ) {
      // R/W - Locks aren't used to protect condition variables
      lock = NULL;
   } else { // for LK_mbRec and LK_nonRec
      lock = HG_(zalloc)("hg.lsd.mutex_create.1", sizeof(struct _LockLSD));
      tl_assert(lock != NULL);
   }

   return lock;
}

inline
void hg_lsd__mutex_free(LockLSD* lock)
{
   HG_(free)(lock);
}

void hg_lsd__mutex_lock ( ThrLSD* thr, LockLSD* lock )
{
   thr->current_lock = lock;

   if( HG_(clo_lost_signal_detector) >= 2 ) {
      if( thr->waiting == False ) {
         alist_clear( thr->writes );
         alist_clear( thr->reads );
      }

      thr->record_writes = True;
      thr->record_reads  = True;
   }
   
   if(0) { // pretty print
      VG_(printf)("thr#%d: hg_lsd__mutex_lock\n", ((Thread*)thr->opaque)->coretid);
      VG_(get_and_pp_StackTrace)( ((Thread*)thr->opaque)->coretid, 8 );
   }
}

void hg_lsd__mutex_unlock ( ThrLSD* thr, LockLSD* lock )
{
   thr->current_lock = NULL;
   
   //if( clo_lost_signal_detector >= 2 ) {
      thr->record_writes = False;
      thr->record_reads  = False;
   //}
}

inline
ThrLSD* hg_lsd__create_ThrLSD (void)
{
   ThrLSD* lsdthr = HG_(zalloc)( "hg.lsd.create_ThrLSD.1", sizeof(struct _ThrLSD) );
   tl_assert(lsdthr);

   lsdthr->reads = HG_(zalloc)( "hg.lsd.create_ThrLSD.2", sizeof(AccessList) );
   tl_assert(lsdthr->reads);

   lsdthr->writes = HG_(zalloc)( "hg.lsd.create_ThrLSD.3", sizeof(AccessList) );
   tl_assert(lsdthr->writes);

   lsdthr->record_writes = False;
   lsdthr->record_reads  = False;

   lsdthr->waiting = False;

   lsdthr->simstack = HG_(zalloc)( "hg.lsd.create_ThrLSD.4", sizeof(UChar)*simstack_size );
   tl_assert(lsdthr->simstack);
   
   lsdthr->in_waiting_loop = False;
   lsdthr->expect_cond = 0;
   lsdthr->expect_mutex = 0;
   lsdthr->cvlist = NULL;
   
   return lsdthr;
}

inline
void* hg_lsd__get_ThrLSD_opaque ( ThrLSD* thr )
{
   return thr->opaque;
}

inline
void hg_lsd__set_ThrLSD_opaque ( ThrLSD* thr, void* opaque )
{
   thr->opaque = opaque;
}

inline
void hg_lsd__rec_write ( ThrLSD* thr, Addr a )
{
   if( thr->record_writes ) {
      static UChar buf[100] = "";
      VgSectKind sk;
      
      /* ignore everything that is not on the heap/stack or static */
      sk = VG_(seginfo_sect_kind)( NULL, 0, a );
      if ( sk != Vg_SectData && sk != Vg_SectBSS && sk != Vg_SectUnknown )
         return;

      /* ignore everything that is on the stack */
      if ( VG_(is_stack)(a) )
         return;
      
      /* ignore address space of runtime (ld) */
      VG_(seginfo_sect_kind)( buf, 100, a );
      if ( VG_(string_match)("/lib*/ld-*", buf) )
         return;
      
      /* record the write access */
      alist_add(thr->writes, a);
      
      if( 0) { //a == 0x0401c290 || a == 0x8073DF8 || a == 0x8073DF4 ) {
         VG_(printf)("<<<<<<<<<<<<<< 0x%08lx %s [%s]", a, buf, VG_(pp_SectKind)(sk));
         VG_(printf)("\n");
         VG_(get_and_pp_StackTrace)( ((Thread*)thr->opaque)->coretid, 8 );
      }
   }
}

inline
void hg_lsd__rec_read ( ThrLSD* thr, Addr a )
{
   if( thr->record_reads ) {
      VgSectKind sk;
      sk = VG_(seginfo_sect_kind)( NULL, 0, a );
      if ( sk != Vg_SectData && sk != Vg_SectBSS && sk != Vg_SectUnknown )
         return;
      if ( VG_(is_stack)(a) )
         return;
      alist_add(thr->reads, a);
   }
}

void hg_lsd__cond_signal_pre ( ThrLSD* thr, void* cond, SO* so )
{
   CondVar* cv;
   //CondVar* cvnull;

   AccessList* al;
   UInt        i;

   cv = lookup_CondVar((Addr)cond);
   //cvnull = lookup_CondVar((Addr)0);
   
   cv->signaled = True;
   
   if( hg_lsd__clo_verbose_lost_signal_catcher )
      VG_(printf)("------- cond_signal %p \n\n", cond);

   /* ---------- W / R - relation ---------- */

   if( HG_(clo_lost_signal_detector) < 2 )
      return;

   // forget latest recorded signal
   i = cv->recorded_signals_size - 1;
   al = cv->recorded_signals[i];
   if( al ) {
      libhb_so_dealloc( al->so );
      HG_(free)( al );
   }
   for( i = cv->recorded_signals_size - 1; i >= 1; i-- ) {
      cv->recorded_signals[i] = cv->recorded_signals[i-1]; 
   }
   
   // store access list
   cv->recorded_signals[0] = thr->writes;
   cv->recorded_signals[0]->so = libhb_so_extract( ((Thread*)thr->opaque)->hbthr ); // TODO: FIXME!!!
   
   // new access list for thread
   thr->writes = HG_(zalloc)( "hg.lsd.cond_signal_pre.1", sizeof(AccessList) );
   tl_assert(thr->writes);
   alist_clear( thr->writes );
   
   if(hg_lsd__clo_verbose_write_read_relation) { // pretty print
      VG_(printf)("thr#%d: hg_lsd__cond_signal_pre(%p)\n", ((Thread*)thr->opaque)->coretid, (void*)cond);
      VG_(get_and_pp_StackTrace)( ((Thread*)thr->opaque)->coretid, 8 );
      VG_(printf)("   [SO=%p]\n", cv->recorded_signals[0]->so);
   }
}

void hg_lsd__cond_wait_pre ( ThrLSD* thr, void* cond )
{
   tl_assert( thr->waiting == False );
   thr->waiting = True;
}

inline
Bool hg_lsd__cond_wait_post ( ThrLSD* thr, void* cond, void* mutex )
{
   tl_assert( thr->waiting == True );
   thr->waiting = False;
   
   if( thr->in_waiting_loop ) {
      CVList **cvlist_iter = &thr->cvlist;

      if( hg_lsd__clo_verbose_lost_signal_catcher )
         VG_(printf)("####### ignoring pthread_cond_wait(%p,%p)\n", cond, mutex);
      
      while( *cvlist_iter ) {
         if( (*cvlist_iter)->cond == cond ) {
            tl_assert( (*cvlist_iter)->mutex == mutex );
            return False; // Don't record HB-Relations
         }
         cvlist_iter = &(*cvlist_iter)->next;
      }
      *cvlist_iter = HG_(zalloc)( "hg.lsd.cond_wait_post.1", sizeof(CVList) );
      (*cvlist_iter)->cond = cond;
      (*cvlist_iter)->mutex = mutex;
      (*cvlist_iter)->next = NULL;
      
      return False; // Don't record HB-Relations
   } else {
      if( hg_lsd__clo_verbose_lost_signal_catcher )
         VG_(printf)("####### unconditional pthread_cond_wait(%p,%p)\n", cond, mutex);

      return True; // Do HB-Relations
   }
}

SO* hg_lsd__cond_direct_so ( ThrLSD* thr, void* cond, Bool annotation )
{
   CondVar* cv;
   SO*      so;
   OSet*    set;
   Addr     addr;

   cv = lookup_CondVar((Addr)cond);
   cv->signaled = False;

   /* ---------- W / R - relation ---------- */

   if( HG_(clo_lost_signal_detector) < 2 )
      return NULL;

   set = VG_(OSetWord_Create)( HG_(zalloc), "hg.lsd.cond_wait_post.1", HG_(free) );
   
   {
      tl_assert( alist_iterate_begin( thr->reads ) );
      while( alist_iterate_next(thr->reads, &addr) ) {
         if( !VG_(OSetWord_Contains)(set, addr) )
            VG_(OSetWord_Insert)(set, addr);
      }
      alist_iterate_end(thr->reads);
   }
   
   {
      UInt  score[cv->recorded_signals_size];
      UInt  best_score = 0;
      UInt  i;

      if(hg_lsd__clo_verbose_write_read_relation) { // pretty print
         VG_(printf)("thr#%d: hg_lsd__cond_wait_post(%p)\n", ((Thread*)thr->opaque)->coretid,(void*)cond);
         VG_(get_and_pp_StackTrace)( ((Thread*)thr->opaque)->coretid, 8 );
      }
      
      for( i = 0; i < cv->recorded_signals_size; i++ ) {
         score[i] = 0;
      }
      for( i = 0; i < cv->recorded_signals_size; i++ ) {
         AccessList* al = cv->recorded_signals[i];
         if( !al )
            break;

         if(hg_lsd__clo_verbose_write_read_relation) { // pretty print
            VG_(printf)("   [SO=%p] : ", cv->recorded_signals[i]->so);
         }
         tl_assert( alist_iterate_begin( al ) );
         while( alist_iterate_next(al, &addr) ) {
            if( VG_(OSetWord_Contains)(set, addr) ) {
               if(hg_lsd__clo_verbose_write_read_relation) { // pretty print
                  VG_(printf)("   %p", (void*)addr);
               }
               score[i]++;
            }
         }
         alist_iterate_end(al);
         
         if( score[i] > best_score ) {
            best_score = score[i];
         }
         if(hg_lsd__clo_verbose_write_read_relation) { // pretty print
            VG_(printf)("   ::: %d\n", score[i]);
         }
      }
      
      if( best_score == 0 ) {
         VG_(OSetWord_Destroy)(set);
         return NULL;
      }
      
      so = libhb_so_alloc();
      for( i = 0; i < cv->recorded_signals_size; i++ ) {
         if( !cv->recorded_signals[i] )
            break;
         if( score[i] == best_score ) {
            libhb_so_join(so, cv->recorded_signals[i]->so);
         }
      }      
      
      VG_(OSetWord_Destroy)(set);
      return so;
   }
}

void hg_lsd__cond_wait_timeout ( ThrLSD* thr, void* cond, SO* so )
{
   tl_assert( thr->waiting == True );
   thr->waiting = False;
}

/*----------------------------------------------------------------*/
/*--- Initializsation stuff                                    ---*/
/*----------------------------------------------------------------*/

void hg_lsd_init ( ThrLSD* (*get_current_ThrLSD) (void),
                   void    (*record_error_Misc) (ThrLSD*, char*),
                   void    (*annotate_wait) (void*, void*),
                   void    (*annotate_signal) (void*) ) {
   tl_assert(get_current_ThrLSD);
   main_get_current_ThrLSD = get_current_ThrLSD;
   tl_assert(record_error_Misc);
   main_record_error_Misc = record_error_Misc;
   tl_assert(annotate_wait);
   main_annotate_wait = annotate_wait;
   tl_assert(annotate_signal);
   main_annotate_signal = annotate_signal;

   tl_assert(map_cond_to_CondVar == NULL);
   map_cond_to_CondVar = VG_(newFM)( HG_(zalloc), "hg.lsd.init.2" ,HG_(free), NULL/*unboxed Word cmp*/);
   tl_assert(map_cond_to_CondVar != NULL);

   tl_assert(map_loop_addrs == NULL);
   map_loop_addrs = VG_(newFM)( HG_(zalloc), "hg.lsd.init.3" ,HG_(free), NULL/*unboxed Word cmp*/);
   tl_assert(map_loop_addrs != NULL);
}

void hg_lsd_shutdown ( Bool show_stats ) {
   if( hg_lsd__clo_verbose_lost_signal_catcher || hg_lsd__clo_verbose_write_read_relation || show_stats ) {
      VG_(printf)("%s","<<< BEGIN hg_lsd stats >>>\n");
      pp_admin_condvars(0);
      pp_map_loop_addrs(0);

      VG_(printf)(" lostsig_loop: %'10lu total jmps\n",        stats__total_jumps);
      VG_(printf)(" lostsig_loop: %'10lu loop jmps\n",        stats__loop_jumps);
      VG_(printf)(" lostsig_loop: %'10lu loop bodies reoccured\n",        stats__loopbodies_reoccurred);
      VG_(printf)(" lostsig_loop: %'10lu loop bodies reinstr.\n",        stats__loopbodies_reinstrumented);
      VG_(printf)(" lostsig_loop: %'10lu waiting loops instrumented\n",        stats__instrumented_loops);
      VG_(printf)(" lostsig_loop: %'10lu signaling blocks instrumented\n",        stats__instrumented_signals);

      VG_(printf)(" lostsig: %'10lu instr. jmps\n",        stats__instrumented_jumps);
      VG_(printf)(" lostsig: %'10lu signals sent\n",       stats__condition_signaled);
      VG_(printf)(" lostsig: %'10lu signals received\n",   stats__signals_received);
      VG_(printf)(" lostsig: %'10lu signals lost\n",       stats__signals_lost);
      VG_(printf)(" lostsig: %'10lu signals catched\n",    stats__signals_catched);
      VG_(printf)(" lostsig: %'10lu signals with w/r-rel\n",    stats__signals_direct);
      VG_(printf)(" lostsig: %'10lu barriers\n",           stats__barriers);

      if (1) {
         VG_(printf)("\n");
         VG_(printf)(" synchronization primitives: \n");
         VG_(printf)("           pthead_mutex: %'10lu calls,  %'10lu locks,    %'10lu unlocks\n",
               stats__pthread_mutex_lock + stats__pthread_mutex_unlock,
               stats__pthread_mutex_lock, stats__pthread_mutex_unlock );
         VG_(printf)("            pthead_cond: %'10lu calls,  %'10lu signals,  %'10lu waits\n",
               stats__pthread_cond_signals + stats__pthread_cond_waits,
               stats__pthread_cond_signals, stats__pthread_cond_waits );
         VG_(printf)("            pth_barrier: %'10lu calls\n",
               stats__pthread_barriers );
      }
      VG_(printf)("%s","<<< END hg_lsd stats >>>\n");
   }
#ifndef NDEBUG
   // TODO : FREE isn't sufficient for condVar
   VG_(deleteFM)( map_cond_to_CondVar, NULL, NULL );
   VG_(deleteFM)( map_loop_addrs, NULL, NULL );

#endif
}
