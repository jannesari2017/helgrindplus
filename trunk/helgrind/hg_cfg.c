/*
 * hg_cfg.c
 *
 *  Created on: 08.06.2009
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

#include "hg_basics.h"
#include "hg_cfg.h"

Int HG_(clo_control_flow_graph) = 3;

Bool hg_cfg__clo_verbose_control_flow_graph = False;
Bool hg_cfg__clo_show_control_flow_graph = False;
Bool hg_cfg__clo_ignore_pthread_spins = False;
Bool hg_cfg__clo_show_spin_reads = False;

UWord hg_cfg__clo_show_bb = 0;
UWord hg_cfg__clo_test_da = 0;

Bool hg_cfg__spin_reading = False;

typedef UWord VarOffset;

typedef
   enum {
      HG_CFG_VAR_UNDEFINED,
      HG_CFG_VAR_CONDITION,
      HG_CFG_VAR_TEMP,
      HG_CFG_VAR_REGISTER,
      HG_CFG_VAR_ADDRESS,
      HG_CFG_VAR_LOAD
   } VarType;

typedef
   struct _Variable {
      VarType   type;
      VarOffset index;
      WordFM * dependencies;
      Bool marked; // used by spin_read_detect()
      Bool modified;
   } Variable;
   
/*
 * This is a representation of a code block within the client program
 */
struct _BasicBlock {
   // single linked list to be able to access all blocks in random order 
   struct _BasicBlock* admin;
   UInt   uid;
   
   // control flow links
   struct _BasicBlock* next;
   struct _BasicBlock* branch;
   
   // loop detection
   Bool loop_in_next;
   Bool loop_in_branch;

   // code links
   struct _BasicBlock* pred;
   struct _BasicBlock* succ;
   
   // spin reads detection
   Bool spinread_detection_done;

   enum {
      HG_CFG_Direct,
      HG_CFG_Indirect
   } next_type;

   enum {
      HG_CFG_Normal,
      HG_CFG_Inherit,
      HG_CFG_Branch,
      HG_CFG_PLTEntry,
      HG_CFG_Jumpslot
   } blocktype;
   
   enum {
      HG_CFG_NoMark,
      HG_CFG_Analysed,
      HG_CFG_Visited
   } mark;
   
   Addr base;
   Addr length;
};
typedef struct _BasicBlock BBlock;


static BBlock * admin_bblocks = NULL;

WordFM* map_basicblocks = NULL;

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
/*--- Helper Functions                                         ---*/
/*----------------------------------------------------------------*/

// Extract immediate value from IR 
static
Addr value_of ( IRExpr* e ) {
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

static UInt bb_new_uid( void ) {
   static UInt last_uid = 0;
   
   last_uid++;   
   return last_uid;
}

static BBlock* mk_BBlock( Addr base, Addr length ) {
   BBlock* bb = NULL;
   
   bb = HG_(zalloc)( "mk.BBlock.1", sizeof(BBlock) );
   tl_assert( bb );
   
   bb->admin  = admin_bblocks; 
   bb->uid    = bb_new_uid();

   bb->base   = base;
   bb->length = length;
   bb->blocktype = HG_CFG_Normal;
   bb->mark   = HG_CFG_NoMark;
   
   bb->next   = NULL;
   bb->next_type = HG_CFG_Direct;
   bb->branch = NULL;
      
   bb->pred   = NULL;
   bb->succ   = NULL;

   bb->loop_in_next   = False;
   bb->loop_in_branch = False;
   
   bb->spinread_detection_done = False;
   
   admin_bblocks = bb;
   
   return bb;
}

static
void pp_BBlock( BBlock *bb, int d ) {
   Bool    name_found = False;
   static Char fnname[100];
   UWord   addr;
   
   space(d);
   VG_(printf)("BBlock %d (0x%lx, length=%ld) {\n", bb->uid, bb->base, bb->length);
   if( bb->next ) {
      space(d);
      VG_(printf)("    next: %s0x%lx%s\n",
            (bb->next_type==HG_CFG_Indirect)?"*":"",
            bb->next->base,
            bb->loop_in_next?" (loop)":"");
   }
   if( bb->branch ) {
      space(d);
      VG_(printf)("  branch: 0x%lx%s\n",
            bb->branch->base,
            bb->loop_in_branch?" (loop)":"");
   }
   
   space(d);
   VG_(printf)("    type: " );
   switch( bb->blocktype ) {
   case HG_CFG_Normal:
      VG_(printf)("Normal");
      name_found = VG_(get_fnname_w_offset) ( bb->base, fnname, sizeof(fnname) );
      break;
   case HG_CFG_Branch:
      VG_(printf)("Branch");
      name_found = VG_(get_fnname_w_offset) ( bb->base, fnname, sizeof(fnname) );
      break;
   case HG_CFG_Inherit:
      VG_(printf)("Inherit");
      name_found = VG_(get_fnname_w_offset) ( bb->base, fnname, sizeof(fnname) );
      break;
   case HG_CFG_Jumpslot:
      VG_(printf)("Jumpslot");
      name_found = VG_(get_fnname_w_offset) ( bb->base, fnname, sizeof(fnname) );
      break;
   case HG_CFG_PLTEntry:
      VG_(printf)("PLTEntry");
      tl_assert( bb->next );
      name_found = VG_(get_jumpslot_fnname)( bb->next->base, fnname, sizeof(fnname) );
      break;
   default:
      tl_assert(0);
   }
   VG_(printf)("\n");

   if( name_found ) {
      space(d);
      VG_(printf)("    info: %s\n", fnname );
   }

   switch( bb->mark ) {
   case HG_CFG_NoMark:
      break;
   case HG_CFG_Analysed:
      space(d);
      VG_(printf)("    mark: analysed\n");
      break;
   case HG_CFG_Visited:
      space(d);
      VG_(printf)("    mark: visited\n");
      break;
   default:
      tl_assert(0);
   }
   
   if( bb->spinread_detection_done ) {
      space(d);
      VG_(printf)("    spin reads: done\n" );
   } else {
      space(d);
      VG_(printf)("    spin reads: not finished\n" );
   }
   
   space(d);
   VG_(printf)("}\n");
}

/*----------------------------------------------------------------*/
/*--- Spin Read detection                                      ---*/
/*----------------------------------------------------------------*/

static WordFM * varmap_temps = NULL;
static WordFM * varmap_varset = NULL;

static Word cmp_Variables( UWord w1, UWord w2 ) {
   Variable * var1 = (Variable*) w1;
   Variable * var2 = (Variable*) w2;
   
   if( var1->type == var2->type ) {
      if( var1->index < var2->index )
         return -1;
      if( var1->index > var2->index )
         return 1;
      return 0;
   }
   else if( var1->type < var2->type )
      return -1;
   else
      return 1;
}

static Variable* mk_Variable( VarType type, VarOffset index) {
   Variable* v = NULL;
   v = HG_(zalloc)( "mk.Variable.1", sizeof(Variable) );
   tl_assert( v );
   v->type   = type;
   v->index  = index;
   v->marked = False;
   v->modified = False;
   v->dependencies = NULL;
   v->dependencies = VG_(newFM)(HG_(zalloc), "mk.Variable.3", HG_(free), cmp_Variables);
   tl_assert( v->dependencies );
   return v;
}

static void del_Variable( UWord w_var ) {
   Variable * var = (Variable*) w_var;
   
   if( var->type != HG_CFG_VAR_TEMP && var->dependencies )
      VG_(deleteFM)( var->dependencies, NULL, NULL );
   
   HG_(free)( var );
}

static void init_varmaps( void ) {
   tl_assert(varmap_temps == NULL);
   varmap_temps = VG_(newFM)( HG_(zalloc), "hg.cf.init.maps.1" ,HG_(free), cmp_Variables);
   tl_assert(varmap_temps != NULL);

   tl_assert(varmap_varset == NULL);
   varmap_varset = VG_(newFM)( HG_(zalloc), "hg.cf.init.maps.2" ,HG_(free), cmp_Variables);
   tl_assert(varmap_varset != NULL);
}

static void reset_varmaps( Bool resetAll ) {
   VG_(deleteFM)( varmap_temps, del_Variable, NULL );
   varmap_temps = VG_(newFM)( HG_(zalloc), "hg.cf.reset.maps.1" ,HG_(free), cmp_Variables );
   
   if( resetAll ) {
      VG_(deleteFM)( varmap_varset, del_Variable, NULL );
      varmap_varset = VG_(newFM)( HG_(zalloc), "hg.cf.reset.maps.2" ,HG_(free), cmp_Variables );
   }
}

/* Fetch a Variable from the maps or create one */
static Variable * varmap_lookup_WRK( VarType type, VarOffset index, Bool create ) {
   WordFM * map;
   Variable * var;
   Variable searchvar;
   
   searchvar.type = type;
   searchvar.index = index;
   
   if( type == HG_CFG_VAR_TEMP )
      map = varmap_temps;
   else
      map = varmap_varset;
   
   if( !VG_(lookupFM)( map, (UWord*)&var, NULL, (UWord)&searchvar ) ) {
      if( !create )
         return NULL;
      
      var = mk_Variable( type, index );
      VG_(addToFM)( map, (UWord)var, 0 );
   }

   return var;
}

static Variable * varmap_lookup_or_create( VarType type, VarOffset index ) {
   return varmap_lookup_WRK( type, index, True );
}

static Variable * varmap_lookup( VarType type, VarOffset index ) {
   return varmap_lookup_WRK( type, index, False );
}

static void add_dependency ( Variable* var, VarType type, VarOffset index ) {
   Variable dep;
   
   dep.type = type;
   dep.index = index;
   
   tl_assert( var->dependencies );
   if( !VG_(lookupFM)( var->dependencies, NULL, NULL, (UWord)&dep ) ) {
      Variable *new_dep = varmap_lookup_or_create( type, index );
      VG_(addToFM)( var->dependencies, (UWord)new_dep, 0 );
   }
}

static void
pp_Dependency( Variable * dep ) {
   if( dep->type == HG_CFG_VAR_TEMP )
      VG_(printf)( "t:%u ", (unsigned)dep->index );
   else if( dep->type == HG_CFG_VAR_REGISTER )
      VG_(printf)( "r:%u ", (unsigned)dep->index );
   else if( dep->type == HG_CFG_VAR_ADDRESS )
      VG_(printf)( "0x%lx ", dep->index );
   else if( dep->type == HG_CFG_VAR_LOAD )
      VG_(printf)( "load@(0x%lx) ", dep->index );
}

static void
pp_Variable( Variable * var ) {
   if( var->marked )
      VG_(printf)(">");
   
   pp_Dependency( var );
   
   if( var->modified )
      VG_(printf)("<-- ");
   else
      VG_(printf)(". ");
   
   if( var->dependencies ) {
      Variable * dep;
      
      VG_(initIterFM)( var->dependencies );
      while( VG_(nextIterFM)( var->dependencies, (UWord*)&dep, NULL ) ) {
         pp_Dependency( dep );
      }
      VG_(doneIterFM)( var->dependencies );
   }
}

static void
pp_varmap( WordFM* map, int d ) {
   UWord key;

   VG_(initIterFM)( map );
   while( VG_(nextIterFM)( map, &key, NULL ) ) {
      space(d);
      pp_Variable( (Variable*)key );
      VG_(printf)("\n");
   }
   VG_(doneIterFM)( map );
}

static void
pp_varmaps( int d ) {
   space(d);
   VG_(printf)("varmap {\n");
   pp_varmap( varmap_varset, d+3 );
   pp_varmap( varmap_temps, d+3 );
   space(d);
   VG_(printf)("}\n");
}

/*----------------------------------------------------------------*/
/*--- Instrumentation function for data flow analysis          ---*/
/*----------------------------------------------------------------*/

static
void detect_dependencies ( Variable * var, IRExpr * e ) {
   switch( e->tag ) {
      case Iex_CCall:
         break;
      case Iex_Const: {
         add_dependency( var, HG_CFG_VAR_ADDRESS, value_of(e) );
      } break;
      case Iex_Get: {
         add_dependency( var, HG_CFG_VAR_REGISTER, e->Iex.Get.offset );
      } break;
      case Iex_RdTmp: {
         Variable * tmp = varmap_lookup( HG_CFG_VAR_TEMP, e->Iex.RdTmp.tmp );
         if( tmp ) {
            Variable * dep;
            VG_(initIterFM)( tmp->dependencies );
            while( VG_(nextIterFM)( tmp->dependencies, (UWord*)&dep, NULL ) ) {
               VG_(addToFM)( var->dependencies, (UWord)dep, 0 );
            }
            VG_(doneIterFM)( tmp->dependencies );
         }
      } break;
      case Iex_Mux0X: {
         detect_dependencies( var, e->Iex.Mux0X.cond );
         detect_dependencies( var, e->Iex.Mux0X.expr0 );
         detect_dependencies( var, e->Iex.Mux0X.exprX );
      } break;
      case Iex_Qop: {
         detect_dependencies( var, e->Iex.Qop.arg1 );
         detect_dependencies( var, e->Iex.Qop.arg2 );
         detect_dependencies( var, e->Iex.Qop.arg3 );
         detect_dependencies( var, e->Iex.Qop.arg4 );
      } break;
      case Iex_Triop: {
         detect_dependencies( var, e->Iex.Triop.arg1 );
         detect_dependencies( var, e->Iex.Triop.arg2 );
         detect_dependencies( var, e->Iex.Triop.arg3 );
      } break;
      case Iex_Binop: {
         detect_dependencies( var, e->Iex.Binop.arg1 );
         detect_dependencies( var, e->Iex.Binop.arg2 );
      } break;
      case Iex_Unop: {
         detect_dependencies( var, e->Iex.Unop.arg );
      } break;
      case Iex_Load: { // cannot happen in flat IRSB
         tl_assert(0);
      } break;
      default:
         tl_assert(0);
   }
}

static
IRSB* analyse_data_dependencies ( VgCallbackClosure* closure,
                                  IRSB* bbIn,
                                  VexGuestLayout* layout,
                                  VexGuestExtents* vge,
                                  IRType gWordTy, IRType hWordTy )
{
   Int     i;
   IRSB*   bbOut;

   Bool  x86busLocked   = False;
   Bool  isSnoopedStore = False;
   
   /* Address and length of the current binary instruction */
   Addr   iaddr = 0,
          ilen  = 0;

   /* Output of this func is not stored anywhere, so we try to create a
      minimal but consistent basic block. */
   bbOut           = emptyIRSB();
   bbOut->tyenv    = emptyIRTypeEnv();
   bbOut->next     = mkIRExpr_HWord(0x1010);
   bbOut->jumpkind = Ijk_Boring;
   
   // Ignore any IR preamble preceding the first IMark
   i = 0;
   while (i < bbIn->stmts_used && bbIn->stmts[i]->tag != Ist_IMark) {
      i++;
   }
   
   if( hg_cfg__clo_test_da != 0 )
      ppIRSB(bbIn);
   
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
         case Ist_Dirty:
            /* None of these can contain anything of interest */
            break;
            
         case Ist_MBE:
            switch (st->Ist.MBE.event) {
               case Imbe_Fence:
                  break; /* not interesting */
               /* Imbe_Bus{Lock,Unlock} arise from x86/amd64 LOCK
                  prefixed instructions. */
               case Imbe_BusLock:
                  tl_assert(x86busLocked == False);
                  x86busLocked = True;
                  break;
               case Imbe_BusUnlock:
                  tl_assert(x86busLocked == True);
                  x86busLocked = False;
                  break;
                  /* Imbe_SnoopedStore{Begin,End} arise from ppc
                     stwcx. instructions. */
               case Imbe_SnoopedStoreBegin:
                  tl_assert(isSnoopedStore == False);
                  isSnoopedStore = True;
                  break;
               case Imbe_SnoopedStoreEnd:
                  tl_assert(isSnoopedStore == True);
                  isSnoopedStore = False;
                  break;
               default:
                  tl_assert(0);
            }
            break;
            
         case Ist_IMark:
            iaddr = st->Ist.IMark.addr;
            ilen  = st->Ist.IMark.len;
            break;            
            
         case Ist_Store: {
            IRExpr * addr = st->Ist.Store.addr;
            IRExpr * data = st->Ist.Store.data;
            
            /* Workaround to handle atomic instructions:
             *    Observe only reads during atomic instructions.
             */
            if( x86busLocked || isSnoopedStore )
               break;
            
            if( addr->tag == Iex_RdTmp ) {
               Variable * dep;
               Variable * tmp = varmap_lookup( HG_CFG_VAR_TEMP, addr->Iex.RdTmp.tmp );
               tl_assert( tmp );
               VG_(initIterFM)( tmp->dependencies );
               while( VG_(nextIterFM)( tmp->dependencies, (UWord*)&dep, NULL ) ) {
                  dep->modified = True;
                  detect_dependencies( dep, data );
               }
               VG_(doneIterFM)( tmp->dependencies );
            } else if ( addr->tag == Iex_Const ) {
               Variable * var = varmap_lookup_or_create( HG_CFG_VAR_ADDRESS, value_of(addr) );
               var->modified = True;
               detect_dependencies( var, data );
            }
         } break;
            
         case Ist_Put: {
            IRExpr * data = st->Ist.Put.data;
            Variable * var_reg = varmap_lookup_or_create( HG_CFG_VAR_REGISTER, st->Ist.Put.offset );
            var_reg->modified = True;
            detect_dependencies( var_reg, data );
         } break;
            
         case Ist_WrTmp: {
            IRExpr * data = st->Ist.WrTmp.data;
            Variable * var_temp = varmap_lookup_or_create( HG_CFG_VAR_TEMP, st->Ist.WrTmp.tmp );
            var_temp->modified = True;
            if( data->tag == Iex_Load ) {
               IRExpr * addr = data->Iex.Load.addr;
               Variable * var_load = varmap_lookup_or_create( HG_CFG_VAR_LOAD, iaddr );
               
               add_dependency( var_temp, HG_CFG_VAR_LOAD, iaddr );
               detect_dependencies( var_load, addr );
               var_load->modified = True;
            } else {
               detect_dependencies( var_temp, data );
            }
         } break;

         case Ist_Exit: {
            Variable *condition;
            IRExpr *guard = st->Ist.Exit.guard;
            
            condition = varmap_lookup_or_create( HG_CFG_VAR_CONDITION, 0 );
            detect_dependencies( condition, guard );
         } break;
         
         default:
            tl_assert(0);

      } /* switch (st->tag) */
   } /* iterate over bbIn->stmts */
   
   return bbOut;
}

/*----------------------------------------------------------------*/
/*--- Handling basic blocks                                    ---*/
/*----------------------------------------------------------------*/

static void _update_bb( BBlock *bb, Addr length ) {
   Addr base = bb->base;
   BBlock *pred = bb->pred;
   BBlock *succ = bb->succ;
   bb->length = length;

   tl_assert( length != 0 );
   
   if( pred && pred->base < base && base < pred->base+pred->length ) {
      // Split block!
      bb->next   = pred->next;
      bb->branch = pred->branch;
      pred->next = bb;
      pred->branch = NULL;
      pred->blocktype = HG_CFG_Inherit;
   }

   if( succ && base < succ->base && succ->base < base+length ) {
      // successor block is within this block
      bb->next = succ;
      bb->branch = NULL;
      bb->blocktype = HG_CFG_Inherit;
      bb->mark = HG_CFG_Analysed;
      //bb = succ;
   }
}

static BBlock* _register_new_bb( Addr base, Addr length ) {
   Bool  ok;
   UWord kMin, vMin = 0, kMax, vMax = 0;
   UWord minAddr = 0;
   UWord maxAddr = ~minAddr;
   BBlock * bb;

   ok = VG_(findBoundsFM)( map_basicblocks,
                           &kMin, &vMin, &kMax, &vMax,
                           minAddr, minAddr,
                           maxAddr, maxAddr, base );
   tl_assert(ok);

   // register the new block 
   bb = mk_BBlock( base, length );
   VG_(addToFM)( map_basicblocks, (UWord)base, (UWord)bb );
   
   // maybe there is a predecessor block?
   if( kMin != minAddr ) {
      // possible pred bblock found
      BBlock *pred;
      
      pred = (BBlock*)vMin;
      tl_assert(pred);
      bb->pred   = pred;
      pred->succ = bb;
   }

   // maybe there is a successor block?
   if( kMax != maxAddr ) {
      BBlock *succ;
      
      succ = (BBlock*)vMax;
      tl_assert(succ);
      bb->succ   = succ;
      succ->pred = bb; 
   }
   
   if( length != 0 )
      _update_bb( bb, length );
   
   return bb;
}

static BBlock* register_bb( Addr base, Addr length ) {
   BBlock* bb = NULL;

   Bool found;
   UWord keyW, valW;
   
   found = VG_(lookupFM)( map_basicblocks, &keyW, &valW, (UWord)base );
   if( found ) {
      bb = (BBlock*) valW;
      tl_assert( bb->base == base );
      // update length of basic block
      if( length > 0 && bb->length == 0 )
         _update_bb( bb, length );
   } else {
      bb = _register_new_bb( base, length );
   }
   
   return bb;
}

/*----------------------------------------------------------------*/
/*--- This is the hook into helgrind's normal                  ---*/
/*--- instrumentation process                                  ---*/
/*----------------------------------------------------------------*/

static Bool looking_ahead = False;
static Bool dfs_search( BBlock * bb, unsigned depth ); /* fwd */

static UWord stats__loops_found   = 0;
static UWord stats__spins_found   = 0;

static WordFM * map_spinreads = NULL;

static
void pp_map_spinreads( int d ) {
   static Char fnname[100];
   UWord key;

   VG_(initIterFM)( map_spinreads );
   while( VG_(nextIterFM)( map_spinreads, &key, NULL ) ) {
      space(d);
      if( VG_(get_fnname_w_offset) ( key, fnname, sizeof(fnname) ) ) {
         VG_(printf)("0x%lx: %s\n", key, fnname );
      } else {
         VG_(printf)("0x%lx\n", key );
      }
   }
   VG_(doneIterFM)( map_spinreads );
}

static VG_REGPARM(0)
void start_spin_reading(void) {
   hg_cfg__spin_reading = True;
}

static VG_REGPARM(0)
void stop_spin_reading(void) {
   hg_cfg__spin_reading = False;
}

static inline
int _get_bb_index ( VexGuestExtents* vge,
                   Addr             iaddr ) {
   int i = 0;
   while( i < vge->n_used ) {
      if( vge->base[i] <= iaddr && iaddr < vge->base[i]+vge->len[i] )
         return i;
      i++;
   }
   return -1;
}

static
BBlock * get_current_bb ( BBlock*          bblocks[],
                          VexGuestExtents* vge,
                          Addr             iaddr ) {
   BBlock * bb;
   BBlock * succ;
   int bb_index = _get_bb_index( vge, iaddr );
   tl_assert( bb_index != -1 );
   bb = bblocks[bb_index];
   
   succ = bb->succ;
   while( succ && succ->base <= iaddr && iaddr < succ->base + succ->length ) {
      bb = succ;
      succ = bb->succ;
   }
   
   return bb;
}

Bool hg_cfg_instrument_bb (  HG_CFG_INSTR_MODE mode,
                             IRSB* bbIn,
                             IRSB* bbOut,
                             VgCallbackClosure* closure,
                             VexGuestLayout* layout,
                             VexGuestExtents* vge,
                             IRStmt* stmt )
{
   static BBlock* bblocks[3];
   
   // 
   static Bool   spin_read_detected = False;
   
   // Control flow analysis and spin read detection off?
   if ( HG_(clo_control_flow_graph) <= 0 )
      return True;
   
   
   /////////////////////////////////////////////////////////////////
   // Pre instrumentatin analysis                                 //
   /////////////////////////////////////////////////////////////////
   
   if ( mode == HG_CFG_IM_Pre ) {
      BBlock *curbb = NULL;
      BBlock *dstbb;
      BBlock *lastbb = NULL;
      Int i;
      
      /* Address and length of the current binary instruction */
      Addr   iaddr = 0,
             ilen  = 0;
      
      // Initialization
      spin_read_detected = False;
      
      /////////////////////////////////////////////////////////////////
      // Phase 0: Insert new basic blocks into graph                 //
      /////////////////////////////////////////////////////////////////
      
      for( i = 0; i < vge->n_used; i++ ) {
         bblocks[i] = register_bb( vge->base[i], vge->len[i] );
      }

      /////////////////////////////////////////////////////////////////
      // Phase 1: Set the next pointers of blocks                    //
      /////////////////////////////////////////////////////////////////
      
      for( i = 0; i < (vge->n_used)-1; i++ ) {
         if( bblocks[i]->mark != HG_CFG_Analysed )
            bblocks[i]->next = bblocks[i+1]; 
      }

      // cfg analysis phase 1.5: last basic block may be an indirect jump //
      
      curbb = bblocks[(vge->n_used)-1];      
      if( curbb->mark != HG_CFG_Analysed ) {
         switch( bbIn->jumpkind ) {
         case Ijk_Boring: {
            if( bbIn->next->tag == Iex_Const ) {
               dstbb = register_bb( value_of(bbIn->next), 0 );
               tl_assert(dstbb);
               curbb->next = dstbb;
            } else
            if( bbIn->next->tag == Iex_RdTmp ) {
               Addr jumpdest = 0;
               // read contents of temp
               for (i = 0; i < bbIn->stmts_used; i++) {
                  IRStmt* st = bbIn->stmts[i];
                  tl_assert(st);
                  if( st->tag == Ist_WrTmp &&
                      st->Ist.WrTmp.tmp == bbIn->next->Iex.RdTmp.tmp )
                  {
                     jumpdest = value_of(st->Ist.WrTmp.data);
                  }
               }
               // detect dynamic function calls
               if( jumpdest != 0 ) {
                  VgSectKind dst_kind;
                  VgSectKind cur_kind;
                  
                  dstbb = register_bb( jumpdest, 0 );
                  tl_assert(dstbb);
                  
                  dst_kind = VG_(seginfo_sect_kind)( NULL, 0, jumpdest );
                  cur_kind = VG_(seginfo_sect_kind)( NULL, 0, curbb->base );
                  
                  // Indirect jump to jump slot ?
                  if( dst_kind == Vg_SectGOTPLT &&
                      cur_kind == Vg_SectPLT ) {
                     dstbb->blocktype = HG_CFG_Jumpslot;
                     curbb->blocktype = HG_CFG_PLTEntry;
                     curbb->next = dstbb;
                     curbb->next_type = HG_CFG_Indirect;
                     curbb->mark = HG_CFG_Analysed;
                  } else {
                     curbb->next = dstbb;
                     curbb->next_type = HG_CFG_Indirect;
                  }
               }
            } else {
               tl_assert(0);
            } /* if( bbIn->next->tag == ... ) */
         } break;
         case Ijk_Call:
         case Ijk_ClientReq:
         case Ijk_Sys_syscall:
         case Ijk_Sys_int32:
         case Ijk_Sys_int128:
         case Ijk_Sys_int129:
         case Ijk_Sys_int130:
         case Ijk_Sys_sysenter:
         case Ijk_Yield:
            curbb->next = register_bb( curbb->base+curbb->length, 0 );
            break;
         case Ijk_Ret:
         case Ijk_SigSEGV:
         case Ijk_SigTRAP:
         case Ijk_NoRedir:
         case Ijk_EmFail:
            break;
         default:
            ppIRSB(bbIn);
            tl_assert(0);
         } /* switch( bbIn->jumpkind ) */
      } /* if( curbb->mark != HG_CFG_Analysed ) */
      curbb = NULL;
      
      // --da= switch **DEBUGGING**
      {
         if( hg_cfg__clo_test_da != 0 ) {
            VG_(translate_ahead)( hg_cfg__clo_test_da, analyse_data_dependencies );
            pp_varmaps(4);
            reset_varmaps(True);
            hg_cfg__clo_test_da = 0;
         }
      }

      /////////////////////////////////////////////////////////////////
      // Phase 2: handle branches                                    //
      /////////////////////////////////////////////////////////////////

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
            case Ist_Put:
            case Ist_PutI:
            case Ist_MBE:
            case Ist_Dirty:
            case Ist_WrTmp:
               /* None of these are of interest for control flow */
               break;


            /* this is used to detect loop-blocks for the lost signal detection */
            case Ist_IMark: {
               BBlock * bb;
               
               iaddr = st->Ist.IMark.addr;
               ilen  = st->Ist.IMark.len;

               bb = get_current_bb( bblocks, vge, iaddr );
               if( bb != curbb ) {
                  lastbb = curbb;
                  curbb = bb;
                  
                  if( curbb->mark != HG_CFG_Analysed &&
                      bb->blocktype == HG_CFG_PLTEntry )
                  {
                     curbb->next = register_bb( curbb->base+curbb->length, 0 );
                     curbb->branch = bb;
                  }
               }

               break;
            }
            
            // Detect a chased function call
            case Ist_Store: {
               if( curbb->mark != HG_CFG_Analysed ) {
                  IRExpr * data = st->Ist.Store.data;
                  if( data->tag == Iex_Const &&
                      value_of(data) == curbb->base + curbb->length &&
                      curbb->next != NULL &&
                      curbb->next->base != curbb->base + curbb->length ) {
                     curbb->branch = curbb->next;
                     curbb->next = register_bb( curbb->base + curbb->length, 0 );
                  }
               }
            } break;
            
            case Ist_Exit: {
               Addr    dst;
               
               tl_assert(iaddr != 0);
               tl_assert(ilen  != 0);
               
               if( curbb->mark != HG_CFG_Analysed ) {
                  curbb->blocktype = HG_CFG_Branch;
                  
                  dst = value_of(IRExpr_Const(st->Ist.Exit.dst));
                  dstbb = register_bb( dst, 0 );
                  tl_assert(dstbb);
                  
                  curbb->branch = dstbb;
               }
               
               break;
            }
            
            default:
               tl_assert(0);

         } /* switch (st->tag) */
      } /* iterate over bbIn->stmts */


      /////////////////////////////////////////////////////////////////
      // Phase 3: detect spin read loops                             //
      /////////////////////////////////////////////////////////////////
      
      BBlock *debug_bb = NULL;
      for( i = 0; i < (vge->n_used); i++ ) {
         // for debuging with cmd line switch --show-bb=0x....
         if( hg_cfg__clo_show_bb != 0 &&
             hg_cfg__clo_show_bb == vge->base[i] ) {
            VG_(printf)("^^^^^^^^ %d %s\n",i, (looking_ahead?"(ahead)":""));
            debug_bb = bblocks[i];
            pp_BBlock( debug_bb, 4 );
            if( debug_bb->next )
               pp_BBlock( debug_bb->next, 8 );
            if( debug_bb->branch )
               pp_BBlock( debug_bb->branch, 8 );
         }

         // mark blocks as analysed
         bblocks[i]->mark = HG_CFG_Analysed;
      }
      
      // search for spin loops
      if( looking_ahead == False ) {
         static BBlock* bblocks_copy[3];
         
         // backup bblocks
         bblocks_copy[0] = bblocks[0];
         bblocks_copy[1] = bblocks[1];
         bblocks_copy[2] = bblocks[2];
         
         for( i = 0; i < (vge->n_used); i++ ) {
            dfs_search( bblocks_copy[i], HG_(clo_control_flow_graph) );
            // bblocks are broken after this !
         }
         
         bblocks[0] = bblocks_copy[0];
         bblocks[1] = bblocks_copy[1];
         bblocks[2] = bblocks_copy[2];
      }

      // for debuging with cmd line switch --show-bb=0x....
      if( debug_bb ) {
         ppIRSB(bbIn);
         pp_BBlock( debug_bb, 4 );
         if( debug_bb->next )
            pp_BBlock( debug_bb->next, 8 );
         if( debug_bb->branch )
            pp_BBlock( debug_bb->branch, 8 );
         VG_(printf)("vvvvvvvv\n");
         debug_bb = NULL;
      }
   }
   
   /////////////////////////////////////////////////////////////////
   // Phase 4: instrument spin reads                              //
   /////////////////////////////////////////////////////////////////
   
   else if ( UNLIKELY(mode == HG_CFG_IM_Post) ) {
      static Bool spin_reading = False;
      if( stmt == NULL ) {
         if( spin_reading ) {
            IRDirty* di;
            di = unsafeIRDirty_0_N( 0, "stop_spin_reading", &stop_spin_reading, mkIRExprVec_0() );
            addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
            spin_reading = False;
         }
         return True;
      }
      if( stmt->tag == Ist_IMark ) {
         Addr iaddr = stmt->Ist.IMark.addr;
         if( spin_reading ) {
            IRDirty* di;
            di = unsafeIRDirty_0_N( 0, "stop_spin_reading", &stop_spin_reading, mkIRExprVec_0() );
            addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
         }
         if( VG_(lookupFM)(map_spinreads, NULL, NULL, (UWord)iaddr ) ) {
            IRDirty* di;
            di = unsafeIRDirty_0_N( 0, "start_spin_reading", &start_spin_reading, mkIRExprVec_0() );
            addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
            spin_reading = True;
         }
      }
   }
   
   
   else {
      // Should not happen
      tl_assert(0);
   }

   return True;
}

/*----------------------------------------------------------------*/
/*--- Ahead translation stuff                                  ---*/
/*----------------------------------------------------------------*/

static
IRSB* look_ahead ( VgCallbackClosure* closure,
                   IRSB* bbIn,
                   VexGuestLayout* layout,
                   VexGuestExtents* vge,
                   IRType gWordTy, IRType hWordTy )
{
   IRSB*   bbOut;

   /* Output of this func is not stored anywhere, so we try to create a
      minimal but consistent basic block. */
   bbOut           = emptyIRSB();
   bbOut->tyenv    = emptyIRTypeEnv();
   bbOut->next     = mkIRExpr_HWord(0xdeadbeef);
   bbOut->jumpkind = Ijk_Boring;
   
   looking_ahead = True;
   
   hg_cfg_instrument_bb( HG_CFG_IM_Pre, bbIn, bbOut, closure, layout, vge, NULL );

   looking_ahead = False;
   
   return bbOut;
}

static
Bool dfs_search_WRK( BBlock * bb, unsigned current_depth, unsigned max_depth )
{
   UShort ret = 0;
   int    old_mark;
   Bool   loop_found = False;
   
   /* -------- break condition -------- */
   if( current_depth > max_depth )
      return False;
   
   /* -------- detect loops -------- */
   if( bb->mark == HG_CFG_Visited ) {
      // we found a loop!
      return True;
   }

   /* -------- discover unknown blocks -------- */
   if( bb->mark != HG_CFG_Analysed ) {
      // This block was not yet instrumented
      ret = VG_(translate_ahead)( bb->base, look_ahead );
      if( ret == 0 ) {
         // Ignore errors during ahead translation and go on normally
         return False;
      }
   }
   tl_assert( bb->length != 0 );
   tl_assert( bb->mark == HG_CFG_Analysed );

   /* -------- don't go deeper on certain block types -------- */
   if( bb->blocktype == HG_CFG_PLTEntry ||
       bb->blocktype == HG_CFG_Jumpslot )
      return False;
   
   /* -------- go deeper -------- */
   old_mark = bb->mark;
   if( current_depth == 0 ) {
      bb->mark = HG_CFG_Visited;
   }
   
   // search deeper into next
   if( bb->next &&
       bb->next_type == HG_CFG_Direct )
   {
      if( dfs_search_WRK( bb->next, current_depth+1, max_depth ) == True ) {
         loop_found = True;
         bb->loop_in_next = True;
      }
   }
   
   // search deeper into branch
   if( bb->branch )
   {
      if( dfs_search_WRK( bb->branch, current_depth+1, max_depth ) == True ) {
         loop_found = True;
         bb->loop_in_branch = True;
      }
   }

   if( current_depth == 0 )
      bb->mark = old_mark;
   tl_assert(bb->mark != HG_CFG_Visited);
   
   return loop_found;
}

static
void dfs_search_analyse_da( BBlock * bb )
{
   Bool loop_in_next,
        loop_in_branch;
   
   /* -------- data analysis -------- */
   VG_(translate_ahead)( bb->base, analyse_data_dependencies );
   reset_varmaps( False );

   /* copy and reset loop flags */
   loop_in_next = bb->loop_in_next;
   loop_in_branch = bb->loop_in_branch; 
   
   bb->loop_in_next = False; 
   bb->loop_in_branch = False;
   
   /* -------- go deeper -------- */
   // search deeper into next
   if( bb->next && loop_in_next )
   {
      dfs_search_analyse_da( bb->next );
   }
   
   // search deeper into branch
   if( bb->branch && loop_in_branch )
   {
      dfs_search_analyse_da( bb->branch );
   }
}

static
Bool load_is_const( Variable *load ) {
   Variable *var;
   
   VG_(initIterFM)( load->dependencies );
   while( VG_(nextIterFM)( load->dependencies, (UWord*)&var, NULL ) ) {
      if( var->modified == True ) {
         VG_(doneIterFM)( load->dependencies );
         return False;
      }
   }
   VG_(doneIterFM)( load->dependencies );
   
   return True;
}

#if 1

/*
 * Check if dependencies of condition are all constant
 */
static
Bool loop_condition_is_const( Variable *condition ) {
   Variable *var;
   
   VG_(initIterFM)( condition->dependencies );
   while( VG_(nextIterFM)( condition->dependencies, (UWord*)&var, NULL ) ) {
      if( var->type == HG_CFG_VAR_LOAD ) {
         if( !load_is_const(var) ) {
            VG_(doneIterFM)( condition->dependencies );
            return False;
         }
      }
      else if( var->modified == True ) {
         VG_(doneIterFM)( condition->dependencies );
         return False;
      }
   }
   VG_(doneIterFM)( condition->dependencies );
   
   return True;
}

static
Bool mark_spin_reads_WRK( Variable *condition, BBlock *bb ) {
   Variable *dep;
   Bool found_spin_read = False;
   
   VG_(initIterFM)( condition->dependencies );
   while( VG_(nextIterFM)( condition->dependencies, (UWord*)&dep, NULL ) ) {
      if( dep->type == HG_CFG_VAR_LOAD &&
          load_is_const( dep ) &&
          dep->index >= bb->base &&
          dep->index < bb->base+bb->length ) {
            VG_(addToFM)( map_spinreads, (UWord)dep->index, 0 );
            found_spin_read = True;
      }
   }
   VG_(doneIterFM)( condition->dependencies );
   
   return found_spin_read;
}

static
Bool mark_spin_reads( BBlock * bb )
{
   Variable *condition = NULL;
   
   condition = varmap_lookup( HG_CFG_VAR_CONDITION, 0 );
   if( !condition )
      return False;
   
   if( !loop_condition_is_const( condition ) )
      return False;
   
   return mark_spin_reads_WRK( condition, bb );
}

#endif

#if 0
static
Bool mark_spin_reads_tag( BBlock *bb ) {
   Variable *dep;
   Bool found_spin_read = False;

   VG_(initIterFM)( varmap_varset );
   while( VG_(nextIterFM)( varmap_varset, (UWord*)&dep, NULL ) ) {
      if( dep->type == HG_CFG_VAR_LOAD &&
          dep->marked == True &&
          load_is_const( dep ) &&
          dep->index >= bb->base &&
          dep->index < bb->base+bb->length ) {
            VG_(addToFM)( bb->spin_reads, (UWord)dep->index, 0 );
            found_spin_read = True;
      }
   }
   VG_(doneIterFM)( varmap_varset );
   
   return found_spin_read;
}

static
void mark_spin_reads_scan( Variable *var ) {
   Variable *dep;

   var->marked = True;
   VG_(initIterFM)( var->dependencies );
   while( VG_(nextIterFM)( var->dependencies, (UWord*)&dep, NULL ) ) {
      if( dep->marked == True )
         continue;
      mark_spin_reads_scan( dep );
   }
   VG_(doneIterFM)( var->dependencies );
}

static
Bool mark_spin_reads( BBlock * bb )
{
   Variable *condition = NULL;
   
   condition = varmap_lookup( HG_CFG_VAR_CONDITION, 0 );
   if( !condition )
      return False;
   
   mark_spin_reads_scan( condition );
   return mark_spin_reads_tag( bb );
}

#endif

static
Bool dfs_search( BBlock * bb, unsigned depth )
{
   static UChar buf_obj[4096];
   Bool found_spin_read = False;
   Bool found_loop = False;
   
   // run only once
   if( bb->spinread_detection_done == True )
      return False;
   bb->spinread_detection_done = True;
   
   // Filter
   VG_(seginfo_sect_kind)( buf_obj, sizeof(buf_obj), bb->base );
   if( VG_(string_match)("/lib*/libc-*.so", buf_obj) ||
       VG_(string_match)("/lib*/ld-*.so", buf_obj) ||
       VG_(string_match)("*/vgpreload*", buf_obj) )
   {
      return False;
   }
   if( hg_cfg__clo_ignore_pthread_spins == True &&
       VG_(string_match)("/lib*/libpthread-*.so", buf_obj) ) {
      return False;
   }
   
search_again:
   if( bb->blocktype == HG_CFG_Inherit ) {
      BBlock *nextbb = bb->next;
      
      if( dfs_search_WRK( nextbb, 0, depth ) )
      {
         dfs_search_analyse_da( bb );
         dfs_search_analyse_da( nextbb );
         found_spin_read = mark_spin_reads( bb );
         found_loop = True;
      }
   } else {
      if( dfs_search_WRK( bb, 0, depth ) )
      {
         dfs_search_analyse_da( bb );
         found_spin_read = mark_spin_reads( bb );
         found_loop = True;
      } else if( bb->blocktype == HG_CFG_Inherit ) {
         // This block turned into Inherit type
         goto search_again;
      }
   }
   

   if( hg_cfg__clo_verbose_control_flow_graph ) {
      if( found_spin_read ) {
         VG_(printf)("************spin found in %s\n", buf_obj);
         pp_varmaps(4);
         pp_BBlock( bb, 0 );
      }
      //else
      //   VG_(printf)("loop found!\n");
   }
   
   reset_varmaps( True );
   
   if( found_loop )
      stats__loops_found++;
   if( found_spin_read )
      stats__spins_found++;
   
   return found_spin_read;
}

Bool hg_cfg_is_spin_reading( void ) {
   return hg_cfg__spin_reading;
}

/*----------------------------------------------------------------*/
/*--- Initializsation stuff                                    ---*/
/*----------------------------------------------------------------*/

void hg_cfg_init ( void ) {
   tl_assert(map_basicblocks == NULL);
   map_basicblocks = VG_(newFM)( HG_(zalloc), "hg.cf.init.1" ,HG_(free), NULL/*unboxed Word cmp*/);
   tl_assert(map_basicblocks != NULL);
   
   tl_assert(map_spinreads == NULL);
   map_spinreads = VG_(newFM)(HG_(zalloc), "hg.cf.inst.2", HG_(free), NULL /* unboxed Word cmp */);
   tl_assert(map_spinreads != NULL);
   
   init_varmaps();
}

static
void validate_bbs ( void ) {
   BBlock *cur = admin_bblocks;

   while(cur) {
      if( cur->pred ) {
         tl_assert( cur->pred->base < cur->base );
         tl_assert( cur->pred->succ = cur );
      }
      if( cur->succ ) {
         tl_assert( cur->base < cur->succ->base );
         tl_assert( cur->succ->pred = cur );
      }
      
      tl_assert( cur->mark != HG_CFG_Visited );
      
      cur = cur->admin;
   }  
}

void hg_cfg_shutdown ( Bool show_stats ) {
   validate_bbs();
   
   if( hg_cfg__clo_show_control_flow_graph ||
       hg_cfg__clo_verbose_control_flow_graph ||
       hg_cfg__clo_show_spin_reads ||
       show_stats ) {
      VG_(printf)("%s","<<< BEGIN hg_cf stats >>>\n");
      if( hg_cfg__clo_show_control_flow_graph ) {
         VG_(printf)("   %s","<<< BEGIN vcg >>>\n");
         VG_(printf)("digraph \"estimated CFG\" {\n");
         
         {
            BBlock *cur = admin_bblocks;
   #        define BUF_LEN    4096
            
            while(cur) {
               static Char *sectkind;
               UInt  lineno;
               static UChar buf_srcloc[BUF_LEN];
               static UChar buf_dirname[BUF_LEN];
               static UChar buf_fn[BUF_LEN];
               static UChar buf_obj[BUF_LEN];
               Bool  know_dirinfo = False;
               Bool  know_srcloc  = VG_(get_filename_linenum)(
                                       cur->base, 
                                       buf_srcloc,  BUF_LEN, 
                                       buf_dirname, BUF_LEN, &know_dirinfo,
                                       &lineno 
                                    );
               Bool  know_fnname  = VG_(get_fnname) (cur->base, buf_fn, BUF_LEN);
               
               sectkind = (Char*)VG_(pp_SectKind(VG_(seginfo_sect_kind)( buf_obj, sizeof(buf_obj), cur->base )));
   #if 0
               if ( VG_(string_match)("/lib*/ld-*.so", buf_obj) ||
                    VG_(string_match)("/lib*/libc-*.so", buf_obj) ||
                    VG_(string_match)("/lib*/librt-*.so", buf_obj) ||
                    VG_(string_match)("/lib*/libpthread-*.so", buf_obj) ||
                    VG_(string_match)("*/libgomp.so*", buf_obj) ||
                    VG_(string_match)("*/vgpreload*", buf_obj) ||
                    VG_(strcmp)("???", buf_obj) == 0 ) {
                  cur = cur->admin;
                  continue;
               }
   #endif
               
               switch( cur->blocktype ) {
               case HG_CFG_Normal: {
                  VG_(printf)("%d [shape=ellipse,color=lawngreen,style=filled", cur->uid );
                  break;
               }
               case HG_CFG_Inherit: {
                  VG_(printf)("%d [shape=ellipse", cur->uid );
                  break;
               }
               case HG_CFG_Branch:
                  VG_(printf)("%d [shape=diamond,color=yellow,style=filled", cur->uid );               
                  break;
               case HG_CFG_Jumpslot: {
                  know_fnname = VG_(get_jumpslot_fnname)( cur->base, buf_fn, BUF_LEN );
                  VG_(printf)("%d [shape=box,color=maroon,style=filled", cur->uid );
                  break;
               }
               case HG_CFG_PLTEntry: {
                  tl_assert( cur->next );
                  know_fnname = VG_(get_jumpslot_fnname)( cur->next->base, buf_fn, BUF_LEN );
                  VG_(printf)("%d [shape=ellipse,color=lightsalmon,style=filled", cur->uid );
                  break;
               }
               default:
                  tl_assert(0);
               }
               
               VG_(printf)(",label=\"" );
               
               if( know_fnname )
                  VG_(printf)("<%s>", buf_fn );
               else
                  VG_(printf)("<?> in %s", sectkind);
               
               if( know_srcloc )
                  VG_(printf)(" (%s:%d)\\n", buf_srcloc, lineno );
               else
                  VG_(printf)(" (%s)\\n", buf_obj );
   
               
               if( cur->length > 0 )
                  VG_(printf)("0x%lx - 0x%lx\"];\n", cur->base, cur->base+cur->length );
               else
                  VG_(printf)("0x%lx\"];\n", cur->base );
               
               if( cur->next ) {
                  if( cur->next->blocktype == HG_CFG_PLTEntry )
                     VG_(printf)("%d -> %d [color=orange];\n",
                           cur->uid, cur->next->uid );
                  else
                     VG_(printf)("%d -> %d [color=%s,weight=8];\n",
                           cur->uid, cur->next->uid, (cur->loop_in_next?"tomato,style=bold":"darkgreen") );
               }
               if( cur->branch ) {
                  if( cur->branch->blocktype == HG_CFG_PLTEntry )
                     VG_(printf)("%d -> %d [color=orange];\n",
                           cur->uid, cur->branch->uid );
                  else
                     VG_(printf)("%d -> %d [color=%s,weight=8];\n",
                           cur->uid, cur->branch->uid, (cur->loop_in_branch?"red,style=bold":"blue") );
               }
   
               /*
               if( cur->succ ) {
                  VG_(printf)("edge: { color: lightgray sourcename: \"%d\" targetname:\"%d\" }\n", cur->uid, cur->succ->uid );
               }
               */
               
               VG_(printf)("\n");
               cur = cur->admin;
            }
         }
         
         VG_(printf)("}\n");
         VG_(printf)("   %s","<<< END vcg >>>\n");
      }
      VG_(printf)("   %s %d\n","spin search depth:", HG_(clo_control_flow_graph) );
      VG_(printf)("   %s %lu out of %lu loops\n","spins found:", stats__spins_found, stats__loops_found);
      VG_(printf)("   %s","<<< BEGIN spin reads >>>\n");
      pp_map_spinreads( 6 );
      VG_(printf)("   %s","<<< END spin reads >>>\n");
      VG_(printf)("%s","<<< END hg_cf stats >>>\n");
   }
}
