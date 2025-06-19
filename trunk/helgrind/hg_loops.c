/*
 * hg_loops.c
 * 
 * This module detects additional happens-before relations
 *  from synchronisation loops which are inferred from analysis
 *  of guest code
 * 
 * (c) 2009-2010 Univercity of Karlsruhe, Germany
 */

#include "pub_tool_basics.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_hashtable.h"
//#include "pub_tool_replacemalloc.h"
#include "pub_tool_machine.h"
#include "pub_tool_options.h"
//#include "pub_tool_xarray.h"
#include "pub_tool_stacktrace.h"
#include "pub_tool_debuginfo.h"  /* VG_(get_data_description) */
#include "pub_tool_wordfm.h"
//#include "pub_tool_oset.h"
#include "pub_tool_seqmatch.h"   /* VG_(string_match) */
//#include "pub_tool_aspacemgr.h"  /* VG_(am_is_valid_for_client) */
//#include "pub_tool_vki.h"        /* VKI_PROT_READ */
#include "../coregrind/pub_core_translate.h"  /* VG_(translate_ahead) */
#include "../coregrind/pub_core_transtab.h"   /* VG_(discard_translations) */

#include "hg_basics.h"
#include "hg_wordset.h"
#include "hg_lock_n_thread.h"
#include "hg_loops.h"


// New dependency calculator
#include "hg_dependency.h"
// Used in lock-detector
#include "hg_interval.h"
// logging
#include "hg_logging.h"

#define MAX_BBLOCKS 3

/*----------------------------------------------------------------*/
/*--- Exported functions from hg_main                          ---*/
/*----------------------------------------------------------------*/

static ThreadLoopExtends* (*main_get_LoopExtends)( void ) = NULL;

/*----------------------------------------------------------------*/
/*--- Command line options                                     ---*/
/*----------------------------------------------------------------*/

Int HG_(clo_control_flow_graph) = 3;
Bool HG_(clo_detect_mutex) = True;

Bool hg_cfg__clo_verbose_control_flow_graph = False;
Bool hg_cfg__clo_show_control_flow_graph = False;
Bool hg_cfg__clo_ignore_pthread_spins = False;
Bool hg_cfg__clo_show_spin_reads = True;

UWord hg_cfg__clo_show_bb = 0;
UWord hg_cfg__clo_test_da = 0;

/*----------------------------------------------------------------*/
/*--- Primary data structures                                  ---*/
/*----------------------------------------------------------------*/

typedef struct _UnpredictableCall UC;

/*
 * This is a representation of a valgrind's superblock (piece of guest code)
 * It stores linkage between superblocks for loop detection
 */
struct _SuperBlock {
   // single linked list to be able to access all blocks in random order 
   struct _SuperBlock* admin;
   
   UInt   uid;
   Addr64 base;
   
   enum {
      HgL_Unknown,  /* freshly initialized superblock: don't know anything
                       except it's base address */  
      HgL_Analysed, /* we know superblock extents and branch pointers */
      HgL_Visited   /* spin read analysis was already done */
   } mark;

   /* ------- the following information is only available
    *    if marked HgL_Analysed or HgL_Visited  ------- */
   
   struct {
      Addr64 base;
      UShort len;
      Bool   is_func_call;
   } bblocks[MAX_BBLOCKS];
   UShort n_bblocks;
   
   enum {
      HgL_Next_Unknown, /* probably Return */
      HgL_Next_Direct,
      HgL_Next_Return,
      HgL_Next_Indirect
   } next_type;
   struct _SuperBlock* next;
   struct _SuperBlock* branches[MAX_BBLOCKS];

   /**
    * Used for synchronisation whithout loops (occurs for optimisation purpose):
    *   lock cmpxchg a,b,c       # try to acquire lock atomicaly outside of loop
    *   if(failed) {
    *      while(a) {} // loop only if lock currently held
    *   }
    *
    * @see detect_spining_reads_lookAhead
    */
   struct _AddrListNode * atomicLoads;


   /* ------- following information is updated during runtime ------- */
   
   Addr64 return_hint;
   Addr64 last_jump_iaddr;
   
   UC* uc;
   
   /* ------- not so relevant?! ------- */
   /*
   enum {
      HgL_Normal,
      HgL_Branch
   } blocktype;
   */
   struct {  // used to create dot-graphs
      struct _SuperBlock* nextReturn;
   } debug;
};
typedef struct _SuperBlock SB;

/*
 * Handles function calls which are unpredictable during instrumentation
 */

struct _UnpredictableCall {
   Addr64 instr_addr;
   Addr64 return_addr;
   Addr64 dest_addr;
   SB* invalidate_sb;
   UShort instr_len;
};

/*
 * ThreadLoopExtends extends the Thread structure with relevant information for
 * this module (Loop detection etc.).
 */
struct _ThreadLoopExtends {
   /* curSB points to the current Superblock */
   SB* curSB;
   
   /* uc != NULL if last Superblock ended with an unpredictable call */
   UC* uc;

   /* opaque (to us) data we hold on behalf of the library's user. */
   void* opaque;
};

/* Admin linked list of SuperBlocks */
static SB * admin_sblocks = NULL;

/* Map: Base Address --> SB */
WordFM* map_superblocks = NULL;

/* Map: Instruction Address of spin read --> Bool */
WordFM * map_spinreads = NULL;

/* Map: Instruction Address -> unpredictable function call structure */
WordFM * map_unpredictable_calls = NULL;

/* Static variable set to true when doing an ahead translation */
static Bool looking_ahead = False;

static Bool hg_loops__spin_reading = False;

/* Stores address corresponding to the end of a loop */
static WordFM * map_loop_ends = NULL;

/* Statistics */

static UWord stats__loops_found   = 0;
static UWord stats__spins_found   = 0;


/*----------------------------------------------------------------*/
/*--- Helper Functions                                         ---*/
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


/*----------------------------------------------------------*/
/*--- Simple helpers for debuging                        ---*/
/*--------------------------------------------------------- */

//#define NDEBUG

#ifndef NDEBUG
   #define INLINE
#else
   #define INLINE inline
#endif


#pragma GCC diagnostic ignored "-Wunused-function"
static
void pp_vge(VexGuestExtents * vge) {
   int i;
   for(i=0;i<vge->n_used;i++)
   {

      VG_(printf)(" block %d: 0x%llx + %d = 0x%llx \n", i, vge->base[i], (int)vge->len[i], vge->base[i]+vge->len[i]);
   }
}


/*----------------------------------------------------------------*/
/*--- Simple helpers for the data structures                   ---*/
/*----------------------------------------------------------------*/

static
ThreadLoopExtends* mk_ThrLoopExtends( SB* sb )
{
   ThreadLoopExtends* tle = NULL;
   
   tle = HG_(zalloc)( "mk.ThrLoopExtends.1", sizeof(ThreadLoopExtends) );
   tl_assert( tle );
   
   tle->curSB = sb;
   tle->uc = NULL;
   tle->opaque = NULL;
   
   return tle;
}

static
void update_vge_info( SB* sb, VexGuestExtents* vge ) {
   int i;
   tl_assert( sb->mark == HgL_Unknown );
   sb->n_bblocks = vge->n_used;
   for( i = 0; i < vge->n_used; i++ ) {
      sb->bblocks[i].base = vge->base[i];
      sb->bblocks[i].len  = vge->len[i];
      sb->bblocks[i].is_func_call = False;
   }
}

static UInt unique_id( void ) {
   static UInt last_uid = 0;
   last_uid++;
   return last_uid;
}

/*
 * Create a new SuperBlock
 */
static SB* mk_SB( Addr64 base, VexGuestExtents* vge ) {
   SB* sb = NULL;
   int i;
   
   sb = HG_(zalloc)( "mk.SB.1", sizeof(SB) );
   tl_assert( sb );
   
   sb->mark   = HgL_Unknown;
   sb->uid    = unique_id();
   sb->base   = base;

   for( i = 0; i < MAX_BBLOCKS; i++ ) {
      sb->bblocks[i].base = 0;
      sb->bblocks[i].len  = 0;
      sb->bblocks[i].is_func_call = False;
   }
   sb->n_bblocks = 0;
   if( vge )
      update_vge_info( sb, vge );
   
   sb->next_type = HgL_Next_Unknown;
   sb->next   = NULL;
   for( i = 0; i < MAX_BBLOCKS; i++ ) {
      sb->branches[i] = NULL;
   }
   
   sb->return_hint = 0;
   sb->last_jump_iaddr = 0;
   sb->uc = NULL;
   
   sb->admin  = admin_sblocks; 
   admin_sblocks = sb;
   
   return sb;
}

static
void pp_SB( SB *sb, int d ) {
   Bool    name_found = False;
   static char fnname[100];
   int i;
   
   name_found = VG_(get_fnname_w_offset) ( sb->base, fnname, sizeof(fnname) );
   
   space(d);
   switch( sb->mark ) {
   case HgL_Unknown:
      VG_(printf)("SB 0x%llx (%s) id:%d ;\n", sb->base, (name_found?fnname:""), sb->uid);
      return;
      break;
   case HgL_Analysed:
      VG_(printf)("SB 0x%llx (%s) id:%d {\n", sb->base, (name_found?fnname:""), sb->uid);
      break;
   case HgL_Visited:
      VG_(printf)("SB 0x%llx (%s) id:%d {\n", sb->base, (name_found?fnname:""), sb->uid);
      break;
   default:
      tl_assert(0);
   }
   
   for( i = 0; i < sb->n_bblocks; i++ ) {
      space(d);
      VG_(printf)(" block %d: 0x%llx + %d = 0x%llx , funcall:%s\n", i, sb->bblocks[i].base, (int)sb->bblocks[i].len, sb->bblocks[i].base+sb->bblocks[i].len, sb->bblocks[i].is_func_call?"yes":"no" );
   }
   
   if( sb->next_type == HgL_Next_Return ) {
      space(d);
      VG_(printf)("    next: Return (hint: 0x%llx)\n", sb->return_hint );
   } else
   if( sb->next_type == HgL_Next_Unknown ) {
      space(d);
      VG_(printf)("    next: Unknown \n");
      if( sb->next && d < 70 )
         pp_SB( sb->next, d+10 );
   } else
   if( sb->next ) {
      space(d);
      VG_(printf)("    next: %s \n", (sb->next_type==HgL_Next_Indirect)?"*":"");
      if( d < 70 )
         pp_SB( sb->next, d+10 );
   }
   
   for( i = 0; i < MAX_BBLOCKS; i++ ) {
      if( sb->branches[i] ) {
         space(d);
         VG_(printf)("  branch: \n");
         if( d < 70 )
            pp_SB( sb->branches[i], d+10 );
      }
   }
   
   space(d);
   VG_(printf)("}\n");
}

static
SB* lookup_SB( Addr base )
{
   Bool found;
   UWord keyW, valW;
   
   found = VG_(lookupFM)( map_superblocks, &keyW, &valW, (UWord)base );
   if( found )
      return (SB*)valW;
   else
      return NULL;
}

static
SB* lookup_or_create_SB( Addr base, VexGuestExtents* vge )
{
   SB* sb = lookup_SB( base );
   
   if( !sb ) {
      sb = mk_SB( base, vge );
      VG_(addToFM)( map_superblocks, (UWord)base, (UWord)sb );
   }
   
   return sb;
}

static
int get_sb_index( SB* sb, Addr addr )
{
   int i;
   for( i = 0; i < sb->n_bblocks; i++ )
   {
      Addr64 base = sb->bblocks[i].base;
      UShort len  = sb->bblocks[i].len;
      if( base <= addr && addr < base + len )
         return i;
   }
   // not found
   return -1;
}

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

static
UC* mk_UC( Addr64 instr_addr, UShort instr_len, SB* sb )
{
   UC* uc = NULL;
   
   uc = HG_(zalloc)( "mk.UC.1", sizeof(UC) );
   tl_assert( uc );
   
   uc->instr_addr = instr_addr;
   uc->instr_len = instr_len;
   uc->dest_addr = 0;
   uc->invalidate_sb = sb;
   
   return uc;
}

static
UC* lookup_UC( Addr64 instr_addr )
{
   Bool found;
   UWord keyW, valW;
   
   found = VG_(lookupFM)( map_unpredictable_calls, &keyW, &valW, (UWord)instr_addr );
   if( found )
      return (UC*)valW;
   else
      return NULL;
}

static
UC* lookup_or_create_UC( Addr64 instr_addr, UShort instr_len, SB* sb )
{
   UC* uc = NULL;

   uc = lookup_UC( instr_addr );
   
   if( !uc ) {
      uc = mk_UC( instr_addr, instr_len, sb );
      VG_(addToFM)( map_unpredictable_calls, (UWord)instr_addr, (UWord)uc );
   }
   
   return uc;
}

#if 0
static
void pp_UC(UC* uc, int d)
{
   space(d);
   VG_(printf)("UC @ 0x%llx -> 0x%llx ret 0x%llx ;\n",
         uc->instr_addr, uc->dest_addr, uc->return_addr );
}
#endif

/*----------------------------------------------------------------*/
/*--- AddrList double linked list                              ---*/
/*----------------------------------------------------------------*/

static
Addr64 ptrToAddr64( void * ptr )
{
   Addr64 a=((Int)ptr);
   return a;
}

typedef
   struct _AddrListNode {
      struct _AddrListNode *pred;
      struct _AddrListNode *succ;
      Addr64 key;
   }
   AddrList;

static
AddrList* addrlist_create( void )
{
   AddrList* al = NULL;
   
   al = HG_(zalloc)( "hg.loops.addrlist_create", sizeof(AddrList) );
   tl_assert( al );
   
   al->pred = al;
   al->succ = al;
   
   return al;
}

static
void addrlist_destroy( AddrList *al )
{
   AddrList* node = al->succ;
   while( node != al ) {
      AddrList* temp = node; // node != al
      tl_assert( node );
      node = node->succ;
      HG_(free)( temp ); // temp != al
   }
   HG_(free)( al ); // !!! Mem leak otherwise
}

/**
 * Clear list
 * Return al (can be used as a decorator).
 */
static
AddrList* addrlist_empty( AddrList* al )
{
   AddrList* node = al->succ;

   while( node != al ) {
      AddrList* temp = node; // node != al
      tl_assert( node );
      node = node->succ;
      HG_(free)( temp ); // temp != al
   }

   al->succ = al;
   al->pred = al;

   return al;
}

#if 0
static
void addrlist_debug_dump( AddrList *al, int depth )
{
   int i;
   AddrList* node = al;
   
   for( i = 0; i < depth; i++ ) {
      VG_(printf)("n %p < %p %p > key: 0x%llx \n", (void*)node, node->pred, node->succ, node->key );
      node = node->succ;
   }
   node = al;
   for( i = 0; i < depth; i++ ) {
      VG_(printf)("p %p < %p %p > key: 0x%llx \n", (void*)node, node->pred, node->succ, node->key );
      node = node->succ;
   }
}
#endif

static
void addrlist_dump( AddrList *al )
{
   AddrList* node = al->succ;
   VG_(printf)(" %p :", (void*)al );
   while( node != al ) {
      tl_assert( node );
      VG_(printf)(" 0x%llx ->", node->key );
      node = node->succ;
   }
   VG_(printf)(" []\n");
}

static
void addrlist_push_front( AddrList* al, Addr64 addr )
{
   AddrList* node = NULL;
   
   node = HG_(zalloc)( "hg.loops.addrlist_push_front", sizeof(AddrList) );
   tl_assert( node );
   
   node->key = addr;
   
   node->pred = al;
   node->succ = al->succ;
   
   al->succ->pred = node;
   al->succ = node;
}

static
void addrlist_push_back( AddrList* al, Addr64 addr )
{
   AddrList* node = NULL;
   
   node = HG_(zalloc)( "hg.loops.addrlist_push_back", sizeof(AddrList) );
   tl_assert( node );

   node->key = addr;
   
   node->pred = al->pred;
   node->succ = al;
   
   al->pred->succ = node;
   al->pred = node;
}

static
Bool addrlist_pop_front( AddrList* al, Addr64* addr )
{
   AddrList* node = al->succ;
   
   if( node == al )
      return False;
   
   *addr = node->key;
   
   al->succ = node->succ;
   node->succ->pred = al;
   HG_(free)( node );
   
   return True;
}

static
Bool addrlist_peek_front( AddrList* al, Addr64* addr )
{
   AddrList* node = al->succ;

   if( node == al )
      return False;

   *addr = node->key;

   return True;
}

static
Bool addrlist_pop_back( AddrList* al, Addr64* addr )
{
   AddrList* node = al->pred;
   
   if( node == al )
      return False;

   *addr = node->key;
   
   al->pred = node->pred;
   node->pred->succ = al;
   HG_(free)( node );

   return True;
}

static
AddrList* addrlist_dopy( AddrList* al )
{
   AddrList* copy = addrlist_create();
   AddrList* node = al->succ;
   while( node != al ) {
      tl_assert( node );
      addrlist_push_back( copy, node->key );
      node = node->succ;
   }
   return copy;
}

static
void addrlist_push_all_back( AddrList* src, AddrList* dst, Addr64 replace_first )
{
   AddrList* node = src->succ;
   while( node != src ) {
      tl_assert( node );
      if( replace_first ) {
         addrlist_push_back( dst, replace_first );
         replace_first = 0;
      } else {
         addrlist_push_back( dst, node->key );
      }
      node = node->succ;
   }
}

static
Bool in_addrlist( AddrList* al, Addr a )
{
   AddrList* node = al->succ;
   while( node != al ) {
      tl_assert( node );
      if( node->key == a )
         return True;
      node = node->succ;
   }
   
   return False;
}


/*----------------------------------------------------------------*/
/*--- Data dependency helpers                                  ---*/
/*----------------------------------------------------------------*/

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
   
   tl_assert( var->dependencies );
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

static
Variable * varmap_lookup_or_create( VarType type, VarOffset index ) {
   return varmap_lookup_WRK( type, index, True );
}

static
Variable * varmap_lookup( VarType type, VarOffset index ) {
   return varmap_lookup_WRK( type, index, False );
}

static
void add_dependency ( Variable* var, VarType type, VarOffset index ) {
   Variable dep;
   
   dep.type = type;
   dep.index = index;
   
   tl_assert( var->dependencies );
   if( !VG_(lookupFM)( var->dependencies, NULL, NULL, (UWord)&dep ) ) {
      Variable *new_dep = varmap_lookup_or_create( type, index );
      VG_(addToFM)( var->dependencies, (UWord)new_dep, 0 );
   }
}

#if 0
static
void reset_dependencies ( Variable* var ) {
   VG_(deleteFM)( var->dependencies, NULL, NULL );
   var->dependencies = VG_(newFM)(HG_(zalloc), "reset_dependencies.1", HG_(free), cmp_Variables);
   tl_assert( var->dependencies );
}
#endif

static
void swap_dependencies( Variable * v1, Variable * v2 )
{
   WordFM * temp;
   temp = v1->dependencies;
   v1->dependencies = v2->dependencies;
   v2->dependencies = temp;
}

static
void copy_dependencies( Variable * dst, Variable * src ) {
   Variable * dep;
   VG_(initIterFM)( src->dependencies );
   while( VG_(nextIterFM)( src->dependencies, (UWord*)&dep, NULL ) ) {
      VG_(addToFM)( dst->dependencies, (UWord)dep, 0 );
   }
   VG_(doneIterFM)( src->dependencies );
}

static
Bool has_dependency( Variable * var, VarType type, VarOffset index ) {
   Variable dep;
   
   dep.type = type;
   dep.index = index;
   
   tl_assert( var->dependencies );
   if( VG_(lookupFM)( var->dependencies, NULL, NULL, (UWord)&dep ) )
      return True;
   else
      return False;
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
      VG_(printf)(" == ");
   
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
      case Iex_GetI: {
         /* unhandled */
      } break;
      case Iex_RdTmp: {
         Variable * tmp = varmap_lookup( HG_CFG_VAR_TEMP, e->Iex.RdTmp.tmp );
         if( tmp ) {
            Variable * dep;
            VG_(initIterFM)( tmp->dependencies );
            while( VG_(nextIterFM)( tmp->dependencies, (UWord*)&dep, NULL ) ) {
               if( var->type == HG_CFG_VAR_CONDITION &&
                   dep->type == HG_CFG_VAR_REGISTER ) {
                  Variable *dep2;
                  VG_(initIterFM)( dep->dependencies );
                  while( VG_(nextIterFM)( dep->dependencies, (UWord*)&dep2, NULL ) ) {
                     VG_(addToFM)( var->dependencies, (UWord)dep2, 0 );
                  }
                  VG_(doneIterFM)( dep->dependencies );
               }
               else {
                  VG_(addToFM)( var->dependencies, (UWord)dep, 0 );
               }
            }
            VG_(doneIterFM)( tmp->dependencies );
            if( tmp->modified )
               var->modified = True;
         }
      } break;
      case Iex_Mux0X: {
         var->modified = True;
         detect_dependencies( var, e->Iex.Mux0X.cond );
         detect_dependencies( var, e->Iex.Mux0X.expr0 );
         detect_dependencies( var, e->Iex.Mux0X.exprX );
      } break;
      case Iex_Qop: {
         var->modified = True;
         detect_dependencies( var, e->Iex.Qop.arg1 );
         detect_dependencies( var, e->Iex.Qop.arg2 );
         detect_dependencies( var, e->Iex.Qop.arg3 );
         detect_dependencies( var, e->Iex.Qop.arg4 );
      } break;
      case Iex_Triop: {
         var->modified = True;
         detect_dependencies( var, e->Iex.Triop.arg1 );
         detect_dependencies( var, e->Iex.Triop.arg2 );
         detect_dependencies( var, e->Iex.Triop.arg3 );
      } break;
      case Iex_Binop: {
         switch( e->Iex.Binop.op ) {
         case Iop_Add32:
         case Iop_Sub32:
         case Iop_Mul32:
         case Iop_Add64:
         case Iop_Sub64:
         case Iop_Mul64:
         { // likely to be an address calculation
            var->modified = True;
         } break;
         default:
            break;
         }
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
                  detect_dependencies( dep, data );
               }
               VG_(doneIterFM)( tmp->dependencies );
            } else if ( addr->tag == Iex_Const ) {
               Variable * var = varmap_lookup_or_create( HG_CFG_VAR_ADDRESS, value_of(addr) );
               detect_dependencies( var, data );
            }
         } break;
            
         case Ist_Put: {
            IRExpr * data = st->Ist.Put.data;
            Variable * var_reg = varmap_lookup_or_create( HG_CFG_VAR_REGISTER, st->Ist.Put.offset );
            Variable * var = mk_Variable( HG_CFG_VAR_REGISTER, 0 );
            detect_dependencies( var, data );
            if( !has_dependency( var, HG_CFG_VAR_REGISTER, st->Ist.Put.offset ) )
               swap_dependencies( var_reg, var );
            else
               copy_dependencies( var_reg, var );
            del_Variable( (UWord)var );
         } break;
         
         case Ist_WrTmp: {
            IRExpr * data = st->Ist.WrTmp.data;
            Variable * var_temp = varmap_lookup_or_create( HG_CFG_VAR_TEMP, st->Ist.WrTmp.tmp );
            if( data->tag == Iex_Load ) {
               IRExpr * addr = data->Iex.Load.addr;
               Variable * var_load = varmap_lookup_or_create( HG_CFG_VAR_LOAD, iaddr );
               
               add_dependency( var_temp, HG_CFG_VAR_LOAD, iaddr );
               detect_dependencies( var_load, addr );
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
/*--- BEGIN Schmaltz dot                        ---*/
/*----------------------------------------------------------------*/

typedef struct {
   WordFM * sBs;
} Dot;

typedef struct {
   Char spinIdx;
} _Dot_el;

typedef struct {
   UInt gen;
   //AddrList * dests;
   WordFM * dests;
} _Dot_retAddresses;
static struct {
   UInt gen;
   WordFM * returnEdges;
   Int dotIdx;

   Int dotStructsCnt;
} _dot_static;


/**
 * Macro to add return-edges.
 *  It does so only if some 'Dot' was allocated. If not it was useless to build return-Edges
 */
#define SB_RET_EDGE(sbFrom,sbTo) do { if(_dot_static.dotStructsCnt>0){ dot_addRetEdge(sbFrom,sbTo);  (sbFrom)->debug.nextReturn = (sbTo); } } while(0)

/**
 * Add some return Edge (from func-call) in graph, from 'from' to 'to'
 *  - return edges cannot be found by looking the code
 */
static
void dot_addRetEdge( SB* from, SB* to )
{
   _Dot_retAddresses * retAddr;
   if(!_dot_static.returnEdges) {
      _dot_static.returnEdges=VG_(newFM) ( HG_(zalloc),
                         MALLOC_CC(dot_addRetEdge+_dot_static.returnEdges),
                         HG_(free),
                         NULL );
   }
   if(VG_(lookupFM)(_dot_static.returnEdges,NULL,(UWord*)&retAddr,(UWord)from)) {
   } else {
      retAddr=HG_(zalloc)(MALLOC_CC(dot_addRetEdge+retAddr),sizeof(*retAddr));
      retAddr->dests=VG_(newFM) ( HG_(zalloc),
            MALLOC_CC(dot_addRetEdge+retAddr->dests),
            HG_(free),
            NULL );
      VG_(addToFM)(_dot_static.returnEdges,(UWord)from,(UWord)retAddr);
   }
   //addrlist_push_back(retAddr->dests,to->base);
   VG_(addToFM)(retAddr->dests,(UWord)to,(UWord)0);
}

/**
 * Get the list of return-Edges out of 'from'
 */
static
WordFM * dot_getRetEdges( SB* from )
{
   _Dot_retAddresses * retAddr;
   if(!_dot_static.returnEdges) {
      return NULL;
   }

   if(VG_(lookupFM)(_dot_static.returnEdges,NULL,(UWord*)&retAddr,(UWord)from)) {
      return retAddr->dests;
   }
   return NULL;
}

/**
 * return true if their is a return Edge going from 'from' to 'to'
 */
static
Bool dot_retEdgeExists( SB* from, SB* to )
{
   WordFM * retEdges=dot_getRetEdges(from);
   return (retEdges&&VG_(lookupFM)(retEdges,NULL,NULL,(UWord)to));
}

static
Dot * dot_new( void )
{
   Dot * dot=HG_(zalloc)(MALLOC_CC(dot_new),sizeof(*dot));
   _dot_static.dotStructsCnt++;
   dot->sBs=VG_(newFM) ( HG_(zalloc),
                   MALLOC_CC(dot_new+dot->sBs),
                   HG_(free),
                   NULL );

   tl_assert(sizeof(_Dot_el)<=sizeof(UWord));
   return dot;
}

static
void dot_free( Dot* dot )
{
   tl_assert(dot->sBs);
   VG_(deleteFM)(dot->sBs,NULL,NULL);
   dot->sBs=NULL;
   HG_(free)(dot);
   _dot_static.dotStructsCnt--;
   if(_dot_static.dotStructsCnt==0) {
      _Dot_retAddresses * retAddr;
      SB* from;
      VG_(initIterFM)(_dot_static.returnEdges);
      while(VG_(nextIterFM) ( _dot_static.returnEdges,
            (UWord*)&from, (UWord*)&retAddr )) {
         VG_(deleteFM)(retAddr->dests,NULL,NULL);
         HG_(free)(retAddr);
      }
      VG_(doneIterFM) ( _dot_static.returnEdges );
   }
}

static
void dot_addSB(Dot* dot, SB* sb)
{
   static _Dot_el def_el={-1};
   tl_assert(dot->sBs);
   VG_(addToFM) ( dot->sBs, (UWord)sb, *((UWord*)&def_el) );


   /*
   for( i = 0; i < sb->n_bblocks; i++ ) {
      if( sb->bblocks[i].is_func_call ) {
         Addr64 retAddr=sb->bblocks[i].base + sb->bblocks[i].len;
      }
   }*/
}

static
const Char * _dot_sbKey(Dot* dot, SB* sb)
{
   static Char buf[3][200];
   static Int i=0,tmp;
   Addr64 base;
   i=(i+1)%3;

   if(!sb) {
      static UChar nullCnt=0;
      VG_(snprintf)(buf[i],sizeof(buf[0]),"_NULL_SB_%d_",(Int)nullCnt++);
   } else {
      base=sb->base;
      tmp=VG_(snprintf)(buf[i],sizeof(buf[0]),"sb_%llx",base);
   }
   return buf[i];
}

static
const Char * _dot_sbLabel(Dot* dot, SB* sb)
{
   static Char buf[200];
   static Int tmp;
   Addr64 base;
   base=sb->base;

   tmp=VG_(snprintf)(buf,sizeof(buf),"SB: 0x%llx \\n",base);
   {
      if(!VG_(get_fnname_w_offset) ( base, &(buf[tmp]), sizeof(buf)-tmp )) {
         buf[tmp-2]=0;
      }
   }
   return buf;
}

static
void dot_addSpin(Dot*dot,Addr64 addr)
{
   SB * sb;
   VG_(initIterFM)( dot->sBs );
   while(VG_(nextIterFM)(dot->sBs, (UWord*)&sb, NULL )) {
      Int idx=get_sb_index(sb,addr);
      _Dot_el el;
      if(idx>=0) {
         el.spinIdx=idx;
         VG_(addToFM)(dot->sBs,(UWord)sb,*((UWord*)&el));
      }
   }
   VG_(doneIterFM)( dot->sBs );
}

static Bool sb_intersects( SB* sb1, SB* sb2 );
static
void dot_pp(Dot*dot)
{
   const static Char * colors[]={
                      "red","green","blue","yellow","magenta",
                      "cyan","burlywood", NULL};
   const static Char * edgeColors[]={
                      "black","red","green","blue","magenta",
                      NULL};
   Int edgeColId=0;
   //,constraint=false
#define __DOT_PP_EDGE ""
#define __DOT_PP_MAKE_EDGE(psb1,psb2,label) do{\
		typeof(psb1) _sb1=(psb1), _sb2=(psb2);\
      Bool revert=(_sb2&&_sb1)&&_sb2->base<_sb1->base;\
      if(revert) { typeof(_sb1) tsb=_sb1; _sb1=_sb2; _sb2=tsb; }\
      VG_(printf)("\t%s -> %s [label=\"",_dot_sbKey(dot,_sb1),_dot_sbKey(dot,_sb2));\
      VG_(printf) label ;\
      VG_(printf)("\"");\
      if(revert) VG_(printf)(",dir=back");\
      VG_(printf)(",color=\"%s\",fontcolor=\"%s\"",edgeColors[edgeColId],edgeColors[edgeColId]);\
      edgeColId++;if(!edgeColors[edgeColId])edgeColId=0;\
      VG_(printf)("]\n");\
}while(0)
   //      if(0&&revert) VG_(printf)(",constraint=false");
   //      if(0&&revert) VG_(printf)("\t%s -> %s [color=\"transparent\",weight=0.0] // ordering\n",_dot_sbKey(dot,_sb2),_dot_sbKey(dot,_sb1));
   //decorate
   SB * sb;
   _Dot_el el,*pel=&el;
   Int i,j, clusterCnt=0, numNodes;


   numNodes=VG_(sizeFM)( dot->sBs );

   VG_(printf)("digraph sb_graph {\n");
   {
      SB * superBlocks[numNodes];
      i=0;
      VG_(initIterFM)( dot->sBs );
      while(VG_(nextIterFM)(dot->sBs, (UWord*)&sb, (UWord*)&el )) {
         VG_(printf)("\t%s [label=\"%s\"]\n",_dot_sbKey(dot,sb),_dot_sbLabel(dot,sb));
         if(pel->spinIdx>=0) {
            VG_(printf)("\t%s [shape = \"rect\"] // SPIN %d\n",_dot_sbKey(dot,sb),pel->spinIdx);
         }
         superBlocks[i]=sb;i++;
      }
      VG_(doneIterFM)( dot->sBs );

      VG_(printf)("// Edges \n");

      //VG_(initIterFM)( dot->sBs );
      //while(VG_(nextIterFM)(dot->sBs, (UWord*)&sb, NULL )) {
      for(j=0;j<numNodes;j++) {
         sb=superBlocks[j];
         for( i = 0; i < MAX_BBLOCKS; i++ ) {
            if(sb->branches[i]) {
               __DOT_PP_MAKE_EDGE(sb,sb->branches[i],("branch[%d]%s",i,(sb->bblocks[i].is_func_call?" funcCall":"")));
            }
         }
         if(sb->next) {
            __DOT_PP_MAKE_EDGE(sb,sb->next,("next"));
         } else if((0==0xede)&&sb->next_type==HgL_Next_Return) {
            if(sb->debug.nextReturn||!(sb->return_hint)) {
               __DOT_PP_MAKE_EDGE(sb,sb->debug.nextReturn,("return"));
               if((sb->debug.nextReturn&&sb->return_hint)&&!(sb->debug.nextReturn->base==sb->return_hint)) {
                  SB*rSb=lookup_or_create_SB(sb->return_hint,NULL);
                  __DOT_PP_MAKE_EDGE(sb,rSb,("DUP:ret_hint"));
               }
            } else {
               SB*rSb=lookup_or_create_SB(sb->return_hint,NULL);
               __DOT_PP_MAKE_EDGE(sb,rSb,("return:hint"));
            }
         } else if(0==0xede){
            if(sb->return_hint) {
               SB*rSb=lookup_or_create_SB(sb->return_hint,NULL);
               __DOT_PP_MAKE_EDGE(sb,rSb,("next_unknown \\n return_hint"));
               if((sb->debug.nextReturn&&sb->return_hint)&&!(sb->debug.nextReturn->base==sb->return_hint)) {
                  __DOT_PP_MAKE_EDGE(sb,sb->debug.nextReturn,("DUP:next_unknown:ret"));
               }
            } else {
               __DOT_PP_MAKE_EDGE(sb,sb->debug.nextReturn,("next \\n unknown"));
            }
         }

         {
            /*
            AddrList * retEdges = dot_getRetEdges( sb );
            Addr64 retBase;
            if(retEdges) {
               retEdges=addrlist_dopy(retEdges);
               while(addrlist_pop_back(retEdges,&retBase))
               {
                  SB*rSb=lookup_or_create_SB(retBase,NULL);
                  __DOT_PP_MAKE_EDGE(sb,rSb,("ret_edge"));
               }
               addrlist_destroy(retEdges);
            }*/
            WordFM * retEdges = dot_getRetEdges( sb );
            SB* retDst;
            if(retEdges) {
               VG_(initIterFM)( retEdges );
               while(VG_(nextIterFM)(retEdges,(UWord*)&retDst,NULL )) {
                  if(VG_(lookupFM)(dot->sBs,NULL,NULL,(UWord)retDst)) {
                     // Only draw return edges if the node was in the graph
                     __DOT_PP_MAKE_EDGE(sb,retDst,("RETURN"));
                  }
               }
               VG_(doneIterFM)( retEdges );
            }
         }
      }
      {
         Int curColor=0;
         /*
          * Nodes who have some common code (intersection)
          * were now grouped into clusters
          */
         SB * sb1, *sb2;
         Bool wasDone[numNodes];
         Bool inCluster=False;
         Int iCluster[numNodes];
         Int iClusterIdx=0;
         Int cur;
         VG_(memset)(wasDone,0,numNodes*sizeof(Bool));
         for(j=0;j<numNodes;j++) {
            if(wasDone[j]) continue;
            //sb1=superBlocks[j];
            wasDone[j]=True;

            iCluster[iClusterIdx]=j+1;iClusterIdx++;
            while(iClusterIdx>0) {
               iClusterIdx--;
               cur=iCluster[iClusterIdx]-1;
               sb1=superBlocks[cur];

               for(i=cur+1;i<numNodes;i++) {
                  sb2=superBlocks[i];
                  if(sb_intersects(sb1, sb2)) {
                     if(!inCluster) {
                        if(0){
                           VG_(printf)("subgraph cluster_s%d {\n",clusterCnt++);
                           VG_(printf)("\t%s; ",_dot_sbKey(dot,sb1));
                        }
                        if(1) VG_(printf)("\t%s [style=\"filled\",fillcolor=\"%s\"]; ",_dot_sbKey(dot,sb1),colors[curColor]);
                        inCluster=True;
                     }
                     if(!wasDone[i]) {
                        wasDone[i]=True;
                        if(0) VG_(printf)("%s; ",_dot_sbKey(dot,sb2));
                        if(1) VG_(printf)("\t%s [style=\"filled\",fillcolor=\"%s\"]; ",_dot_sbKey(dot,sb2),colors[curColor]);
                        iCluster[iClusterIdx]=i+1;iClusterIdx++;
                     }
                     if(0) VG_(printf)("%s->%s [dir=both, color=\"#222222\"];",_dot_sbKey(dot,sb1),_dot_sbKey(dot,sb2));
                  }
               }
            }

            if(inCluster) {
               if(0) VG_(printf)("\n}\n");
               if(1) {
                  VG_(printf)("\n");
                  curColor++;
                  if(!colors[curColor]) curColor=0;
               }
               inCluster=False;
            }
         }
      }
      //VG_(doneIterFM)( dot->sBs );
   }
   VG_(printf)("}\n");
}




/*----------------------------------------------------------------*/
/*--- BEGIN Schmaltz dependency                        ---*/
/*----------------------------------------------------------------*/

static
void analyse_branches ( SB* sb,
                        IRSB* bbIn,
                        VgCallbackClosure* closure,
                        VexGuestLayout* layout,
                        VexGuestExtents* vge );
// ----------
#define __LoopCode_USEADDR 1
#define __LoopCode_INIT_ONCE 0
#include "pub_tool_xarray.h"
/*
typedef struct {
   Addr64 minAddr,maxAddr;
   UWord nInstr;
} LoopCode;

typedef struct {
   XArray * blocks; // array of LoopCode
   Bool isSorted;
   Bool isIncorrect;

   WordFM * addresses;
} Loop;*/

//#include "hg_interval_Loop.c"

// ----------- SCHMALTZ
/*
 * TODO : put this code somewhere else ?
 * map_spinreads maps to some additional info about the spin
 */


static WordFM * spin_properties;

SpinProperty * HG_(lookupSpinProperty)(IntervalUnion * codeBlock)
{
   SpinProperty*sp;
   if(VG_(lookupFM)(spin_properties,NULL,(UWord*)&sp,(UWord)codeBlock)) {
      return sp;
   }
   return NULL;
}

static
SpinProperty * getSpinProperty(IntervalUnion * codeBlock, Bool*found)
{
   SpinProperty*sp;
   if(!codeBlock) {
      sp=HG_(zalloc)(MALLOC_CC(SpinProperty+sp),sizeof(*sp));
      sp->sp_magic=SP_PROP_MAGIC;
      return sp;
   }
   if(!spin_properties) {
      spin_properties=VG_(newFM)( HG_(zalloc),
            MALLOC_CC(spin_properties),
            HG_(free),
            HG_(ivUnionCompare) );
   }
   if(VG_(lookupFM)(spin_properties,NULL,(UWord*)&sp,(UWord)codeBlock)) {
      if(found) (*found)=True;
      return sp;
   }
   sp=HG_(zalloc)(MALLOC_CC(SpinProperty+sp),sizeof(*sp));
   sp->sp_magic=SP_PROP_MAGIC;

   assert(HG_(ivUnion_isSane)(codeBlock));
   sp->codeBlock=codeBlock;

   sp->spinVariables=VG_(newFM)( HG_(zalloc),
         MALLOC_CC(sp->spinVariables),
         HG_(free),
         NULL );

   if(0){ // merge blocks that have some intersection
      SpinProperty*spTmp;
      IntervalUnion * ivuTmp;
      AddrList * toFree=addrlist_create();
      tl_assert( HG_(ivUnion_isSane)(codeBlock) );
      VG_(initIterFM)( spin_properties );
      while( VG_(nextIterFM)( spin_properties, NULL, (UWord*)&spTmp ) ) {
         IntervalUnion * res;
         if(sp==spTmp) continue;
         res=HG_(ivUnionIntersect)(sp->codeBlock,spTmp->codeBlock);
         if(res) {
            VG_(printf)(__AT__":HG_(ivUnionIntersect) ");
            HG_(ivUnionDump_ext)(sp->codeBlock,True);
            HG_(ivUnionDump_ext)(spTmp->codeBlock,True);
            VG_(printf)("\n");

            HG_(ivUnionAddIvUnion)(sp->codeBlock,res);
            //HG_(freeIvUnion)(spTmp->codeBlock);
            //spTmp->codeBlock=sp->codeBlock;
            HG_(freeIvUnion)(res);
            addrlist_push_back(toFree,ptrToAddr64(spTmp->codeBlock));
         }
      }
      VG_(doneIterFM)( spin_properties );

      while(addrlist_pop_back(toFree,(Addr64*)&ivuTmp)) {
         VG_(delFromFM)(spin_properties,NULL,NULL,(UWord)ivuTmp);
         HG_(freeIvUnion)(ivuTmp);
         // TODO : leak : spTmp
      }

      tl_assert(sp->codeBlock==codeBlock);
      tl_assert( HG_(ivUnion_isSane)(codeBlock) );
   } // if(0)
   //VG_(printf)("getSpinProperty:codeBlock=%p\n",codeBlock);
   //VG_(addToFM) ( spin_properties, (UWord)codeBlock,(UWord)sp );
   if(found) (*found)=False;
   return sp;
}

static
void ppSpinProperty(SpinProperty * sp)
{
   //VG_(printf)("Sp=%p\n",sp);
   HG_(ivUnionDump)(sp->codeBlock);
}

/**
 * TODO : hg_loops_lastSpinProperty is currently shared between threads.
 *    Is it really a good idea ?
 */
static WordFM * hg_loops_lastSpinProperty; // ThreadId => SpinProperty
//SpinProperty * hg_loops_lastSpinProperty; // ThreadId tid = VG_(get_running_tid)();

SpinProperty * HG_(get_lastSpinProperty)( void )
{
   ThreadId tid = VG_(get_running_tid)();
   SpinProperty * res=NULL;
   VG_(lookupFM)(hg_loops_lastSpinProperty,NULL,(UWord*)&res,tid);
   return res;
}

static
void HG_(set_lastSpinProperty)( SpinProperty * sp )
{
   ThreadId tid = VG_(get_running_tid)();
   if(sp) SP_MAGIC_ASSERT(sp);
#ifndef NDEBUG
   if(sp&&(sp->codeBlock)&&!(HG_(ivUnion_isSane)(sp->codeBlock))) {
      VG_(printf)("Assert failed at : "__AT__"\n");
      HG_(ivUnionDump)(sp->codeBlock);
      tl_assert(0);
   }
   if(PRINT_LOGGING_HG_LOOPS&& sp&&sp->codeBlock) {
      VG_(printf)("HG_(set_lastSpinProperty)::");
      HG_(ivUnionDump_ext)(sp->codeBlock,True);
   }
#endif

   VG_(addToFM)(hg_loops_lastSpinProperty,tid,(UWord)sp);
}


// ---------- END SCHMALTZ

//#define Loop IntervalUnion

static
void loop_addInstruction(IntervalUnion * loop, Addr64 addr, Int len)
{
   //Interval * iv=HG_(zalloc)(
   HG_(ivUnionAddInterval)(loop,(HWord)addr,(HWord)(addr+len),NULL);
   if(PRINT_SPIN_PROPERTY) VG_(printf)("+%llx,%d\t",addr,len);
   if(PRINT_SPIN_PROPERTY) HG_(ivUnionDump)(loop);
   //tl_assert(0);
}

#define loop_init(__loop) do{ *__loop=*HG_(newIvUnion)(__AT__); }while(0)

/*
static
Bool loop_contains(IntervalUnion * loop, Addr64 addr)
{
   return HG_(ivUnionContains)(loop,(HWord)(addr))!=NULL;
}
*/

// ----------------------------

static
void analyse_atomicInstructions(SB* sb, IRSB * bbIn)
{

   Int i=0;
   Bool x86busLocked=False, isSnoopedStore=False;
   Addr64 addr=0;
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
         case Ist_IMark: {
            addr=st->Ist.IMark.addr;
         } break;
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
         case Ist_WrTmp: {
            IRExpr * data=st->Ist.WrTmp.data;
            if((isSnoopedStore||x86busLocked)&&data->tag==Iex_Load) {
               if(!sb->atomicLoads) {
                  sb->atomicLoads=addrlist_create();
               }
               if(!addr) {
                  ppIRSB(bbIn);
                  tl_assert(addr);
               }
               addrlist_push_back(sb->atomicLoads,addr);
            }
         } break;

         default: break;
      }
   }

}

// -----------------------------
static Bool sb_matchFunc(const Char* func, SB*sb)
{
   int i;
   static Char buf[200];

   for( i = 0; i < sb->n_bblocks; i++ )
   {
      Addr64 base = sb->bblocks[i].base;
      if(VG_(get_fnname_w_offset) ( base, buf, sizeof(buf) ))
      {
         if(VG_(string_match)(func, buf)) return True;
      }
   }
   return False;
}
static Bool sb_intersectRange(SB*sb,Addr64 addrMin,Addr64 addrMax)
{
   int i;
   for( i = 0; i < sb->n_bblocks; i++ )
   {
      Addr64 base = sb->bblocks[i].base;UShort len  = sb->bblocks[i].len;
      if( (base >= addrMin && base <= addrMax) || ((base+len-1)>=addrMin &&(base+len-1)<=addrMax) )
         return True;
   }
   // not found
   return False;
}
// -----------------------------

static struct {
   IRSB* concatBB;
   Int callDepth;
   Addr64 minAddr,maxAddr;
   struct {
      //Loop loop;
      IntervalUnion * loop, *loopToFree;
      Bool useLoopBlocks;
   }loopBlocks;
   AddrList* loop;
   Addr64 firstBlock;

   Dot * dot;
   /*useless, I think*/
   IRSB *instrumented_bbIn, * instrumented_bbOut;
} depconf;

static IRSB* 
buildConcatBB ( VgCallbackClosure* closure,
                                  IRSB* bbIn,
                                  VexGuestLayout* layout,
                                  VexGuestExtents* vge,
                                  IRType gWordTy, IRType hWordTy )
{
   IRSB*   bbOut;
   Addr64  nextSuperBlock=0xdead;
   Addr64 skippableAddr=0;

   /* Output of this func is not stored anywhere, so we try to create a
      minimal but consistent basic block. */
   bbOut           = emptyIRSB();
   bbOut->tyenv    = emptyIRTypeEnv();
   bbOut->next     = mkIRExpr_HWord(0x1010);
   bbOut->jumpkind = Ijk_Boring;

#if !(COMMITABLE) && 1
   if(!depconf.loopBlocks.useLoopBlocks && __LoopCode_INIT_ONCE) {
      VG_(printf)(__AT__" init loopBlocks\n");
      //loop_init(&depconf.loopBlocks.loop);
      depconf.loopBlocks.loop = HG_(newIvUnion)("Uniq LoopBlocks");
   }
   depconf.loopBlocks.useLoopBlocks=True;


#endif

   tl_assert(bbIn!=depconf.instrumented_bbIn);
   tl_assert(bbIn!=depconf.instrumented_bbOut);

   if(!addrlist_peek_front( depconf.loop, &nextSuperBlock )) {
      nextSuperBlock=depconf.firstBlock;
   }

   if(PRINT_Concatenation) {
      ppIRSB(bbIn);
      VG_(printf)("Next super block = %llx\n",nextSuperBlock);
   }

   if(1) {
      Int i;
      Bool willSkip=False;
      Addr64 addrMin=0,addrMax=0;
      for(i=0;i<bbIn->stmts_used;i++) {
         IRStmt * st=bbIn->stmts[i];
         if(st->tag==Ist_IMark) {
            if(willSkip) {
               skippableAddr=st->Ist.IMark.addr;
               if(PRINT_Concatenation) {
                  VG_(printf)("willSkip=True : going to @%llx\n",nextSuperBlock);
                  ppIRStmt(st);
               }
               break;
            }

            if(addrMin==0) {
               addrMin=st->Ist.IMark.addr;
            } else if(addrMin>st->Ist.IMark.addr) {
               addrMin=st->Ist.IMark.addr;
            }
            if(addrMax==0) {
               addrMax=st->Ist.IMark.addr;
            } else if(addrMax<st->Ist.IMark.addr) {
               addrMax=st->Ist.IMark.addr;
            }
         }
         if(st->tag==Ist_Exit) {
            Addr dest_addr = value_of(IRExpr_Const(st->Ist.Exit.dst));

            if(dest_addr==nextSuperBlock) {
               willSkip=True;
            }
         }
      }
      if(!(depconf.maxAddr)||depconf.maxAddr<addrMax) {
         depconf.maxAddr=addrMax;
      }
      if(!(depconf.minAddr)||depconf.minAddr>addrMin) {
         depconf.minAddr=addrMin;
      }
      if(!(COMMITABLE)){ // Just for assert
         SB* sb=lookup_or_create_SB( vge->base[0], vge );

         if( sb->mark == HgL_Unknown ) {
            /**
             * TODO : this wasn't needed anymore
             */
            update_vge_info( sb, vge );
            analyse_branches( sb, bbIn, closure, layout, vge );
            sb->mark = HgL_Analysed;
         }
         if(!(willSkip)){
            if(sb->next) {
               if(!dot_retEdgeExists(sb,lookup_SB(nextSuperBlock))) // trick : some edges were unknown. dot knows them
               {
                  VG_(printf)("dotIdx=%d : %p->%p %p !\n",_dot_static.dotIdx,(void*)(Int)sb->base,(void*)(Int)sb->next->base,(void*)nextSuperBlock);
                  soft_assert(sb->next->base==nextSuperBlock);
               }
            } else {
               soft_assert(sb->next_type == HgL_Next_Return);
            }
         }
      }
   }

   HG_(concat_irsb)(bbIn,depconf.concatBB,skippableAddr,vge,closure,layout);

   return bbOut;
}

static
Bool addSBtoLoop(IntervalUnion * ivu, Addr base, Int depth)
{
   SB * sb;
   Int i,j=0;
   Bool res=False;
   Bool changed=False;

   if(depth<=0) return False;
   sb=lookup_SB(base);
   //sb=lookup_or_create_SB(base,NULL);
   if(sb==NULL) {
      if(0)VG_(printf)("Enter:addSBtoLoop:noSB for #%lx\n",base);
      return False;
   }
   //VG_(printf)("Enter:addSBtoLoop\n");

   for(i=0;i<sb->n_bblocks;i++)
   {
      Addr64 baseL = sb->bblocks[i].base;
      UShort len  = sb->bblocks[i].len;
      Bool   isFn = sb->bblocks[i].is_func_call;

      if(HG_(ivUnionContains)(ivu,baseL)) {
         for(;j<=i;j++) {
            if(HG_(ivUnionAddInterval)(ivu,sb->bblocks[j].base,sb->bblocks[j].base+sb->bblocks[j].len,NULL)) {
               changed=True;
               if(0)VG_(printf)(__AT__":addSBtoLoop - something added\n");//something added!
            } else {
               //VG_(printf)(__AT__":addSBtoLoop - nothing added\n");
            }
         }
         res=True;
      }
      if(sb->branches[i]) {
         //addrlist_push_back(addr_list,(Addr64)sb->branches[i]);
         if(addSBtoLoop( ivu, sb->branches[i]->base , depth-1 )) {
            res=True;
            for(;j<=i;j++) {
               if(HG_(ivUnionAddInterval)(ivu,sb->bblocks[j].base,sb->bblocks[j].base+sb->bblocks[j].len,NULL)) {
                  changed=True;
                  if(0)VG_(printf)(__AT__":addSBtoLoop - something added\n");//something added!
               } else {
                  //VG_(printf)(__AT__":addSBtoLoop - nothing added\n");
               }
            }
         }
      }

      if( isFn )
      {
         if(addSBtoLoop(ivu,baseL+len,depth-1)) {
            res=True;
            for(;j<=i;j++) {
               if(HG_(ivUnionAddInterval)(ivu,sb->bblocks[j].base,sb->bblocks[j].base+sb->bblocks[j].len,NULL)) {
                  changed=True;
                  if(0)VG_(printf)(__AT__":addSBtoLoop - something added\n");//something added!
               } else {
                  //VG_(printf)(__AT__":addSBtoLoop - nothing added\n");
               }
            }
         }
      }
   }
   if(sb->next&&addSBtoLoop( ivu, sb->next->base , depth-1 )) {
      res=True;
      for(;j<sb->n_bblocks;j++) {
         if(HG_(ivUnionAddInterval)(ivu,sb->bblocks[j].base,sb->bblocks[j].base+sb->bblocks[j].len,NULL)) {
            changed=True;
            if(0)VG_(printf)(__AT__":addSBtoLoop - something added\n");//something added!
         } else {
            //VG_(printf)(__AT__":addSBtoLoop - nothing added\n");
         }
      }
   }
   if(res&&changed) {
      addSBtoLoop( ivu, base, HG_(clo_control_flow_graph) );
   }

   return res;
}

static
void addIRSBtoLoop(IntervalUnion * ivu, IRSB * bbIn)
{
   Int i;
   if(0) VG_(printf)("BEGIN loopBlocks.loop generation\n");
   for(i=0;i<bbIn->stmts_used;i++) {
      IRStmt * st=bbIn->stmts[i];
      if(st->tag==Ist_IMark&&(st->Ist.IMark.len>0)) {
         loop_addInstruction(ivu,st->Ist.IMark.addr,st->Ist.IMark.len);
      }
   }
   for(i=0;i<bbIn->stmts_used;i++) {
      IRStmt * st=bbIn->stmts[i];
      if(st->tag==Ist_Exit) {
         Addr dst=value_of(IRExpr_Const(st->Ist.Exit.dst));
         addSBtoLoop(ivu,dst, HG_(clo_control_flow_graph) );
      }
   }
}

// Declaration
static
IRSB* look_ahead ( VgCallbackClosure* closure,
                   IRSB* bbIn,
                   VexGuestLayout* layout,
                   VexGuestExtents* vge,
                   IRType gWordTy, IRType hWordTy );
static
Bool getBaseSpin( SB * sb_base, SB * sb, IntervalUnion * ivu, Int depth )
{
   Bool loop_found=False;
   Int i,j,k;
   Bool hasChasing=False;
   Bool isRootCall = (depth==HG_(clo_control_flow_graph)*2);
   //HG_(clo_control_flow_graph)
   // search deeper into branches
   //if( sb_intersects(sb_base, sb) )
   if(depth<=0) return False;
   if(PRINT_getBaseSpin&&2) {
      if(isRootCall) {
         VG_(printf)("getBaseSpin(0x%llx,0x%llx)::getBaseSpin-start\n",sb_base->base,sb->base);
      }
      VG_(printf)("getBaseSpin(0x%llx,0x%llx)::d=%d\n",sb_base->base,sb->base,depth);
      pp_SB(sb,0);
   }

   if(0) {
      tl_assert(sb_base->base!=0x405cf40||sb->base!=0x406181d);
      if(sb_base->base==0x405cf40&&(HG_(ivUnionContains)(ivu,0x4061800)!=NULL)) {
         HG_(ivUnionDump_ext)(ivu,True);
         tl_assert(0);
      }
   }

   if(0) {
      tl_assert(sb->base!=0x4061800);
      for( i = 0; i < sb->n_bblocks; i++ )
      {
         tl_assert(sb->bblocks[i].base!=0x4061800);
      }
   }

   if(!isRootCall) // TODO : handle short loops(?)&& made code clean
   {
      for( i = 0; i < sb_base->n_bblocks; i++ )
      {
         // TODO ; change algorithm ?!
         if( ((k=get_sb_index(sb, sb_base->bblocks[i].base)) != -1) || ((k=get_sb_index(sb, sb_base->bblocks[i].base+sb_base->bblocks[i].len  -1  )) != -1) ) {
            loop_found = True;
            for(j=0;j<=k;j++) {
               if(HG_(ivUnionAddInterval)(ivu,sb->bblocks[j].base,sb->bblocks[j].base+sb->bblocks[j].len,NULL)) {
                  // extended
               }
            }
         }
         // do intersection without jump-chased code portions
         if( sb->bblocks[i].is_func_call )
            break;
      }
      if(PRINT_getBaseSpin&&2) {
         if(loop_found) {
            VG_(printf)("getBaseSpin(0x%llx,0x%llx):Intersection found\n",sb_base->base,sb->base);
         } else {
            VG_(printf)("getBaseSpin(0x%llx,0x%llx):Nointersect\n",sb_base->base,sb->base);
         }
      }
      if(sb_base==sb) {
         return loop_found; // no use to continue
      }
   }

   for( i = 0; i < MAX_BBLOCKS; i++ ) {
      if( sb->branches[i] && !(sb->bblocks[i].is_func_call) )
      {
         if( getBaseSpin( sb_base, sb->branches[i], ivu, depth-1 ) ) {
            loop_found = True;
            if(!isRootCall) // don't use root
            for(j=0;j<=i;j++) {
               if(HG_(ivUnionAddInterval)(ivu,sb->bblocks[j].base,sb->bblocks[j].base+sb->bblocks[j].len,NULL)) {
                  // extended
               }
            }
         }
      }
      if(sb->bblocks[i].is_func_call) {
         Addr nextBase=sb->bblocks[i].base+sb->bblocks[i].len;
         SB * nsb=lookup_or_create_SB(nextBase,NULL);

         if(PRINT_getBaseSpin&&2) {
            if(!isRootCall) VG_(printf)("getBaseSpin(0x%llx,0x%llx)::skip func_call %d, goto 0x%llx\n",sb_base->base,sb->base,i,nsb->base);
            else VG_(printf)("getBaseSpin(0x%llx,0x%llx)::NO_SKIP func_call %d, goto 0x%llx\n",sb_base->base,sb->base,i,nsb->base);
         }

         if( nsb->mark == HgL_Unknown ) {
            UShort ret = 0;

            ret = VG_(translate_ahead)( nsb->base, look_ahead );
            if( ret == 0 || nsb->mark == HgL_Unknown ) {
               // Ignore errors during ahead translation and go on normally
               soft_assert(0);
            }
         }
         if( nsb->mark != HgL_Unknown && getBaseSpin( sb_base, nsb, ivu, depth-1 ) ) {
            loop_found = True;
            if(!isRootCall) // don't use root
            for(j=0;j<=i&&j<sb->n_bblocks;j++) {
               if(HG_(ivUnionAddInterval)(ivu,sb->bblocks[j].base,sb->bblocks[j].base+sb->bblocks[j].len,NULL)) {
                  // extended
               }
            }
         }
         if(!isRootCall) {// the spin-loop SB may have started before the mutex function
            if(i<sb->n_bblocks-1) hasChasing=True;
            break;
         }
      }
   }

   // search deeper into next, if not chased func call
   if( !(hasChasing) && sb->next &&
       ( sb->next_type == HgL_Next_Direct ||
         sb->next_type == HgL_Next_Unknown  ))
   {
      if(sb->bblocks[sb->n_bblocks-1].is_func_call) {
         Addr nextBase=sb->bblocks[sb->n_bblocks-1].base+sb->bblocks[sb->n_bblocks-1].len;
         SB * nsb=lookup_or_create_SB(nextBase,NULL);

         if(0) VG_(printf)("getBaseSpin(0x%llx,0x%llx)::skip func_call %d, goto 0x%llx\n",sb_base->base,sb->base,sb->n_bblocks-1,nsb->base);

         if( nsb->mark == HgL_Unknown ) {
            UShort ret = 0;

            ret = VG_(translate_ahead)( nsb->base, look_ahead );
            if( ret == 0 || nsb->mark == HgL_Unknown ) {
               // Ignore errors during ahead translation and go on normally
            }
         }
         if( nsb->mark != HgL_Unknown && getBaseSpin( sb_base, nsb, ivu, depth-1 ) ) {
            loop_found = True;
            if(!isRootCall) // don't use root
            for(j=0;j<sb->n_bblocks;j++) {
               if(HG_(ivUnionAddInterval)(ivu,sb->bblocks[j].base,sb->bblocks[j].base+sb->bblocks[j].len,NULL)) {
                  // extended
               }
            }
         }
      } else if( getBaseSpin( sb_base, sb->next, ivu, depth-1 ) == True ) {
         loop_found = True;
         if(!isRootCall) // don't use root
         for(j=0;j<sb->n_bblocks;j++) {
            if(HG_(ivUnionAddInterval)(ivu,sb->bblocks[j].base,sb->bblocks[j].base+sb->bblocks[j].len,NULL)) {
               // extended
            }
         }
      }
   }
   return loop_found;
}

static
IntervalUnion * getBaseSpinIvu(SB * sb, Bool addLoopEnds, IntervalUnion * map_loop_endsIvu)
{
   IntervalUnion * ivu=HG_(newIvUnion)(MALLOC_CC(ivu+getBaseSpin));
   Interval * ivc;
   Addr64 loopEnd;
   getBaseSpin(sb, sb, ivu, HG_(clo_control_flow_graph)*2);


   if(map_loop_endsIvu==NULL) map_loop_endsIvu=ivu;

   if(PRINT_getBaseSpin&&2) {
      VG_(printf)("getBaseSpin(0x%llx) = H%lx ",sb->base, HG_(ivUnionHash)(ivu));
      HG_(ivUnionDump_ext)(ivu,True);
   }

   if(addLoopEnds) {
      HG_(initIterIvu)(ivu);
      while((ivc=HG_(nextIterIvu)(ivu))) {
         loopEnd=ivc->end;
         if(0) VG_(printf)("ivc->end=%llx\n",loopEnd);
         VG_(addToFM)(map_loop_ends,(UWord)loopEnd,(UWord)map_loop_endsIvu);

#ifndef NDEBUG
         if(HG_(ivUnionContains)(depconf.loopBlocks.loop,(HWord)loopEnd)!=NULL) {
            VG_(printf)("ERR:delimit_loop ");
            HG_(ivUnionDump_ext)(depconf.loopBlocks.loop,True);
            VG_(printf)("ERR:delimit_loop  contains %llx\n",loopEnd);
            soft_assert(0); // happened for my_rw_lock_wrlock : spin contains mutex_lock and unlock
         }
#endif
      }
      HG_(doneIterIvu)(ivu);
   }

   //HG_(freeIvUnion)(ivu);
   return ivu;
}

static
void loop_condition_data_dependencies_SCH( AddrList* loop )
{
   Int stmts_used;

   /* Output of this func is not stored anywhere, so we try to create a
      minimal but consistent basic block. */
   depconf.concatBB           = emptyIRSB();
   depconf.concatBB->tyenv    = emptyIRTypeEnv();
   depconf.concatBB->next     = mkIRExpr_HWord(0x1010);
   depconf.concatBB->jumpkind = Ijk_Boring;

   depconf.callDepth=0;
   depconf.minAddr=depconf.maxAddr=0;
   depconf.loop   = loop;
   addrlist_peek_front(loop,&depconf.firstBlock);

   {
      Addr64 base;

      if(PRINT_graph)
    	  depconf.dot=dot_new();

      if(!__LoopCode_INIT_ONCE) {
         if(depconf.loopBlocks.loopToFree) {
            HG_(freeIvUnion)(depconf.loopBlocks.loopToFree);
         }
         depconf.loopBlocks.loopToFree = depconf.loopBlocks.loop = HG_(newIvUnion)("loopBlocks");
         //loop_init(&depconf.loopBlocks.loop); // FIXME !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
      }

      if(PRINT_Concatenation) {
         VG_(printf)("BEGIN concatenation\n");
         addrlist_dump(loop);
      }
      while( addrlist_pop_front( loop, &base ) ) {
         tl_assert(depconf.loop == loop);
         if(0) {  // remove zeros from loop
            // TODO : handle these separators !
            Addr64 tmpAddr=0;
            if(base==0) tl_assert(addrlist_pop_front( loop, &base ));
            if(addrlist_peek_front(loop,&tmpAddr)&&tmpAddr==0) addrlist_pop_front( loop, &tmpAddr );
         } else tl_assert(base);


         /**
          * Ahead translation.
          */
         VG_(translate_ahead)( base, buildConcatBB );

         if(PRINT_graph) dot_addSB(depconf.dot,lookup_SB(base));
         // old version :
         //VG_(translate_ahead)( base, analyse_data_dependencies );
         reset_varmaps( False );
      }


      // Add instr
      if(depconf.loopBlocks.useLoopBlocks) {
         addIRSBtoLoop(depconf.loopBlocks.loop,depconf.concatBB);
      }


      if(PRINT_Concatenation) VG_(printf)("END   concatenation %llx to %llx | callDepth=%d\n",depconf.minAddr,depconf.maxAddr, depconf.callDepth);

      if(depconf.loopBlocks.useLoopBlocks && PRINT_Concatenation) {
         if(PRINT_SPIN_PROPERTY) HG_(ivUnionDump)(depconf.loopBlocks.loop);
         //VG_(printf)("\n");
      }

      //tl_assert(depconf.callDepth==0); TODO
   }

   /**
    * Print non optimised concatenated block.
    */
   DEBUG_PRINTF_E(PRINT_handleHugeBlock,({
      VG_(printf)("/ HUGE_BLOCK no optim\n");
      ppIRSB(depconf.concatBB);
   }));
   stmts_used=depconf.concatBB->stmts_used;

   //for(i=bbOut->stmts_used-1;i>0;i--) {

   // SSA : look at redundant_get_removal_BB( .)

   // FIXME_ : this changes the block inplace.
   //  concatBB shares data with the first blocks...
   //  could make big bugs :S
   // -> We think their is no sharing. Nothing to fix.
   HG_(optimise_irsb_fast)( depconf.concatBB );

   DEBUG_PRINTF_E(PRINT_handleHugeBlock,({
      static UChar buf_fnname[4096];
      Addr64 base;
      {
         Int i;
         for(i=0;i<depconf.concatBB->stmts_used;i++) {
            if(depconf.concatBB->stmts[i]->tag==Ist_IMark) {
               base=depconf.concatBB->stmts[i]->Ist.IMark.addr;
               break;
            }
         }
      }
      if(VG_(get_fnname_w_offset) ( base, buf_fnname, sizeof(buf_fnname) )) {
          VG_(printf)("In function %s\n", buf_fnname);
      }
      if(stmts_used != depconf.concatBB->stmts_used) {
         VG_(printf)("HUGE_BLOCK = optim\n");
         ppIRSB(depconf.concatBB);
      }
      VG_(printf)("\\ HUGE_BLOCK end  \n");
   }));
}

static
Addr64 delimit_loop( SB* sb, Int depth )
{
   Int i;
   Addr64 loopEnd=0;
   Addr64 tmpRes;
   //AddrList * addr_list=addrlist_create();

   if(depth == 0) {
      return loopEnd;
   }

   //addrlist_push_back(addr_list,(Addr64)sb);

   for(i=0;i<sb->n_bblocks;i++)
   {
      Addr64 base = sb->bblocks[i].base;
      UShort len  = sb->bblocks[i].len;
      Bool   isFn = sb->bblocks[i].is_func_call;

      if(base+len>loopEnd) loopEnd=base+len; // even if it is a func_call

      if( isFn )
      {
         sb = lookup_SB(base+len);
         if(sb!=NULL) {
            tmpRes=delimit_loop( sb , depth-1 );
            if(tmpRes>loopEnd) loopEnd=tmpRes;
         }
         break;
      }
      if(sb->branches[i]) {
         //addrlist_push_back(addr_list,(Addr64)sb->branches[i]);
         tmpRes=delimit_loop( sb->branches[i] , depth-1 );
         if(tmpRes>loopEnd) loopEnd=tmpRes;
      }
   }
   if( sb->next ) {
      tmpRes=delimit_loop( sb->next , depth-1 );
      if(tmpRes>loopEnd) loopEnd=tmpRes;
   }

   return loopEnd;

   //sb->n_bblocks
}

static //
Bool mark_spin_reads_SCH( SB* sb )
{
   typedef struct {
      UChar isConst;
   } AllreadyTestedIF;
   static WordFM * allreadyTestedIFS=NULL;
   Bool doPrint=True;
   Bool isSomeSpin=False;
   Bool isAllSpin=True;

   AddrList * dependencies ;
   /**
    * Choose the algorithm :
    * True : either we require that all outgoing jumps are "spinning"
    * False: one spinning jump is sufficient
    */
   const Bool useAllSpins=False;

   //if(useAllSpins) ;
   dependencies = addrlist_create();

   if(!allreadyTestedIFS) {
      allreadyTestedIFS=VG_(newFM)(HG_(zalloc),"mark_spin_reads_SCH.allreadyTestedIFS",HG_(free),NULL);
   }

   {
      Int i;
      IrsbTree * tree = HG_(newTree)(depconf.concatBB);
      Addr64 addr=0;

      /**
       * Prints original tree
       */
      if(PRINT_LOGGING_HG_LOOPS) {
         VG_(printf)("mark_spin_reads_SCH original tree:\n");
         HG_(ppTree_irsb)(tree);
      }

      //for(i=bbOut->stmts_used-1;i>=0;i--) {
      for(i=0;i<depconf.concatBB->stmts_used;i++) {
         IRStmt * st = depconf.concatBB->stmts[i];
         if(st->tag==Ist_IMark) {
            if(st->Ist.IMark.len>0) {
               addr=st->Ist.IMark.addr;
            } else {
               //ppIRStmt(st);
            }
         }
         if(st->tag==Ist_Exit) {
            Char curStmtIsASpin=0;
            UWord isTestedIf=0;

            if(VG_(lookupFM)(allreadyTestedIFS,NULL,&isTestedIf,addr)) {
               if(PRINT_mark_spin_reads_SCH_COMMENTS) VG_(printf)("Skipping if %llx ; already done\n",addr);
               /*
                * TODO what did I do with the result (isSpin=True(?);) ??
                */
               curStmtIsASpin=((isTestedIf|1)?1:-1);
               goto _mark_spin_reads_SCH_POST_TEST;
            }

            if(0){ // TODO : lost trueSpin2 and trueSpin5 when adding this
               /* Check if we jump out of the loop.
                *
                * This has to be done before adding i to allreadyTestedIFS
                * -> we can have a while within a while.
                */
               Addr64 dst=value_of(IRExpr_Const( st->Ist.Exit.dst ));
               if(dst>=depconf.minAddr&&dst<depconf.maxAddr) {
                  continue;
               }
            }

            if(doPrint) {
               DEBUG_PRINTF_E(PRINT_mark_spin_reads_SCH_TREE_BEFORE,({
                  VG_(printf)("# if-tree@%llx : \n",addr);
                  HG_(ppTree)(tree,i);
                  VG_(printf)("\n# if-tree-end\n");
               }));
            }
            // #
            if(HG_(isConstTest)(tree,i)) {
               /*
               DEBUG_PRINT_FILE(mark_spin_reads_SCH + isConstTest_return_true_0);
               DEBUG_PRINT_STACK();
               DEBUG_VG_PRINTF(mark_spin_reads_SCH,0,"concatBB=%p",concatBB);
               DEBUG_VG_PRINTF(mark_spin_reads_SCH,0,"concatBB->stmts_used=%d",concatBB->stmts_used);
               HG_printfFile(addr);*/
               const UInt maxAddr=20;//concatBB->stmts_used
               Int j;
               Addr64 loadAddr[maxAddr];
               DEBUG_PRINT_FILE(mark_spin_reads_SCH + isConstTest_return_true);
               if(doPrint && PRINT_mark_spin_reads_SCH_COMMENTS) {

                  VG_(printf)("mark_spin_reads_SCH tree after HG_(isConstTest)(tree,%d):\n",i);
                  HG_(ppTree_irsb)(tree);

                  HG_(setLogFile)(PRINT_mark_spin_reads_SCH_COMMENTS);
                  VG_(printf)("This if was constant !\n");
                  HG_(setLogFile)(NULL);
               }
               HG_(getDependendLoadAddresses)(tree,i,loadAddr,maxAddr);
               if(loadAddr[0]) { // if no load dependency, it isn't a spin
                  for(j=0;j<maxAddr;j++) {
                     if(!loadAddr[j]) break;
                     if(doPrint && PRINT_mark_spin_reads_SCH_COMMENTS) {
                        HG_(setLogFile)(PRINT_mark_spin_reads_SCH_COMMENTS);
                        VG_(printf)("# adding dependency : %llx : \n",loadAddr[j]);
                        HG_(setLogFile)(NULL);
                     }

                     //if(useAllSpins)
                     addrlist_push_back(dependencies,loadAddr[j]);
                     //else VG_(addToFM)( map_spinreads, (UWord)loadAddr[j], 0 );

                  }
                  tl_assert2(j<maxAddr,"We possibly lost some dependencies. Increase maxAddr.\n");

                  curStmtIsASpin=1; // SPIN

                  if(PRINT_graph) dot_addSpin(depconf.dot,addr);

                  if(doPrint && PRINT_mark_spin_reads_SCH_COMMENTS) {
                     HG_(setLogFile)(PRINT_mark_spin_reads_SCH_COMMENTS);
                     VG_(printf)("\nThis if is a spin !\n");
                     HG_(setLogFile)(NULL);
                  }
               } else {
                  if(doPrint && PRINT_mark_spin_reads_SCH_COMMENTS) {
                     HG_(setLogFile)(PRINT_mark_spin_reads_SCH_COMMENTS);
                     VG_(printf)("\nThis if have no loads !\n");
                     HG_(setLogFile)(NULL);
                  }
                  curStmtIsASpin=-1;
               }
            } else {
               if(doPrint && PRINT_mark_spin_reads_SCH_COMMENTS) {
                  HG_(setLogFile)(PRINT_mark_spin_reads_SCH_COMMENTS);
                  VG_(printf)("\nThis if was not constant !\n");
                  HG_(setLogFile)(NULL);
               }
               curStmtIsASpin=-1;
            }
            if(doPrint && PRINT_mark_spin_reads_SCH_TREE_AFTER) {
               HG_(setLogFile)(PRINT_mark_spin_reads_SCH_TREE_AFTER);
               VG_(printf)("#NEW IF TREE\n");
               //ppIRSB(bbOut);
               VG_(printf)("# if-tree@%llx : \n",addr);
               HG_(ppTree)(tree,i);
               VG_(printf)("\n# if-tree-end\n");
               HG_(setLogFile)(NULL);
            }
            tl_assert(curStmtIsASpin);

        _mark_spin_reads_SCH_POST_TEST:
            if( curStmtIsASpin==1 && HG_(ivUnionContains)(depconf.loopBlocks.loop,value_of( IRExpr_Const(st->Ist.Exit.dst) )) )
            {
               DEBUG_PRINTF_E(PRINT_SPIN_PROPERTY, ({
                  HG_(ivUnionDump)(depconf.loopBlocks.loop);
                  VG_(printf)("\n contains \n");
                  ppIRStmt(st);
                  VG_(printf)("\n");
               }));
               if(0) {
                  /**
                   * This makes us loose many spins
                   */
                  curStmtIsASpin=-1;
                  continue; // maybe a "real spin" of some inner loop ?
                  // -> don't add it to allreadyTestedIFS, just ignore
               }
            }

            if(curStmtIsASpin==1) {
               isSomeSpin=True;
               isTestedIf |= 1;
            }
            else if(curStmtIsASpin==-1) {
               isAllSpin = False;
            }

            if(curStmtIsASpin==1) {
               /* Sample 'trueSpin20' :
               for(i=0;i<3;i++){
                  while(*mut) ; // spin
                  *mut=1; // lock mutex 'mut'
                  //bla
                  *mut=0; // unlock
               }
               * mut isn't a spin when considering the 'for-loop',
               * but it is a spin when considering the while loop.
               * => If it is detected as modified (non-spin), it can be a spin in some smaller loop.
               */
               VG_(addToFM)(allreadyTestedIFS, addr, isTestedIf);
            }
         }
         if(doPrint &&
               PRINT_mark_spin_reads_STORE_TREES &&
               st->tag==Ist_Store) {
            HG_(setLogFile)(PRINT_mark_spin_reads_SCH_TREE_AFTER);
            VG_(printf)("#. store-tree@%llx : \n",addr);
            HG_(ppTree)(tree,i);
            VG_(printf)("\n# store-tree-end\n");
            HG_(setLogFile)(NULL);
         }
      }
      HG_(freeTree)(tree);


      if((useAllSpins && isAllSpin)||(!useAllSpins && isSomeSpin)) {
         Addr64 dependency;

         while(addrlist_pop_front(dependencies,&dependency))
         {
            if(HG_(clo_detect_mutex)) {
               SpinProperty*sp;
               if(VG_(lookupFM) ( map_spinreads, NULL, (UWord*)&sp, (UWord)dependency )) {
                  soft_assert(HG_(ivUnionCompare)(sp->codeBlock,depconf.loopBlocks.loop)==0);
                  depconf.loopBlocks.loop = sp->codeBlock;
                  //HG_(ivUnionAddIvUnion)(sp->codeBlock,depconf.loopBlocks.loop);
               } else {
                  //sp=HG_(zalloc)(MALLOC_CC(SpinProperty+sp),sizeof(*sp));
                  //sp->codeBlock=depconf.loopBlocks.loop;

                  Bool found;
                  if(0){
                     sp=getSpinProperty(depconf.loopBlocks.loop,&found);
                     if(!found) {
                        depconf.loopBlocks.loopToFree=NULL; // don't free
                     } else {
                        tl_assert(sp->codeBlock);
                        tl_assert(depconf.loopBlocks.loop);
                        //DEBUG_PRINT_STACK();
                        tl_assert(HG_(ivUnionCompare)(sp->codeBlock,depconf.loopBlocks.loop)==0);
                        depconf.loopBlocks.loop = sp->codeBlock;
                     }
                     VG_(addToFM)( map_spinreads, (UWord)dependency, (UWord)sp );
                  } else {
                     IntervalUnion * ivu=getBaseSpinIvu(sb,True,NULL);
                     //HG_(freeIvUnion)(ivu);
                     sp=getSpinProperty(ivu,&found);
                     VG_(addToFM)( map_spinreads, (UWord)dependency, (UWord)sp );
                  }
               }
               tl_assert(depconf.loopBlocks.loopToFree!=sp->codeBlock);
            } // !HG_(clo_detect_mutex)
            else {
               VG_(addToFM)( map_spinreads, (UWord)dependency, (UWord)NULL );
            }

         }
         addrlist_destroy(dependencies);
      }

   }
#ifndef NDEBUG
   {
      Int i;
      for(i=0;i<depconf.concatBB->stmts_used;i++) {
         if(depconf.concatBB->stmts[i]->tag==Ist_WrTmp) {
            depconf.concatBB->stmts[i]->tag=Ist_Dirty;
         }
      }
   }
#endif
   //return isSpin;

   if(PRINT_graph) VG_(printf)("//filename:%s%d_%p\n",(isSomeSpin?"spin":"noSpin"),_dot_static.dotIdx ,depconf.concatBB);
   DEBUG_PRINTF_E(PRINT_graph,({
      if(isSomeSpin) {
         VG_(printf)("// SPIN FOUND \n");
      } else {
         VG_(printf)("// NO_SPIN \n");
      }
      if(depconf.dot) {
         VG_(printf)("//filename:%s%d_%p\n",(isSomeSpin?"spin":"noSpin"),_dot_static.dotIdx ++,depconf.concatBB);
         dot_pp(depconf.dot);
         dot_free(depconf.dot);
         depconf.dot=NULL;
      }
   }));
   if(useAllSpins) return isAllSpin;
   else return isSomeSpin;
}

static
void loop_condition_data_dependencies( AddrList* loop )
{
   Addr64 base;
   
   while( addrlist_pop_front( loop, &base ) ) {
      VG_(translate_ahead)( base, analyse_data_dependencies );
      reset_varmaps( False );
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
Bool mark_spin_reads_WRK( Variable* condition, SB* sb ) {
   Variable *dep;
   Bool found_spin_read = False;
   
   VG_(initIterFM)( condition->dependencies );
   while( VG_(nextIterFM)( condition->dependencies, (UWord*)&dep, NULL ) ) {
      if( dep->type == HG_CFG_VAR_LOAD &&
          load_is_const( dep ) &&
          get_sb_index(sb, dep->index) != -1 ) {
            VG_(addToFM)( map_spinreads, (UWord)dep->index, 0 );
            found_spin_read = True;
      }
   }
   VG_(doneIterFM)( condition->dependencies );
   
   return found_spin_read;
}

static
Bool mark_spin_reads( SB* sb )
{
   Variable *condition = NULL;
   
   condition = varmap_lookup( HG_CFG_VAR_CONDITION, 0 );
   if( !condition )
      return False;
   
   if( !loop_condition_is_const( condition ) )
      return False;
   
   return mark_spin_reads_WRK( condition, sb );
}


/*----------------------------------------------------------------*/
/*--- Spin Read Detection                                      ---*/
/*----------------------------------------------------------------*/

void update_SB_with_UC (SB* sb); /* fwd */

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
   
   /* static */ looking_ahead = True;
   
   hg_loops_instrument_bb( HgL_Pre_Instrumentation, bbIn, bbOut, closure, layout, vge, NULL );

   /* static */ looking_ahead = False;
   
   return bbOut;
}

/*
 * sb_intersects( sb1, sb2 ) checks if the two superblocks shares at least one 
 *  line of assembly code
 * 
 * consider following scenario:
 *
 *  +--+
 *  |19|
 *  +--+ (jump chasing)         +---+
 *  |37|                        v   |
 *  +--+ -------------------> +--+  |
 *   |                        |22|  |
 *   |                        +--+  |
 *   |                         |    |
 *   |                         v    |
 *   v                        +--+  |
 *  +--+ <------------------- |31| -+
 *  |41|                      +--+
 *  +--+
 *  
 *  basic block 37 is contained in 31.
 *  so the 19/37 superblock intersects with 31.  
 */

static
Bool sb_intersects( SB* sb1, SB* sb2 )
{
   int i;
   
   if( sb1 == sb2 ) //trivial
      return True;
   
   for( i = 0; i < sb1->n_bblocks; i++ )
   {
      if( get_sb_index(sb2, sb1->bblocks[i].base) != -1 )
         return True;
      // do intersection without jump-chased code portions
      if( sb1->bblocks[i].is_func_call )
         break;
   }

   for( i = 0; i < sb2->n_bblocks; i++ )
   {
      if( get_sb_index(sb1, sb2->bblocks[i].base) != -1 )
         return True;
      // do intersection without jump-chased code portions
      if( sb2->bblocks[i].is_func_call )
         break;
   }
   
   return False;
}



/*
 * Depth first search with Superblocks to detect loops in IR
 */

static
Bool dfs_search_WRK( SB* sb_base,
                     SB* sb,
                     AddrList* loop,
                     AddrList* path,
                     unsigned depth,
                     AddrList* return_addresses_from_parent )
{
   AddrList* return_addresses = NULL;
   Bool   loop_found = False;
   unsigned int unhandled_function_calls = 0;
   int i;
   
   /* -------- break condition -------- */
   if( depth == 0 )
      return False;
      
   /* -------- discover unknown blocks -------- */
   if( sb->mark == HgL_Unknown ) {
      UShort ret = 0;

#if defined(__amd64) && 0
      this code leds to bugs in the spin-read detection
      if(sb->bblocks[0].len==0) {
         VG_(printf)("dfs_search_WRK got an empty Superblock\n");
         return False;
      }
#endif

      ret = VG_(translate_ahead)( sb->base, look_ahead );
      if( ret == 0 || sb->mark == HgL_Unknown ) {
         // Ignore errors during ahead translation and go on normally
         return False;
      }
   }
   tl_assert( sb->mark != HgL_Unknown );

   /* -------- unpreditable function call handling -------- */
   
   update_SB_with_UC(sb);
   
   /* -------- detect loops -------- */
   
   /* assure we were called recursively, otherwise
      sb_base == sb is always true */
   if( return_addresses_from_parent != NULL ) {
      if( sb_base == sb ) { // trivial loop
         addrlist_push_all_back( path, loop, /* don't replace first sb */ 0 );
         if( PRINT_Concatenation ) {
            VG_(printf)("loop variation found: ");
            addrlist_dump(path);
         }
         return True;
      } else
      if( sb_intersects(sb_base, sb) ) { // loop - maybe with initialization
         if(0) {
            /**
             * Print out graph structure.
             */
            Dot * dot;
            AddrList * cp=addrlist_dopy(path);
            Addr64 sbBase;
            dot=dot_new();
            VG_(printf)("dot_new:ok\n");
            while(addrlist_pop_front(cp,&sbBase)) {
               DEBUG_PRINTF(PRINT_graph,"// sbBase=0x%llx \n",sbBase);
               dot_addSB(dot,lookup_SB(sbBase));
               VG_(printf)("dot_addSB:ok\n");
            }
            dot_addSB(dot,sb);
            dot_addSpin(dot,sb->base); // Mark it
            VG_(printf)("dot_addSpin:ok\n");
            tl_assert(dot->sBs); // TODO delete
            addrlist_destroy(cp);
            VG_(printf)("dot :pre\n");
            //DEBUG_PRINTF_E(PRINT_graph,
                  ({
               do {
                  VG_(printf)("// Stack at "__AT__" :\n"); {
                  Addr st[20];
                  HG_(myPPstackTrace)(HG_(getEipStack)(st,0,20),"//   ");}
               }while(0);

               VG_(printf)("// New loop found \n");
               VG_(printf)("//filename:dfs_%llx\n",sb->base);
               tl_assert(dot->sBs); // TODO delete
               dot_pp(dot);
               tl_assert(dot->sBs); // TODO delete
            });
            //);

            VG_(printf)("pre dot_free\n");
            dot_free(dot);
         }
         addrlist_push_all_back( path, loop, sb->base );
         if( PRINT_Concatenation ) {
            VG_(printf)("loop variation found: replace %llx <-> ", sb->base );
            addrlist_dump(path);
         }
         return True;
      }
   }
   
   /* -------- go deeper -------- */
   
   if( depth > 1 ) {
      Addr64 last_addr;

      /* -------- add this superblock to path -------- */
      
      addrlist_push_back( path, sb->base );
      
      /* -------- init return stack -------- */
      if( return_addresses_from_parent == NULL ) {
         return_addresses = addrlist_create();
         if( sb->return_hint != 0 )
            addrlist_push_back( return_addresses, sb->return_hint );
      }
      else {
         return_addresses =
               addrlist_dopy( return_addresses_from_parent );
      }
      
      // search deeper into chased function calls
      for( i = 0; i < sb->n_bblocks; i++ ) {
         if( sb->bblocks[i].is_func_call ) {
            addrlist_push_back( return_addresses,
                  sb->bblocks[i].base + sb->bblocks[i].len );
            unhandled_function_calls++;
         }
      }
      
      // search deeper into branches
      for( i = 0; i < MAX_BBLOCKS; i++ ) {
         if( sb->branches[i] )
         {
            if( dfs_search_WRK( sb_base, sb->branches[i], loop, path,
                  depth-1, return_addresses ) == True ) {
               loop_found = True;
            }
         }
         //if( loop_found  ) { } SCH
      }
      
      // search deeper into next
      if( sb->next &&
          ( sb->next_type == HgL_Next_Direct ||
            sb->next_type == HgL_Next_Unknown  ))
      {
         if( dfs_search_WRK( sb_base, sb->next, loop, path,
               depth-1, return_addresses ) == True ) {
            loop_found = True;
         }
      }
      else
      if( sb->next_type == HgL_Next_Return ||   /* explizit return */
          sb->next_type == HgL_Next_Indirect )  /* dynamic function call */
      {
         Addr64 retip = 0;
         if( addrlist_pop_back( return_addresses, &retip ) ) {
            SB* next = lookup_or_create_SB( retip, NULL );
            SB_RET_EDGE(sb,next);// TODO SCHMALTZ : I added this
            if( dfs_search_WRK( sb_base, next, loop, path,
                  depth-1, return_addresses ) == True ) {
               loop_found = True;
            }
            if( unhandled_function_calls > 0 )
               unhandled_function_calls--;
         }
      }
      
      while( !loop_found && unhandled_function_calls > 0 )
      {
         Addr64 retip = 0;
         if( addrlist_pop_back( return_addresses, &retip ) ) {
            SB* next = lookup_or_create_SB( retip, NULL );
            SB_RET_EDGE(sb,next);// TODO SCHMALTZ : I added this
            if( dfs_search_WRK( sb_base, next, loop, path,
                  depth-1, return_addresses ) == True ) {
               loop_found = True;
            }
         }
         unhandled_function_calls--;
      }
      
      /* -------- clean up -------- */
      addrlist_destroy( return_addresses );
      
      addrlist_pop_back( path, &last_addr );
      tl_assert( last_addr == sb->base );
   }
   
   return loop_found;
}

static
void print_debuginfo( Addr64 addr )
{
#define BUF_LEN 1024
   static Char *sectkind;
   UInt  lineno;
   static UChar buf_srcloc[BUF_LEN];
   static UChar buf_dirname[BUF_LEN];
   static UChar buf_fn[BUF_LEN];
   static UChar buf_obj[BUF_LEN];
   Bool  know_dirinfo = False;
   Bool  know_srcloc  = VG_(get_filename_linenum)(
                           addr, 
                           buf_srcloc,  BUF_LEN, 
                           buf_dirname, BUF_LEN, &know_dirinfo,
                           &lineno 
                        );
   Bool  know_fnname  = VG_(get_fnname) (addr, buf_fn, BUF_LEN);
   
   sectkind = (Char*)VG_(pp_SectKind(VG_(seginfo_sect_kind)( buf_obj, sizeof(buf_obj), addr )));

   if( know_fnname )
      VG_(printf)("<%s>", buf_fn );
   else
      VG_(printf)("<?> in %s", sectkind);
   
   if( know_srcloc )
      VG_(printf)(" (%s:%d)\n", buf_srcloc, lineno );
   else
      VG_(printf)(" (%s)\n", buf_obj );
   
#undef BUF_LEN
}

static
Bool detect_spining_reads_lookAhead( SB * sb, unsigned depth, unsigned lkAhDepth )
{
   //static Int noRecurse=0;

   Int i;
   SB* nSb;
   UShort ret = 0;
   Bool nextSpin=False;

   AddrList* loop;
   AddrList* path;

   //pp_SB(sb,0);
   if(lkAhDepth>0) {
      loop = addrlist_create();
      path = addrlist_create();

      // TODO : optimise
      //  - if nextSpin is true, don't continue calculation of dfs_search_WRK
      //  - store the result of dfs_search_WRK calls is SB ?
      //    - call detect_spining_reads ? (avoid doing same work twice)

      /*
       * We have to consider non-atomic loads
       *   (atomicity may be ensured by a surrounding mutex)
       * -> compare adresses of spin reads (in loop) and before loop
       */

      for( i = 0; i < MAX_BBLOCKS; i++ ) {

         nSb = sb->branches[i];

         if( nSb )
         {
            ret=1;
            if( nSb->mark == HgL_Unknown ) {
               ret = VG_(translate_ahead)( nSb->base, look_ahead );
            }
            if( !( ret == 0 || sb->mark == HgL_Unknown ) ) {
               // Ignore errors during ahead translation and go on normally
               nextSpin |= dfs_search_WRK( nSb, nSb, addrlist_empty(loop), addrlist_empty(path), depth, /* init */ NULL );
               if(nextSpin) goto detect_spining_reads_lookAhead_HAS_NEXTSPIN;
               nextSpin |= detect_spining_reads_lookAhead(nSb,depth,lkAhDepth-1);
               if(nextSpin) goto detect_spining_reads_lookAhead_HAS_NEXTSPIN;
            }
         }
      }

      if( sb->next_type == HgL_Next_Direct ||
        sb->next_type == HgL_Next_Unknown  )
         nSb = sb->next;
      else
         nSb = NULL;
      if( nSb )
      {
         ret=1;
         if( nSb->mark == HgL_Unknown ) {
            ret = VG_(translate_ahead)( nSb->base, look_ahead );
         }
         if( !( ret == 0 || sb->mark == HgL_Unknown ) ) {
            // Ignore errors during ahead translation and go on normally
            nextSpin |= dfs_search_WRK( nSb, nSb, addrlist_empty(loop), addrlist_empty(path), depth, /* init */ NULL );
            if(nextSpin) goto detect_spining_reads_lookAhead_HAS_NEXTSPIN;
            nextSpin |= detect_spining_reads_lookAhead(nSb,depth,lkAhDepth-1);
            if(nextSpin) goto detect_spining_reads_lookAhead_HAS_NEXTSPIN;
         }
      }
      if(nextSpin) {
detect_spining_reads_lookAhead_HAS_NEXTSPIN:
         if(!COMMITABLE) VG_(printf)("Has nextSpin\n");
         //pp_SB(sb,0);

         if(sb->atomicLoads) {
            /**
             * Simple solution : suppose the spin vars are exacly the loads of the atomic instruction.
             *  TODO : compare load addresses, with adresses from the spin loop
             */
            Addr64 dependency;
            AddrList * dependencies;

            dependencies=addrlist_dopy(sb->atomicLoads);
            while(addrlist_pop_front(dependencies,&dependency)) {
               if(!COMMITABLE) VG_(printf)("Loop-header spin var 0x%llx\n",(dependency));
               //if(1) VG_(addToFM)( map_spinreads, (UWord)dependency, 0 );else
               if(HG_(clo_detect_mutex))
               {
                  SpinProperty*sp;
                  tl_assert(depconf.loopBlocks.loop);
                  if(VG_(lookupFM) ( map_spinreads, NULL, (UWord*)&sp, (UWord)dependency )) {
                     //tl_assert(HG_(ivUnionCompare)(sp->codeBlock,depconf.loopBlocks.loop)==0); // for old version only
#if !defined( NDEBUG ) && 0 /* getBaseSpinIvu has some side effects */
                	 IntervalUnion * ivu=getBaseSpinIvu(nSb,True,NULL); // just for assert
                	 soft_assert(HG_(ivUnionCompare)(sp->codeBlock,ivu)==0);
#endif
                     //HG_(ivUnionAddIvUnion)(sp->codeBlock,depconf.loopBlocks.loop);
                  } else {
                     //sp=HG_(zalloc)(MALLOC_CC(SpinProperty+sp),sizeof(*sp));
                     //sp->codeBlock=depconf.loopBlocks.loop;
                     Bool found;
                     if(0) {
                        sp=getSpinProperty(depconf.loopBlocks.loop,&found);
                        if(!found) depconf.loopBlocks.loopToFree=NULL; // don't free if not found (reference used)
                        VG_(addToFM)( map_spinreads, (UWord)dependency, (UWord)sp );
                     }else{
                        IntervalUnion * ivu=getBaseSpinIvu(nSb,True,NULL);
                        if(!COMMITABLE) VG_(printf)(__AT__ ":nSB isn't always the right place - TODO\n"); // TODO
                        //HG_(freeIvUnion)(ivu);
                        sp=getSpinProperty(ivu,&found);
                        VG_(addToFM)( map_spinreads, (UWord)dependency, (UWord)sp );

                     }
                  }
               } else {
                  VG_(addToFM)( map_spinreads, (UWord)dependency, (UWord)NULL );
               }
            }
            addrlist_destroy(dependencies);
         }
         //found_spin_read = True;
      }

      addrlist_destroy( loop );
      addrlist_destroy( path );
   }
   return nextSpin;
}

static
Bool detect_spining_reads( SB * sb, unsigned depth )
{
   static UChar buf_obj[4096];
   Bool found_spin_read = False;
   Bool found_loop = False;
   AddrList* loop;
   AddrList* path;

   /* run only once */
   if( sb->mark == HgL_Visited )
      return False;
   tl_assert( sb->mark == HgL_Analysed );
   sb->mark = HgL_Visited;

   /* Filtering out common libraries that have nothing to do
    *  with multithreading and therefor cannot do synchronization */
   VG_(seginfo_sect_kind)( buf_obj, sizeof(buf_obj), sb->base );
   if( VG_(string_match)("/lib*/libc-*.so", buf_obj) ||
       VG_(string_match)("/lib*/ld-*.so", buf_obj) ||
       VG_(string_match)("/usr/lib*/libstdc++.so*", buf_obj) ||
       VG_(string_match)("*/vgpreload*", buf_obj) )
   {
      return False;
   }

   /* Filter out PThread library when requested as we suppose
    *  that library interception does a good job. */
   if( hg_cfg__clo_ignore_pthread_spins == True &&
       VG_(string_match)("/lib*/libpthread-*.so", buf_obj) ) {
      return False;
   }

   loop = addrlist_create();
   path = addrlist_create();

   if( hg_cfg__clo_show_bb != 0 &&
       get_sb_index(sb, hg_cfg__clo_show_bb) != -1 ) {
      VG_(printf)("detect_spining_reads (0x%llx, %d) \n", sb->base, depth );
   }

   if( dfs_search_WRK( sb, sb, loop, path, depth, /* init */ NULL ) )
   {
      Bool debug_da = False;

      /* debug --da= */
      if( hg_cfg__clo_verbose_control_flow_graph ||
          in_addrlist( loop, hg_cfg__clo_test_da )  ) {
         addrlist_dump( loop );
         print_debuginfo( sb->base );
         debug_da = True;
      }


      if( !(DEBUG_CONF(useOrigAlgo)) ) {
         if( PRINT_ENTERING_MY_ALGO ) {
            VG_(printf)("######  New block for hg_dependency coming from detect_spining_reads %llx ######\n",sb->base);
         }
         loop_condition_data_dependencies_SCH( loop );
         found_spin_read = mark_spin_reads_SCH( sb );
      }else{
         loop_condition_data_dependencies( loop );
         found_spin_read = mark_spin_reads( sb );
      }
      found_loop = True;

      /* debug */
      if( debug_da ) {
         pp_varmaps(4);
      }
   } // end if( dfs_search_WRK (..) )
   else if(sb->atomicLoads/* if contains atomic instructions */){
      detect_spining_reads_lookAhead(sb,depth,3);
   }
   
   addrlist_destroy( loop );
   addrlist_destroy( path );
   
   /* Debugging output */
   if( hg_cfg__clo_verbose_control_flow_graph ) {
      if( found_spin_read ) {
         VG_(printf)("************spin found in %s\n", buf_obj);
         pp_varmaps(4);
         pp_SB( sb, 0 );
      }
      //else
      //   VG_(printf)("loop found!\n");
   }
   
   /* Reset data analysis store */
   reset_varmaps( True );
   
   if( found_loop )
      stats__loops_found++;
   if( found_spin_read )
      stats__spins_found++;
   
   return found_spin_read;
}

/*----------------------------------------------------------------*/
/*--- END: Spin Read Detection                                 ---*/
/*----------------------------------------------------------------*/



/*----------------------------------------------------------------*/
/*--- Instrumentation helpers                                  ---*/
/*----------------------------------------------------------------*/

static
void handle_unpredictable_functioncalls( ThreadLoopExtends* tle, SB* sb ) {
   if( tle->uc ) {
      SB_RET_EDGE(sb,lookup_or_create_SB(tle->uc->return_addr,NULL));
      sb->return_hint = tle->uc->return_addr;
      if( tle->uc->dest_addr != sb->base ) {
         tle->uc->dest_addr = sb->base;
         VG_(discard_translations)( tle->uc->instr_addr,
                                    tle->uc->instr_len,
                          "hg_loops.c: handle_unpredictable_functioncalls" );
      }
      if( tle->uc->invalidate_sb &&
          tle->uc->invalidate_sb->mark == HgL_Visited )
         tle->uc->invalidate_sb->mark = HgL_Analysed;
   }
}

#ifndef VG_REGPARM
   #define VG_REGPARM(n)            __attribute__((regparm(n)))
   #error "This was just set because eclipse doesn't see the definition"
#endif

static VG_REGPARM(1)
void start_spin_reading(SpinProperty * sp) {
   tl_assert(!hg_loops__spin_reading);
   /* static */ hg_loops__spin_reading = True;

   if(PRINT_spin_lockset_logLoops&&HG_(clo_detect_mutex)) VG_(printf)("start_spin_reading.Sp=%p\n",sp);
   if(sp) {
      assert(HG_(clo_detect_mutex));
      HG_(set_lastSpinProperty)( sp );
      if(PRINT_SPIN_PROPERTY) ppSpinProperty(sp);
   }
}

static VG_REGPARM(0)
void stop_spin_reading(void) {
   tl_assert(hg_loops__spin_reading);
   /* static */ hg_loops__spin_reading = False;
}

static VG_REGPARM(1)
void set_current_SB_and_UC(SB* sb) {
   ThreadLoopExtends* tle = main_get_LoopExtends();

   tl_assert(sb);
   tl_assert(tle);
   
   tle->curSB = sb;
   
   handle_unpredictable_functioncalls(tle, sb);
   
   tle->uc = sb->uc;
   
   //VG_(printf)("# 0x%llx\n", sb->base);
}

static VG_REGPARM(3)
void spin_follow_exitif(/*HWord guard,*/ HWord destination, HWord isInLoop, IntervalUnion * ivu)
{
   //if(guard)
   tl_assert(!(ivu)||HG_(ivUnion_isSane)(ivu));
   {
      if(PRINT_spin_lockset_logLoops) {
         Char fnName[100];
         Bool hasFnName=VG_(get_fnname_w_offset)(destination, fnName, sizeof(fnName));
         VG_(printf)("spin_follow_exitif : jump to %lx in %s : isInLoop=%d\n",destination,(Char*)(hasFnName?fnName:"??"),(Int)isInLoop);
      }

      //isInLoop=HG_(ivUnionContains)(ivu, destination)!=NULL; //TODO : delete

      if(isInLoop==31415) {
         if(ivu) {
            isInLoop=HG_(ivUnionContains)(ivu, destination)!=NULL;
            DEBUG_PRINTF_E(PRINT_SPIN_PROPERTY,({
               VG_(printf)("\tdynamic isInLoop=%d  ",(Int)isInLoop);
               HG_(ivUnionDump)(ivu);
               VG_(printf)("\n");
            }));
         } else {
            VG_(printf)(__AT__ "no IntervalUnion\n");
         }
      }
      if(isInLoop==0) {
         // TODO : we exited the loop
         if(PRINT_spin_lockset_logLoops) VG_(printf)("Code address %lx is the exit of a Spin\n",destination);

         HG_(onExitSpinLoop) ( destination, ivu ); //TODO

         HG_(set_lastSpinProperty)( NULL );
      }
   }
}

Bool hg_loops_is_spin_reading( void ) {
   return /* static */ hg_loops__spin_reading;
}

/*
 * Refreshes the next-pointer of a Superblock
 * with up to date information about
 * unpredictable function calls  
 */
void update_SB_with_UC (SB* sb) {
   UC* uc = NULL;
   
   if( sb->last_jump_iaddr ) {
      uc = lookup_UC( sb->last_jump_iaddr );
      tl_assert(uc);
      if( uc->dest_addr != 0 )
      {
         SB* dest = lookup_or_create_SB( uc->dest_addr, NULL );
         tl_assert(dest);
         sb->next = dest;
      }
   }
}

static
void analyse_branches ( SB* sb,
                        IRSB* bbIn,
                        VgCallbackClosure* closure,
                        VexGuestLayout* layout,
                        VexGuestExtents* vge )
{
   int i;
   SB* dest;
   
   /* Address and length of the current binary instruction */
   Addr   iaddr = 0,
          ilen  = 0;
   
   /* Index of current basic block within SB */
   UInt idx = 0;

   /////////////////////////////////////////////////////////////////
   // Defered unpredictable function call handling
   
   ThreadLoopExtends* tle = main_get_LoopExtends();
   tl_assert(tle);
   
   handle_unpredictable_functioncalls(tle, sb);
   
   /////////////////////////////////////////////////////////////////
   // Destination of next expression?
   
   switch( bbIn->jumpkind ) {
   case Ijk_Call:
      sb->bblocks[sb->n_bblocks-1].is_func_call = True;
      /* no break */
   case Ijk_Boring: {
      
      /* we can evaluate the address of next SB directly
       * if expression is an immediate value
       */
      if( bbIn->next->tag == Iex_Const ) {
         dest = lookup_or_create_SB( value_of(bbIn->next), NULL );
         tl_assert(dest);
         sb->next_type = HgL_Next_Direct;
         sb->next = dest;
      } else
         
      /* address of next SB is stored in a temp
       * which means it represents one of the following:
       * - an indirect jump ( e.g. next = *0x8000 )
       * - a return statement ( e.g. next = *(something from stack) )
       */
      if( bbIn->next->tag == Iex_RdTmp ) {
         IRExpr* dest_expr = NULL;
         Addr    dest_addr = 0;
         
         // put contents of temp into dest_expr
         for (i = 0; i < bbIn->stmts_used; i++) {
            IRStmt* st = bbIn->stmts[i];
            tl_assert(st);
            if( st->tag == Ist_WrTmp &&
                  st->Ist.WrTmp.tmp == bbIn->next->Iex.RdTmp.tmp ) {
               dest_expr = st->Ist.WrTmp.data;
               break;
            }
         } /* for ( ... stmts ... ) */
         
         tl_assert( dest_expr );

         if( dest_expr->tag == Iex_Load ) {
            /* --- dynamic function call ? --- */
            VgSectKind dst_kind;
            VgSectKind cur_kind;
            
            dest_addr = value_of( dest_expr );
            
            dst_kind = VG_(seginfo_sect_kind)( NULL, 0, dest_addr );
            cur_kind = VG_(seginfo_sect_kind)( NULL, 0, sb->bblocks[sb->n_bblocks-1].base );
            
            if( dst_kind == Vg_SectGOTPLT &&
                cur_kind == Vg_SectPLT )
            {
               dest = lookup_or_create_SB( dest_addr, NULL );
               tl_assert(dest);
               
               sb->next_type = HgL_Next_Indirect;
               sb->next = dest;
            }
         }
         /* else */
         
         if( sb->next_type == HgL_Next_Unknown )
         {
            /* --- unpredictable function call or a return statement --- */
            UC* uc = NULL;
            
            // Get last IMark
            for( i = 0; i < bbIn->stmts_used; i++) {
               IRStmt *st = bbIn->stmts[i]; 
               if( st->tag == Ist_IMark ) {
                  iaddr = st->Ist.IMark.addr;
                  ilen  = st->Ist.IMark.len;
               }
            }
            
            tl_assert( iaddr != 0 );
            
            sb->last_jump_iaddr = iaddr;
            
            uc = lookup_or_create_UC( iaddr, ilen, sb );
            uc->return_addr = iaddr + ilen;
            
            sb->uc = uc;
            
            update_SB_with_UC(sb);
         }
      } else {
         // Flat IR only jumps to an address stored
         //  either as constant or in a temp
         tl_assert(0);
      } /* if( bbIn->next->tag == ... ) */
   } /* case Ijk_Boring: */
   break;
   
   case Ijk_ClientReq:
   case Ijk_Sys_syscall:
   case Ijk_Sys_int32:
   case Ijk_Sys_int128:
   case Ijk_Sys_int129:
   case Ijk_Sys_int130:
   case Ijk_Sys_sysenter:
   case Ijk_Yield: {
      Addr next_to_sb = sb->bblocks[sb->n_bblocks-1].base;
      next_to_sb += sb->bblocks[sb->n_bblocks-1].len;
      sb->next_type = HgL_Next_Direct;
      sb->next = lookup_or_create_SB( next_to_sb, NULL );
   }
   break;
      
   case Ijk_Ret: {
      sb->next_type = HgL_Next_Return;
   }
   break;
   
   case Ijk_SigSEGV:
   case Ijk_SigTRAP:
   case Ijk_NoRedir:
   case Ijk_EmFail:
      /* ignore */
      break;
      
   case Ijk_NoDecode:
      VG_(printf)(__AT__"-- found not decoded block (Ijk_NoDecode)\n");
	   break;

   default:
      ppIRSB(bbIn);
      tl_assert(0);
   } /* switch( bbIn->jumpkind ) */

   /////////////////////////////////////////////////////////////////
   // Handle Exit statements

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

         case Ist_IMark: {
            iaddr = st->Ist.IMark.addr;
            ilen  = st->Ist.IMark.len;

            idx = get_sb_index( sb, iaddr );
         } break;
         
         // Chased function call ?
         case Ist_Store: {
            IRExpr * data = st->Ist.Store.data;
            Addr next_to_bb = sb->bblocks[idx].base + sb->bblocks[idx].len;
            if( data->tag == Iex_Const &&
                value_of(data) == next_to_bb &&
                idx+1 < sb->n_bblocks &&
                sb->bblocks[idx+1].base != next_to_bb )
            {
               sb->bblocks[idx].is_func_call = True;
            }
         } break;
         
         case Ist_Exit: {
            Addr dest_addr = value_of(IRExpr_Const(st->Ist.Exit.dst));
            
            dest = lookup_or_create_SB( dest_addr, NULL ); 
            
            tl_assert(dest);
            tl_assert(idx != -1);
            
            /* inverted branch? */
            if( dest == sb->next )
               break;

            /* strange branch? */
            if( st->Ist.Exit.jk != Ijk_Boring )
               break;
            
            /* add branch to superblock structure */
            for( idx = 0; idx < MAX_BBLOCKS; idx++ ) {
               if( sb->branches[idx] == dest )
                  break;
               else
               if( sb->branches[idx] == NULL ) {
                  sb->branches[idx] = dest;
                  break;
               }
            }
            
            if( idx == MAX_BBLOCKS ) {
               ppIRSB( bbIn );
               VG_(printf)("%d == %d !\n",idx,MAX_BBLOCKS);
               soft_assert( 0 );
            }
         } break;
         
         default:
            tl_assert(0);

      } /* switch (st->tag) */
   } /* iterate over bbIn->stmts */
}

/*----------------------------------------------------------------*/
/*--- Instrumentation                                          ---*/
/*----------------------------------------------------------------*/

Bool hg_loops_instrument_bb (  HG_LOOPS_INSTR_MODE mode,
                               IRSB* bbIn,
                               IRSB* bbOut,
                               VgCallbackClosure* closure,
                               VexGuestLayout* layout,
                               VexGuestExtents* vge,
                               IRStmt* stmt )
{
   static Bool spin_reading = False;
   static Bool debug_print_bbOut = False;
   
   static Bool inBlockWithSpins = False;

   tl_assert( mode == HgL_Pre_Instrumentation || mode == HgL_Instrument_Statement || mode == HgL_Post_Instrumentation );

   // Control flow analysis and spin read detection off?
   if ( HG_(clo_control_flow_graph) <= 0 )
      return True;
   
#ifndef NDEBUG
   {
      static char WARN_NDEBUG = 0;
      if(!WARN_NDEBUG) {
         VG_(printf) (" ~~> Warning, Debug activated. NDEBUG not defined while compiling (file="__FILE__")\n");
         WARN_NDEBUG=1;
      }
   }
#endif
   
   /////////////////////////////////////////////////////////////////
   // Pre instrumentatin analysis
   
   if ( mode == HgL_Pre_Instrumentation ) {
      SB *sb = lookup_or_create_SB( vge->base[0], vge );
      
      // Instrument to set the current SB and UC
      {
         IRExpr** argv     = NULL;
         IRDirty* di       = NULL;
         
         argv = mkIRExprVec_1( mkIRExpr_HWord( (HWord)sb ) );
         di = unsafeIRDirty_0_N( 1,
                                 "set_current_SB_and_UC",
                                 VG_(fnptr_to_fnentry)( &set_current_SB_and_UC ),
                                 argv );
         addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
      }
      
      // Analyse branches of this SB if not done yet
      if( sb->mark == HgL_Unknown ) {
         update_vge_info( sb, vge );
         analyse_branches( sb, bbIn, closure, layout, vge );
         sb->mark = HgL_Analysed;
      }

      // Search for spin loops
      if( /* static */ looking_ahead == False &&
          sb->mark != HgL_Visited ) {
         depconf.instrumented_bbIn = bbIn;
         depconf.instrumented_bbOut = bbOut;

         analyse_atomicInstructions(sb,bbIn);

         inBlockWithSpins = detect_spining_reads( sb, HG_(clo_control_flow_graph) );

         if(HG_(clo_detect_mutex)&&inBlockWithSpins&&(__LOOP_ALGO_ID==1)) {
            if(0) {
               Addr64 loopEnd;
               Interval * ivc = NULL;
               Int i;
               ivc = HG_(ivUnionContains)(depconf.loopBlocks.loop,(HWord)sb->base);
               for(i=0;(i<sb->n_bblocks) && (ivc==NULL);i++) {
                  ivc = HG_(ivUnionContains)(depconf.loopBlocks.loop,(HWord)sb->bblocks[i].base);
               }
               //loopEnd=delimit_loop( sb, HG_(clo_control_flow_graph) );
               if(!ivc) {

                  // May happen because of detect_spining_reads_lookAhead :
                  //   the spin loop can be in the next blocks.
                  /*
                  VG_(printf)("ERR:delimit_loop ");
                  HG_(ivUnionDump_ext)(depconf.loopBlocks.loop,True);
                  //VG_(printf)("ERR:delimit_loop ");
                  pp_SB(sb,0);
                  tl_assert(0);
                  */
               }else if(0) {
                  loopEnd=ivc->end;
                  //tl_assert(HG_(clo_control_flow_graph)>0);
                  tl_assert(loopEnd);
                  VG_(addToFM)(map_loop_ends,(UWord)loopEnd,(UWord)depconf.loopBlocks.loop);


                  if(HG_(ivUnionContains)(depconf.loopBlocks.loop,(HWord)loopEnd)!=NULL) {
                     VG_(printf)("ERR:delimit_loop ");
                     HG_(ivUnionDump_ext)(depconf.loopBlocks.loop,True);
                     VG_(printf)("ERR:delimit_loop  contains %llx\n",loopEnd);
                     tl_assert(0);
                  }

                  if(depconf.loopBlocks.loop==depconf.loopBlocks.loopToFree) {
                     // otherwise it get freed
                     depconf.loopBlocks.loopToFree = NULL;
                     DEBUG_PRINTF_E(PRINT_LOGGING_HG_LOOPS,"Didn't we have a leak here ?\n");//TODO
                  }
               }

            } else if(0) {
               IntervalUnion * ivu=getBaseSpinIvu(sb,True,depconf.loopBlocks.loop);
               HG_(freeIvUnion)(ivu);
            }
         }
      }
      
      // Debug: --show-bb=0x...
      if( hg_cfg__clo_show_bb != 0 &&
          get_sb_index( sb, hg_cfg__clo_show_bb ) != -1 ) {
         VG_(printf)("\n^^^^^^^^^^^^^^^^ %s\n", (looking_ahead?"(ahead)":"(now)"));
         VG_(printf)("IRSB Input:\n");
         ppIRSB( bbIn );
         VG_(printf)("Superblock:\n");
         pp_SB( sb, 0 );
         if( looking_ahead )
            VG_(printf)("=========================================\n\n");
         else
            debug_print_bbOut = True;
      }
   }
   
   /////////////////////////////////////////////////////////////////
   // Instrument spin reads
   
   else if ( LIKELY(mode == HgL_Instrument_Statement ) ) {
      static Addr curAddr=0;
      static Addr nextAddr=0;
      if( stmt->tag == Ist_IMark ) {
         curAddr = stmt->Ist.IMark.addr;
         nextAddr = stmt->Ist.IMark.addr+stmt->Ist.IMark.len;
      }
      if( stmt->tag == Ist_IMark ) {
         Addr iaddr = stmt->Ist.IMark.addr;
         SpinProperty * sp;
         if( spin_reading ) {
            IRDirty* di;
            di = unsafeIRDirty_0_N( 0, "stop_spin_reading", &stop_spin_reading, mkIRExprVec_0() );
            addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
            spin_reading = False;
         }

         if( VG_(lookupFM)(map_spinreads, NULL, (UWord*)&sp, (UWord)iaddr ) ) {
            IRDirty* di;
            IRExpr ** argv = mkIRExprVec_1( mkIRExpr_HWord( (HWord)sp ) );
            di = unsafeIRDirty_0_N( 1,
                  "start_spin_reading", &start_spin_reading,
                  argv );
            addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
            spin_reading = True;
         }

         if(HG_(clo_detect_mutex) && __LOOP_ALGO_ID==1 && 0000 ){ // TODO : deactivated
            IntervalUnion * loop;
            if( VG_(lookupFM)(map_loop_ends,NULL,(UWord*)(&loop),(UWord)iaddr) ){
               IRDirty* di       = NULL;
               IRExpr ** argv;
               Bool inLoop=0;

               argv = mkIRExprVec_3( mkIRExpr_HWord( (HWord)iaddr ), mkIRExpr_HWord( (HWord)inLoop ), mkIRExpr_HWord( (HWord)loop ) );

               tl_assert(depconf.loopBlocks.loopToFree != loop);
               di = unsafeIRDirty_0_N( 3,
                                       "spin_follow_exitif",
                                       VG_(fnptr_to_fnentry)( &spin_follow_exitif /*TODO*/ ),
                                       argv );
               //di->guard = guard;
               addStmtToIRSB( bbOut, IRStmt_Dirty(di) );

               if(PRINT_LOGGING_HG_LOOPS) {
                  ppIRStmt(IRStmt_Dirty(di));
                  VG_(printf)("\n\tDst in loop ? %s\n",inLoop?"true":"false");
               }
            }
         }
      }
      if( HG_(clo_detect_mutex) && stmt->tag == Ist_Exit &&(__LOOP_ALGO_ID==1) ) {
         IntervalUnion * loop;
         IRExpr * guard = stmt->Ist.Exit.guard;
         IRConst * conDst = stmt->Ist.Exit.dst;
         Addr dst=value_of(IRExpr_Const(conDst));
         if(vge->base[vge->n_used-1]+vge->len[vge->n_used-1]!=nextAddr) {
            // if it wasn't the last instruction of the SB.
            if( VG_(lookupFM)(map_loop_ends,NULL,(UWord*)(&loop),(UWord)nextAddr) ){
               // if jump not taken, we exit Spin
               tl_assert2(0,__AT__ " TODO : instrument the jump (if not taken -> exit)\n"); // TODO
            }
         }
         if( VG_(lookupFM)(map_loop_ends,NULL,(UWord*)(&loop),(UWord)dst) ){
            IRDirty* di       = NULL;
            IRExpr ** argv;
            Bool inLoop=0;
            IRExpr * inLoopExpr;
            inLoopExpr = guard;/* if the jump was taken, we will stay in loop */
            //inLoopExpr = mkIRExpr_HWord( (HWord)inLoop  );

            if(!COMMITABLE) VG_(printf)("instrument EXIT from H%lx prev to #%lx \n",HG_(ivUnionHash)(loop),dst);

            { // convert 1 bit to 32 bit (must sizeof(HWord)==4 ?)
               IRTemp tmp;
               IRStmt * convStmt;

               tmp=newIRTemp(bbOut->tyenv,Ity_I1);
               convStmt = IRStmt_WrTmp(tmp,IRExpr_Unop(Iop_Not1, inLoopExpr));// we stay in loop if jump is NOT taken
               addStmtToIRSB( bbOut, convStmt);
               inLoopExpr = IRExpr_RdTmp(tmp);

               if(sizeof(HWord)==4) {
				   tmp=newIRTemp(bbOut->tyenv,Ity_I32);
				   convStmt = IRStmt_WrTmp(tmp,IRExpr_Unop(Iop_1Uto32,inLoopExpr ));
				   addStmtToIRSB( bbOut, convStmt);
				   inLoopExpr = IRExpr_RdTmp(tmp);
               } else if(sizeof(HWord)==8) {
				   tmp=newIRTemp(bbOut->tyenv,Ity_I64);
				   convStmt = IRStmt_WrTmp(tmp,IRExpr_Unop(Iop_1Uto64,inLoopExpr ));
				   addStmtToIRSB( bbOut, convStmt);
				   inLoopExpr = IRExpr_RdTmp(tmp);
               } else tl_assert(0);

               if(!(COMMITABLE)) {
                  VG_(printf)("ppIRStmt(convStmt): nextAddr=%p ",(void*)nextAddr);
                  ppIRStmt(convStmt);
                  VG_(printf)("\n");
                  //ppIRStmt(IRStmt_Dirty(di));
               }
            }

            tl_assert(depconf.loopBlocks.loopToFree != loop);

            argv = mkIRExprVec_3( mkIRExpr_HWord( (HWord)dst ), inLoopExpr, mkIRExpr_HWord( (HWord)loop ) );

            //depconf.loopBlocks.loopToFree = NULL; // TODO : leak
            tl_assert(depconf.loopBlocks.loopToFree != loop);
            di = unsafeIRDirty_0_N( 3,
                                    "spin_follow_exitif",
                                    VG_(fnptr_to_fnentry)( &spin_follow_exitif /*TODO*/ ),
                                    argv );
            //di->guard = guard;
            addStmtToIRSB( bbOut, IRStmt_Dirty(di) );

            if(PRINT_LOGGING_HG_LOOPS) {
               ppIRStmt(IRStmt_Dirty(di));
               VG_(printf)("\n\tDst in loop ? %s\n",inLoop?"true":"false");
            }
         }
      }
      /**
       * TODO SCHMALTZ :
       * Finish this.
       */
      if( HG_(clo_detect_mutex) && stmt->tag == Ist_Exit &&(__LOOP_ALGO_ID==0) ) {
         IRConst * conDst = stmt->Ist.Exit.dst;
         IRExpr * guard = stmt->Ist.Exit.guard;
         // if con isn't in the range of spin

         if(inBlockWithSpins)
         {
            IRDirty* di       = NULL;
            IRExpr ** argv;
            Bool inLoop=(HG_(ivUnionContains)(depconf.loopBlocks.loop,(HWord)value_of(IRExpr_Const(conDst)))!=NULL);//loop_contains(&depconf.loopBlocks.loop,value_of(IRExpr_Const(conDst)));
            //mkIRExpr_HWord( (HWord)0 ),
            //argv = mkIRExprVec_2( guard, IRExpr_Const(conDst) );

            tl_assert(HG_(ivUnion_cntParts)(depconf.loopBlocks.loop)!=0);
            //VG_(printf)("###%d\n",VG_(sizeFM)(depconf.loopBlocks.loop->collect));

            argv = mkIRExprVec_3( IRExpr_Const(conDst), mkIRExpr_HWord( (HWord)inLoop ), mkIRExpr_HWord( (HWord)depconf.loopBlocks.loop ) );
            if(depconf.loopBlocks.loop==depconf.loopBlocks.loopToFree) {
               // otherwise it get freed
               depconf.loopBlocks.loopToFree = NULL;
               DEBUG_PRINTF_E(PRINT_LOGGING_HG_LOOPS,"Didn't we have a leak here ?\n");//TODO
            }
            tl_assert(depconf.loopBlocks.loopToFree != depconf.loopBlocks.loop);
            di = unsafeIRDirty_0_N( 3,
                                    "spin_follow_exitif",
                                    VG_(fnptr_to_fnentry)( &spin_follow_exitif /*TODO*/ ),
                                    argv );
            di->guard = guard;
            addStmtToIRSB( bbOut, IRStmt_Dirty(di) );

            if(PRINT_LOGGING_HG_LOOPS) {
               ppIRStmt(IRStmt_Dirty(di));
               VG_(printf)("\n\tDst in loop ? %s\n",inLoop?"true":"false");
            }
         }
      }
   } // END HgL_Instrument_Statement
   else if ( UNLIKELY(mode == HgL_Post_Instrumentation ) ) {
      if( spin_reading ) {
         IRDirty* di;
         di = unsafeIRDirty_0_N( 0, "stop_spin_reading", &stop_spin_reading, mkIRExprVec_0() );
         addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
         spin_reading = False;
      }
      
      if( debug_print_bbOut ) {
         VG_(printf)("IRSB Output:\n");
         ppIRSB(bbOut);
         VG_(printf)("=========================================\n\n");
         debug_print_bbOut = False;
      }
      
      if(HG_(clo_detect_mutex) && inBlockWithSpins)
      {
         IRDirty* di       = NULL;
         IRExpr ** argv;
         //mkIRExpr_HWord( (HWord)0 ),
         //argv = mkIRExprVec_2( guard, IRExpr_Const(conDst) );

         switch(bbOut->jumpkind) {
            case Ijk_ClientReq:
            case Ijk_Sys_syscall:
            case Ijk_Sys_sysenter:
            case Ijk_Yield:
               /* These things (almost) never exit the spin loop */
               break;
            case Ijk_Call:
            case Ijk_Ret:
               break;
            case Ijk_Boring: {
               if(__LOOP_ALGO_ID==0) {
                  tl_assert(depconf.loopBlocks.loop); //depconf.loopBlocks.loopToFree=NULL;
                  depconf.loopBlocks.loopToFree=NULL;
                  argv = mkIRExprVec_3( bbOut->next, mkIRExpr_HWord( (HWord)31415 )
                        , mkIRExpr_HWord( (HWord)depconf.loopBlocks.loop ) );
                  tl_assert(depconf.loopBlocks.loopToFree != depconf.loopBlocks.loop);
                  di = unsafeIRDirty_0_N( 3,
                                          "spin_follow_exitif",
                                          VG_(fnptr_to_fnentry)( &spin_follow_exitif /*TODO*/ ),
                                          argv );
                  addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
                  if(PRINT_LOGGING_HG_LOOPS) {
                     ppIRStmt(IRStmt_Dirty(di));
                     VG_(printf)("\n\t-next\n");
                  }
               } if(__LOOP_ALGO_ID==1) {
                  IntervalUnion * loop;
                  Addr dst=value_of(bbOut->next); // TODO : indirect expressions
                  if( VG_(lookupFM)(map_loop_ends,NULL,(UWord*)(&loop),(UWord)dst) ){
                     Bool inLoop=0;
                     IRExpr * inLoopExpr;
                     di       = NULL;
                     inLoopExpr = mkIRExpr_HWord( (HWord)inLoop  );

                     tl_assert(depconf.loopBlocks.loopToFree != loop);

                     argv = mkIRExprVec_3( mkIRExpr_HWord( (HWord)dst ), inLoopExpr, mkIRExpr_HWord( (HWord)loop ) );

                     //depconf.loopBlocks.loopToFree = NULL; // TODO : leak
                     tl_assert(depconf.loopBlocks.loopToFree != loop);
                     di = unsafeIRDirty_0_N( 3,
                                             "spin_follow_exitif",
                                             VG_(fnptr_to_fnentry)( &spin_follow_exitif /*TODO*/ ),
                                             argv );
                     //di->guard = guard;
                     addStmtToIRSB( bbOut, IRStmt_Dirty(di) );

                     if(PRINT_LOGGING_HG_LOOPS) {
                        ppIRStmt(IRStmt_Dirty(di));
                        VG_(printf)("\n\tDst in loop ? %s\n",inLoop?"true":"false");
                     }

                  }
               }
            } break;
            default:
               // nothing
               break;
         }
      }


      return True;
   }
   
   
   else {
      // Should not happen
	  VG_(printf)("mode=%d\n",mode);
	  tl_assert(*((char*)NULL)=7); // sigseg
      tl_assert(0);
   }

   return True;
}

/*----------------------------------------------------------------*/
/*--- External Interface                                       ---*/
/*----------------------------------------------------------------*/

inline
ThreadLoopExtends* hg_loops__create_ThreadLoopExtends ( void )
{
   return mk_ThrLoopExtends(NULL);
}

inline
void* hg_loops__get_ThreadLoopExtends_opaque ( ThreadLoopExtends* thr )
{
   return thr->opaque;
}

inline
void hg_loops__set_ThreadLoopExtends_opaque ( ThreadLoopExtends* thr, void* opaque )
{
   thr->opaque = opaque;
}

/*----------------------------------------------------------------*/
/*--- Setup / Output statistics                                ---*/
/*----------------------------------------------------------------*/

void hg_loops_init ( ThreadLoopExtends* (*get_current_LoopExtends) (void) ) {
   tl_assert(get_current_LoopExtends);
   main_get_LoopExtends = get_current_LoopExtends;
   
   tl_assert(map_superblocks == NULL);
   map_superblocks = VG_(newFM)( HG_(zalloc), "hg.loops.init.1" ,HG_(free), NULL/*unboxed Word cmp*/);
   tl_assert(map_superblocks != NULL);
   
   tl_assert(map_spinreads == NULL);
   map_spinreads = VG_(newFM)(HG_(zalloc), "hg.loops.inst.2", HG_(free), NULL /* unboxed Word cmp */);
   tl_assert(map_spinreads != NULL);

   tl_assert(map_unpredictable_calls == NULL);
   map_unpredictable_calls = VG_(newFM)(HG_(zalloc), "hg.loops.inst.3", HG_(free), NULL /* unboxed Word cmp */);
   tl_assert(map_unpredictable_calls != NULL);
   
   tl_assert(map_loop_ends == NULL);
   map_loop_ends = VG_(newFM)(HG_(zalloc), "hg.loops.inst.4", HG_(free), NULL /* unboxed Word cmp */);
   tl_assert(map_loop_ends != NULL);

   tl_assert(hg_loops_lastSpinProperty == NULL);
   hg_loops_lastSpinProperty = VG_(newFM)(HG_(zalloc), "hg.loops.inst.5", HG_(free), NULL /* unboxed Word cmp */);
   tl_assert(hg_loops_lastSpinProperty != NULL);

   init_varmaps();
}

void hg_loops_shutdown ( Bool show_stats ) {
   if( hg_cfg__clo_show_control_flow_graph ||
       hg_cfg__clo_verbose_control_flow_graph ||
       hg_cfg__clo_show_spin_reads ||
       show_stats ) {
      VG_(printf)("%s","<<< BEGIN hg_loops stats >>>\n");
      VG_(printf)("   %s %d\n","spin search depth:", HG_(clo_control_flow_graph) );
      VG_(printf)("   %s %lu out of %lu loops\n","spins found:", stats__spins_found, stats__loops_found);
      VG_(printf)("   %s","<<< BEGIN spin reads >>>\n");
      pp_map_spinreads( 6 );
      VG_(printf)("   %s","<<< END spin reads >>>\n");
      VG_(printf)("%s","<<< END hg_loops stats >>>\n");
   }

   if(!(COMMITABLE)){
      SB* curSB=admin_sblocks;
      Dot * d=dot_new();

      while(curSB) {
         Bool isOk=False;
         //isOk|=sb_intersectRange( curSB, 0x8048874,0x080488a2 );

         //isOk|=sb_matchFunc("my_mutex_lock*",curSB);

         //isOk|=sb_matchFunc("*falseSpin*", curSB);
         //isOk|=sb_matchFunc("*trueSpin*", curSB);
         isOk|=sb_matchFunc("*trueSpin2*", curSB);

         //isOk|=sb_matchFunc("pthread_mutex_lock*", curSB);
         //isOk|=sb_matchFunc("*lll_lock_wait*", curSB);

         //isOk|=VG_(string_match)("*my_mutex_*", label);
         //isOk|=VG_(string_match)("*pthread_mutex_*", label);
         if(isOk){
            dot_addSB(d,curSB);
         }

         curSB=curSB->admin;
      }

      {
         Addr64 spin=0;
         VG_(initIterFM)(map_spinreads);
         while( VG_(nextIterFM)( map_spinreads, (UWord*)&spin, NULL ) ) {
            dot_addSpin(d,spin);
         }
         VG_(doneIterFM)( map_spinreads );
      }

      DEBUG_PRINTF_E("select.dot",({
         VG_(printf)("//filename:SELECT\n");
         dot_pp(d);
      }));
      dot_free(d);

   }


   {

      SpinProperty * sp=NULL;
      void* spin=NULL;
      //WordFM* fm = map_spinreads;
      //VG_(printf)("%p\n",fm);
      //VG_(printf)("%p %p %p\n",fm,&spin,&sp);
      VG_(initIterFM)(map_spinreads);
      while( VG_(nextIterFM)( map_spinreads, (UWord*)&spin, (UWord*)&sp ) ) {
         UWord h=0;
         if(sp&&sp->codeBlock) {
            h=HG_(ivUnionHash)(sp->codeBlock);
         }
         VG_(printf)("%p;%p;%lx\n",spin,sp,h);
      }
      VG_(doneIterFM)( map_spinreads );
   }

#if !defined(NDEBUG)
   if(PRINT_MEMLEAK){  // Memory leaks checking
      if(varmap_temps) VG_(deleteFM)( varmap_temps, del_Variable, NULL );
      if(varmap_varset) VG_(deleteFM)( varmap_varset, del_Variable, NULL );
      if(map_superblocks) VG_(deleteFM)( map_superblocks, NULL, (void*)HG_(free) ); //mk_SB
      // needed in pp_map_spinreads above
      if(map_spinreads) VG_(deleteFM)( map_spinreads, NULL, NULL );
      if(map_unpredictable_calls) VG_(deleteFM)( map_unpredictable_calls, NULL, (void*)HG_(free) ); //mk_UC
   }
#endif
}
