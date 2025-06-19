/*
 * hg_dependency.c
 *
 * Sub-module for hg_loops.c containing algorithms for spin-read detection.
 *
 * (c) 2009-2010 Univercity of Karlsruhe, Germany
 */


/* TODO
 * TODO

isRecurse (load) ->
build_Tree_stores ->
treeNode_add_store

-------------------------

ST( a ) = LD( b )
t5 = LD( a )
ST( t5 ) = LD( b )

t3 = LD( $x ) + 5
t5 = LD( t3 )
t8 = LD( t5 )
ST( t3 ) = t8
 TODO */

/***
 * Currently useless code
 */
#define SKIP_UNUSED 1

#define USE_INTERPRETER_FOR_RECURSE !(SKIP_UNUSED)
#define USE_VIRTUAL_MEMORY_V1 !(SKIP_UNUSED)
#define USE_VIRTUAL_MEMORY_V2 (0||USE_INTERPRETER_FOR_RECURSE)
#define USE_BlockAllocator (USE_VIRTUAL_MEMORY_V2||USE_VIRTUAL_MEMORY_V1)
#define USE_INTERPRETER (0||USE_INTERPRETER_FOR_RECURSE||USE_VIRTUAL_MEMORY_V2||USE_VIRTUAL_MEMORY_V1)

/***
 * Usefull code
 * (just done for checking dependencies between "packages")
 */

#define USE_DEPENDENCY_CHECKER 1
#define USE_TREE_ITERATOR (USE_DEPENDENCY_CHECKER)


#include "hg_dependency.h"
#include "hg_logging.h"


#include "pub_tool_libcbase.h"
#include "../coregrind/pub_core_redir.h"   /* VG_(redir_do_lookup) */
#include "../VEX/priv/ir/iropt.h" /* do_iropt_BB */
#include <alloca.h>
//#include <gdefs.h> //guest_x86_spechelper
//#include "../VEX/priv/guest-x86/gdefs.h"


// TODO : delete this ; this was just copied as "memo" of WordFM usage

#include "pub_tool_xarray.h"
#include "pub_tool_oset.h"
#include "pub_tool_hashtable.h"
extern XArray* VG_(newXA) ( void*(*alloc_fn)(HChar*,SizeT),
                            HChar* cc,
                            void(*free_fn)(void*),
                            Word elemSzB );

extern OSet* VG_(OSetWord_Create)       ( OSetAlloc_t alloc, HChar* ec,
                                          OSetFree_t free );


extern VgHashTable VG_(HT_construct) ( HChar* name );

WordFM* VG_(newFM) ( void* (*alloc_nofail)( HChar*, SizeT ),
                     HChar* ccc,
                     void  (*dealloc)(void*),
                     Word  (*kCmp)(UWord,UWord) );

Bool VG_(addToFM) ( WordFM* fm, UWord k, UWord v );

Bool VG_(delFromFM) ( WordFM* fm,
                      /*OUT*/UWord* oldK, /*OUT*/UWord* oldV, UWord key );

Bool VG_(lookupFM) ( WordFM* fm,
                     /*OUT*/UWord* keyP, /*OUT*/UWord* valP, UWord key );
void VG_(deleteFM) ( WordFM* fm, void(*kFin)(UWord), void(*vFin)(UWord) );

void VG_(initIterFM) ( WordFM* fm );
Bool VG_(nextIterFM) ( WordFM* fm, /*OUT*/UWord* pKey, /*OUT*/UWord* pVal );
void VG_(doneIterFM) ( WordFM* fm );
// END:MEMO


typedef struct _IrsbTree {
   IRSB* irsb;
   // maps tmp number to statement index where it was written
   // tstruct[tmp] == statementIndex
   Int * tstruct;

   // maps register offset to statement index where it was written to for last time
   WordFM * regLoopDefs;

   /************
    * These datas were used by the Dependency checker
    */
   // maps store/load/if-statement index to his AddressProp
   WordFM * stmtAddressProps;
   // same for loads (the mapping works because one statement cannot have more than one load in flat IRSB)
   WordFM * loadIdx;

   Bool wasSimplified;
} _IrsbTree;

/*----------------------------------------------------------------*/
/*--- DEBUGGING                                          ---*/
/*----------------------------------------------------------------*/

#ifndef NDEBUG
   #define INLINE
#else
   #define INLINE inline
#endif


#define ISRECURSE_MAX_DEPTH 300


static
void irsb_optimiseAtomicAlias( IRSB * irsb );

/*----------------------------------------------------------------*/
/*--- UTILS                                                 ---*/
/*----------------------------------------------------------------*/


/**
 * Prints n spaces
 */
static void space ( Int n )
{
   Int  i;
   Char spaces[512+1];
   tl_assert(n >= 0 && n < 512);
   if (n == 0)
      return;
   for (i = 0; i < n; i++)
      spaces[i] = ' ';
   spaces[i] = 0;
   tl_assert(i < 512+1);
   VG_(printf)("%s", spaces);
}

static
Addr value_of ( IRConst* con ) {
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
}

static
IRConst* createConst ( IRConstTag typ, Addr value ) {

   switch ( typ ) {
      case Ico_V128:
         return IRConst_V128(value);
      case Ico_U1:
         return IRConst_U1(value);
      case Ico_U8:
         return IRConst_U8(value);
      case Ico_U16:
         return IRConst_U16(value);
      case Ico_U32:
         return IRConst_U32(value);
      case Ico_U64:
      default:
         return IRConst_U64(value);
   }
}

static
#ifdef NDEBUG
inline
#endif
IRStmt * IRStmt_Comment(HChar * comment, Bool copy, ...)
{
#ifdef NDEBUG
   return IRStmt_NoOp();
#else
   IRDirty * di;
   if(copy) {
      static HChar buf[1024];
      HChar * tmp;
      va_list vargs;
      va_start(vargs, copy);
      VG_(vsnprintf)(buf,sizeof(buf),comment,vargs);
      va_end(vargs);
      tmp=LibVEX_Alloc(VG_(strlen)(buf)+1);
      VG_(strcpy)(tmp,buf);
      comment=tmp;
   }

   di = unsafeIRDirty_0_N( 0,
                           comment,
                           (void*)1,
                           mkIRExprVec_0() );
   return IRStmt_Dirty(di);
#endif
}


/**
 * Return
 *    0 if w1==w2
 *    1 if w1>w2
 *   -1 if w1<w2
 */
static Word cmp_unsigned_Words ( UWord w1, UWord w2 ) {
   if (w1 < w2) return -1;
   if (w1 > w2) return 1;
   return 0;
}

/**
 * outVar should be a struct*  having a outVar->next pointer.
 */
#define ALLOCASTACK_ALLOC_PUSH(outVar) {\
   void * _vNext=outVar;\
   outVar=alloca(sizeof(*outVar));\
   outVar->next=_vNext;\
}
#define ALLOCASTACK_POP(outVar) {\
   outVar=outVar->next;\
}


/*----------------------------------------------------------------*/
/*--- Block allocator                                         ---*/
/*----------------------------------------------------------------*/

#if USE_BlockAllocator

typedef struct s_BlockAllocator_list {
      Char * memBlock;
      UInt curPos;
      void * next;
} _BlockAllocator_list;

typedef struct s_BlockAllocator {
   _BlockAllocator_list list;
   UInt allocSize;
} BlockAllocator;

static
void blockAllocator_new(BlockAllocator * ba, UInt allocSize)
{
   tl_assert(allocSize>=0xFF);
   ba->list.next=NULL;
   ba->list.memBlock=HG_(zalloc)("newBlockAllocator",allocSize); ba->list.curPos=0;
   ba->allocSize=allocSize;
}

static
void * blockAllocator_alloc(BlockAllocator * ba,UInt size)
{
//HChar* cc
   void* res;
   if(ba->list.curPos+size>ba->allocSize)
   {
      Char * newBlock=HG_(zalloc)("blockAllocator_alloc",ba->allocSize);
      UInt i;
      for(i=0;i<sizeof(ba->list);i++)
         newBlock[i]=((Char*)(&ba->list))[i];
      //((_BlockAllocator_list)(*newBlock))=ba->list;

      ba->list.next=newBlock;
      ba->list.curPos=sizeof(ba->list);
      ba->list.memBlock=newBlock;
      tl_assert(ba->list.curPos+size<=ba->allocSize);
   }
   res=&(ba->list.memBlock[ba->list.curPos]);
   ba->list.curPos+=size;
   return res;
}


static
void __blockAllocator_list_free(_BlockAllocator_list * lst)
{
   if(lst->next)
   {
      __blockAllocator_list_free((_BlockAllocator_list*)lst->next);
   }
   HG_(free)(lst->memBlock);
}

static
void blockAllocator_free(BlockAllocator * ba)
{
   __blockAllocator_list_free(&(ba->list));
   ba->allocSize=0;
}

/**
 * Allocates the BlockAllocator structure in itself.
 */
static
BlockAllocator * blockAllocator_new2(UInt allocSize)
{
   BlockAllocator b;
   Int i;
   Char * newBlock;
   blockAllocator_new(&b,allocSize);
   newBlock=blockAllocator_alloc(&b,sizeof(b));
   for(i=0;i<sizeof(b);i++)
      newBlock[i]=((Char*)(&b))[i];
   return (BlockAllocator*)newBlock;
}
#endif


/*----------------------------------------------------------------*/
/*--- TREE ACCESS                                          ---*/
/*----------------------------------------------------------------*/

static INLINE
Int treeNode_tmp(IrsbTree * tree, IRTemp tmp)
{
   Int res;
   tl_assert(tree);
   tl_assert(tree->irsb);
   if(!(tmp>=0&&tmp<tree->irsb->tyenv->types_used)) {
      ppIRSB(tree->irsb);
      VG_(printf)("In treeNode_tmp, tmp was %d not in [0,%d[.",tmp,tree->irsb->tyenv->types_used);
      tl_assert(0);
   }
   res=tree->tstruct[tmp];
   if(!(res>=0&&res<tree->irsb->stmts_used)) {
      ppIRSB(tree->irsb);
      VG_(printf)("In treeNode_tmp, result was %d not in [0,%d[.  Tmp was t%d",res,tree->irsb->stmts_used,tmp);
      tl_assert(0);
   }
   return res;
}

/*static INLINE
void treeNode_splitRegister(UInt registerWithOffset, Int * offset,IRType * ty)
{
   UInt minoff, maxoff,sz;
   minoff = (registerWithOffset >> 16)&0xFFFF;
   maxoff = (registerWithOffset)&0xFFFF;

}*/


static INLINE
UWord _treeNode_regKey(Int offset,IRType ty)
{
   //tl_assert(ty>Ity_INVALID&&ty<Ity_INVALID+128);
   /* offset should fit in 16 bits. */
   UInt minoff = offset;
   UInt maxoff = minoff + sizeofIRType(ty) - 1;
   tl_assert((minoff & ~0xFFFF) == 0);
   tl_assert((maxoff & ~0xFFFF) == 0);
   return (minoff << 16) | maxoff;
}

static INLINE
Int treeNode_regOffset(UWord regWithOffset)
{
   return (regWithOffset>>16)&0xFFFF;
}

static INLINE
Int treeNode_register(IrsbTree * tree, Int offset,IRType ty)
{
   Int stmtIdx;
   UWord key=_treeNode_regKey(offset, ty );
   if( !VG_(lookupFM)( tree->regLoopDefs, NULL, (UWord*)&stmtIdx, (UWord)key ) ) {
      return -1;
   } else {
      return stmtIdx;
   }
}

static INLINE
void treeNode_addRegister(IrsbTree * tree, Int offset,IRType ty, Int stmtIdx)
{
   UWord key=_treeNode_regKey(offset, ty );
   //VG_(printf)("treeNode_addRegister r:");ppIRType(ty);VG_(printf)("(%d) = %d\n", offset, stmtIdx);
   VG_(addToFM)( tree->regLoopDefs, key, stmtIdx);
}

static INLINE
Bool treeNode_delRegisterWithOffset(IrsbTree * tree, UWord regWithOffset)
{
   return VG_(delFromFM) ( tree->regLoopDefs, NULL, NULL, regWithOffset );
}

/*
static INLINE
void treeNode_regInitIter( IrsbTree * tree )
{
      VG_(initIterFM) ( tree->regLoopDefs2 );
}
static INLINE
Bool treeNode_regNextIter( IrsbTree * tree, Int offset,IRType ty, Int stmtIdx )
{
   return VG_(nextIterFM) ( tree->regLoopDefs2, &registerOffset, &stmtIndex );
}*/

/*----------------------------------------------------------------*/
/*--- TREE PRINT                                          ---*/
/*----------------------------------------------------------------*/

typedef struct s_ppTreeHdl {
   UInt * usedStmts; // anti recursion
   UInt depth;
   UInt maxDepth;
   Char tabSize;
   Bool singleLine;
   Bool followGetPutLoops;
   Bool printTmpNames;

   UInt __align__;
} _ppTreeHdl;

// DECLARATION
static
void _ppTree_Stmt (IrsbTree * tree, Int root, Int d, _ppTreeHdl * hdl);
static
void _ppTree_expr ( IrsbTree * tree, IRExpr * e, Int d, _ppTreeHdl * hdl );


static
void _ppTree_expr ( IrsbTree * tree, IRExpr * e, Int d, _ppTreeHdl * hdl ) {
   const Int sp=hdl->tabSize;
   //VG_(printf)("\%p\n",hdl);
   //VG_(printf)("%d\n",hdl->followGetPutLoops);
   switch( e->tag ) {
      case Iex_CCall: {
         ppIRExpr(e); //not implemented
         break;/*
         int numArgs,i;
         IRExpr ** args=shallowCopyIRExprVec(e->Iex.CCall.args);
         for (i = 0; e->Iex.CCall.args[i] != NULL; i++) {
            IRExpr * arg=e->Iex.CCall.args[i];
            args[i]=arg;
         }*/
      } break;
      case Iex_Const: {
         space(d);ppIRExpr(e);if(!hdl->singleLine)VG_(printf)("\n");
      } break;
      case Iex_Get: {
         Int stmtIdx;
#ifdef __amd64
         /*
          * Compiler bug workaround for amd64
          */
         _ppTreeHdl * hdlCp=hdl;
         //VG_(printf)("\n%d %p\n",0,hdl);
         //VG_(printf)("%d\n",hdl->followGetPutLoops);
         VG_(printf)("",&hdlCp); // avoid compiler reordering
#endif
         stmtIdx = treeNode_register(tree,e->Iex.Get.offset,e->Iex.Get.ty);
#ifdef __amd64
         VG_(printf)("",&hdlCp); // avoid compiler reordering
         hdl=hdlCp;
         //VG_(printf)("\n%d %p\n",stmtIdx,hdl);
         //VG_(printf)("%d\n",hdl->followGetPutLoops);
#endif
         if( (stmtIdx<0) || !(hdl->followGetPutLoops) ) {
            space(d);ppIRExpr(e);if(!hdl->singleLine)VG_(printf)("\n");
         } else {
            //ppTree_Stmt(tree,stmtIdx,d);
            IRStmt * st=tree->irsb->stmts[stmtIdx];
            hdl->usedStmts[stmtIdx]++;
            if(hdl->usedStmts[stmtIdx]>3) {
               space(d);ppIRExpr(e);
               VG_(printf)("*recurse:%d ",hdl->usedStmts[stmtIdx]);
            } else {
               _ppTree_expr(tree,st->Ist.Put.data,d,hdl);
            }
         }
      } break;
      case Iex_GetI: {
         space(d);ppIRExpr(e);
      } break;
      case Iex_RdTmp: {
         /* */
         _ppTree_Stmt(tree,treeNode_tmp(tree,e->Iex.RdTmp.tmp), d,hdl);
      } break;
      case Iex_Mux0X: {
         space(d);
         VG_(printf)("Mux0X(");if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Mux0X.cond,d+sp,hdl);
         space(d);VG_(printf)( "," );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Mux0X.expr0,d+sp,hdl);
         space(d);VG_(printf)( "," );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Mux0X.exprX,d+sp,hdl);
         VG_(printf)(")");if(!hdl->singleLine) VG_(printf)( "\n" );
      } break;
      case Iex_Qop: {
         space(d);
         //VG_(printf)("Qop(");
         ppIROp(e->Iex.Qop.op);
         space(d);VG_(printf)( "(" );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Qop.arg1,d+sp,hdl);
         space(d);VG_(printf)( "," );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Qop.arg2,d+sp,hdl);
         space(d);VG_(printf)( "," );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Qop.arg3,d+sp,hdl);
         space(d);VG_(printf)( "," );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Qop.arg4,d+sp,hdl);
         space(d);VG_(printf)( ")" );if(!hdl->singleLine) VG_(printf)( "\n" );

      } break;
      case Iex_Triop: {
         space(d);
         //VG_(printf)("Qop(");
         ppIROp(e->Iex.Triop.op);
         VG_(printf)( "(" );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Triop.arg1,d+sp,hdl);
         VG_(printf)( "," );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Triop.arg2,d+sp,hdl);
         VG_(printf)( "," );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Triop.arg3,d+sp,hdl);
         VG_(printf)( ")" );if(!hdl->singleLine) VG_(printf)( "\n" );
      } break;
      case Iex_Binop: {
         space(d);
         ppIROp(e->Iex.Binop.op);
         VG_(printf)( "(" );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Binop.arg1,d+sp,hdl);
         space(d);VG_(printf)( "," );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Binop.arg2,d+sp,hdl);
         space(d);VG_(printf)( ")" );if(!hdl->singleLine) VG_(printf)( "\n" );
      } break;
      case Iex_Unop: {
         space(d);
         //VG_(printf)("Qop(");
         ppIROp(e->Iex.Unop.op);
         VG_(printf)( "(" );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Unop.arg,d+sp,hdl);
         space(d);
         VG_(printf)( ")" );if(!hdl->singleLine) VG_(printf)( "\n" );
      } break;
      case Iex_Load: {
         space(d);
         VG_(printf)( "LD%s:", e->Iex.Load.end==Iend_LE ? "le" : "be" );
         ppIRType(e->Iex.Load.ty);
         VG_(printf)( "(" );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,e->Iex.Load.addr,d+sp,hdl);
         space(d);
         VG_(printf)( ")" );if(!hdl->singleLine) VG_(printf)( "\n" );
      } break;
      default:
         tl_assert(0);
   }
}

static
void _ppTree_Stmt (IrsbTree * tree, Int root, Int d, _ppTreeHdl * hdl)
{
   IRSB* bbIn = tree->irsb;
   IRStmt* st;
   const Int sp=hdl->tabSize;
   tl_assert(root>=0);
   tl_assert(root<bbIn->stmts_used);
   st = bbIn->stmts[root];

   if( hdl->depth>hdl->maxDepth ) {
      VG_(printf)( " * recurse\n" );
      return; // RECUSIVE
   }

   hdl->depth++;
   hdl->usedStmts[root]++;

   switch (st->tag) {
      case Ist_NoOp:
      case Ist_AbiHint:
      case Ist_PutI:
      case Ist_Dirty:
         /* None of these can contain anything of interest */
         ppIRStmt(st); //not implemented
         break;

      case Ist_MBE:

      case Ist_IMark:
         ppIRStmt(st); //not implemented
         break;

      case Ist_Store: {
         space(d);
         VG_(printf)( "ST%s:", st->Ist.Store.end==Iend_LE ? "le" : "be" );
         VG_(printf)( "(" );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,st->Ist.Store.addr,d+sp,hdl);
         space(d);VG_(printf)( ") = (" );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,st->Ist.Store.data,d+sp,hdl);
         space(d);VG_(printf)( ")" );if(!hdl->singleLine) VG_(printf)( "\n" );

      } break;

      case Ist_Put: {
         ppIRStmt(st); //not implemented
      } break;

      case Ist_WrTmp: {
         //IRExpr * data = st->Ist.WrTmp.data;
         //IRTemp tmp = st->Ist.WrTmp.tmp;
         if(hdl->printTmpNames) {
            VG_(printf)("[t%d]",st->Ist.WrTmp.tmp);
         }
         //VG_(printf)("\n%p\n",hdl);
         //VG_(printf)("%d\n",hdl->followGetPutLoops);
         _ppTree_expr(tree,st->Ist.WrTmp.data,d,hdl);
      } break;

      case Ist_Exit: {
         IRExpr *guard = st->Ist.Exit.guard;

         tl_assert( guard->tag==Iex_RdTmp );

         //ppIRStmt(st);
         space(d);VG_(printf)( "if (" );if(!hdl->singleLine) VG_(printf)( "\n" );
         _ppTree_expr(tree,guard,d+sp,hdl);
         space(d);VG_(printf)( ") goto {");
         ppIRJumpKind(st->Ist.Exit.jk);
         VG_(printf)("} ");
         ppIRConst(st->Ist.Exit.dst);
         if(!hdl->singleLine)VG_(printf)("\n");
      } break;

      default:
         tl_assert(0);

   } /* switch (st->tag) */

   hdl->depth--;
   //if(d)VG_(printf)("\n");
}

#ifndef NDEBUG
static
Int ppTreeGetStmtIdx(IrsbTree * tree, IRStmt * st) {
   Int i;

   for(i=0;i<tree->irsb->stmts_used;i++) {
      if(tree->irsb->stmts[i]==st) return i;
   }
   VG_(printf)("ppTreeGetStmtIdx failed\n");
   return 0;
}
#endif

static
void ppTreeExt (IrsbTree * tree, Int root, Bool singleLine, Int tabSize, Int maxDepth, Bool followGetPutLoops, Bool printTmpNames)
{
   _ppTreeHdl hdl;
   Int stmtsUsed=tree->irsb->stmts_used;
   UInt SZ=stmtsUsed*sizeof(UInt);
   tl_assert(root>=0&&root<stmtsUsed);
   hdl.usedStmts=alloca(SZ);
   hdl.depth=0;
   hdl.maxDepth=maxDepth;
   if(hdl.maxDepth>40) hdl.maxDepth=40;
   hdl.tabSize=tabSize;
   hdl.singleLine=singleLine;
   hdl.followGetPutLoops=followGetPutLoops;
   hdl.printTmpNames=printTmpNames;
   if(hdl.singleLine) {
      hdl.tabSize=0;
      if(hdl.maxDepth>10) hdl.maxDepth=10;
   }
   VG_(memset)(hdl.usedStmts,0,SZ);
   if(!hdl.singleLine) VG_(printf)("# tree@%d : \n",root);
   _ppTree_Stmt(tree,root,0,&hdl);
}

static
void ppTree (IrsbTree * tree, Int root)
{
   ppTreeExt(tree,root,False,2,40,True, True /*printTmpNames*/);
}

void HG_(ppTree_irsb)(IrsbTree * tree) {
   IRSB * irsb=tree->irsb;
   Int i;
   VG_(printf)("Tree-IRSB {\n");
   for(i=0;i<irsb->stmts_used;i++) {
      IRStmt * st=irsb->stmts[i];
      VG_(printf)("  %'3d   ",i);
      switch (st->tag) {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_PutI:
         case Ist_Dirty:
         case Ist_MBE:
         case Ist_IMark:
            ppIRStmt(st); //not implemented
            break;

         case Ist_Store:
         case Ist_Put:
         case Ist_WrTmp:
         case Ist_Exit:
            ppIRStmt(st);
            VG_(printf)("\t| ");
            ppTreeExt(tree,i,True,0,10,False,True);
            break;

         default:
            tl_assert(0);

      }
      VG_(printf)("\n");
   }
   VG_(printf)("} Tree-IRSB-end \n");
}

/*----------------------------------------------------------------*/
/*--- TREE BUILD                                          ---*/
/*----------------------------------------------------------------*/

#define TREE_NODE_TMP_NONE (-3141592)

/**
 * Removes Stores in atomic instructions
 */
static
void _tree_buildTmp ( IrsbTree * tree )
{
   //WordFM* irTree;// get it as parameter ??
   //irTree=VG_(newFM)( MALLOC, "analyseIRSBjumps" ,FREE, NULL/*unboxed Word cmp*/);;

   //Int tyMap[bbIn->tyenv->types_used];

   Int     i;
   //IRSB*   bbOut;

   /* Address and length of the current binary instruction */
   //Addr   iaddr = 0;
   IRSB * bbIn = tree->irsb;
   Bool x86busLocked=False, isSnoopedStore=False;
   if(!tree->tstruct) {
      Int asZ=bbIn->tyenv->types_used*sizeof(Int);
      if(asZ<=0) asZ=sizeof(Int);
      tree->tstruct=HG_(zalloc)("build_Tree.tree",asZ);
   }
   for(i=0;i<bbIn->tyenv->types_used;i++) {
      tree->tstruct[i]=TREE_NODE_TMP_NONE; // PI
   }

   // Ignore any IR preamble preceding the first IMark
   i = 0;
   while (i < bbIn->stmts_used && bbIn->stmts[i]->tag != Ist_IMark) {
      i++;
   }

   /* Do just as much as needed to determine whether there is a call to
      pthread_cond_wait */
   //for (i=bbIn->stmts_used-1; i >=minI ; i--) {
   for (/*use current i*/; i <bbIn->stmts_used ; i++) {
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
            }break;
         case Ist_IMark:
            //iaddr=st->Ist.IMark.addr;
            break;

         case Ist_Store: {
            if( x86busLocked || isSnoopedStore ) {
               // remove stores in atomics
               bbIn->stmts[i] = IRStmt_Comment("Store removed in atomic instruction",False);
            }
         } break;

         case Ist_Put: {
            //treeNode_addRegister(tree, st->Ist.Put.offset, typeOfIRExpr(tree->irsb->tyenv,st->Ist.Put.data), i);
         } break;

         case Ist_WrTmp: {
            //IRExpr * data = st->Ist.WrTmp.data;
            IRTemp tmp = st->Ist.WrTmp.tmp;
            tree->tstruct[tmp]=i;

         } break;

         case Ist_Exit: {

            IRExpr *guard = st->Ist.Exit.guard;

            tl_assert( guard->tag==Iex_RdTmp );


         } break;

         default:
            tl_assert(0);

      } /* switch (st->tag) */
   } /* iterate over bbIn->stmts */
}

static
void _tree_buildRegister ( IrsbTree * tree )
{
   //WordFM* irTree;// get it as parameter ??
   //irTree=VG_(newFM)( MALLOC, "analyseIRSBjumps" ,FREE, NULL/*unboxed Word cmp*/);;

   //Int tyMap[bbIn->tyenv->types_used];

   Int     i;
   //IRSB*   bbOut;

   IRSB * bbIn = tree->irsb;

   // maps offset to stmt index
   WordFM * lastPuts=VG_(newFM)( HG_(zalloc), MALLOC_CC(_tree_buildRegister+lastPuts), HG_(free), cmp_unsigned_Words );
   WordFM * getsTodo=VG_(newFM)( HG_(zalloc), MALLOC_CC(_tree_buildRegister+getsTodo), HG_(free), cmp_unsigned_Words );

   /* Do just as much as needed to determine whether there is a call to
      pthread_cond_wait */
   //for (i=bbIn->stmts_used-1; i >=minI ; i--) {
   for (/*use current i*/; i <bbIn->stmts_used ; i++) {
      IRStmt* st = bbIn->stmts[i];
      tl_assert(st);
      tl_assert(isFlatIRStmt(st));
      switch (st->tag) {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_PutI:
         case Ist_Dirty:
         case Ist_MBE:
         case Ist_IMark:
         case Ist_Store: break;

         case Ist_Put: {
            VG_(addToFM)(lastPuts,st->Ist.Put.offset,i);
         } break;

         case Ist_WrTmp: {
            //IRExpr * data = st->Ist.WrTmp.data;
            //IRTemp tmp = st->Ist.WrTmp.tmp;
            // handle Get's
            if(st->Ist.WrTmp.data->tag==Iex_Get) {
               IRExpr * e=st->Ist.WrTmp.data;
               UWord stmt;
               //e->Iex.Get.offset
               if(VG_(lookupFM)(lastPuts,NULL,&stmt,e->Iex.Get.offset)) {
                  treeNode_addRegister(tree, e->Iex.Get.offset, e->Iex.Get.ty, stmt);
               } else {
                  VG_(addToFM)(getsTodo,e->Iex.Get.offset,e->Iex.Get.ty);
               }
            }
         } break;

         case Ist_Exit:break;

         default:
            tl_assert(0);

      } /* switch (st->tag) */
   } /* iterate over bbIn->stmts */

   {
      UWord stmt,offset,typ;
      VG_(initIterFM)(getsTodo);
      while(VG_(nextIterFM)(getsTodo,&offset,&typ)) {
         if(VG_(lookupFM)(lastPuts,NULL,&stmt,offset)) {
            //VG_(printf)("Add register r:");ppIRType(typ);VG_(printf)("(%d) ; loop\n", offset);
            treeNode_addRegister(tree, offset, typ, stmt);
         } else {
            // this is a get without any put
         }
      }
      VG_(doneIterFM)(getsTodo);
   }

   VG_(deleteFM)(lastPuts,NULL,NULL);
   VG_(deleteFM)(getsTodo,NULL,NULL);
}

static
void tree_init ( IrsbTree * tree, IRSB* bbIn )
{
   //remoreIrsbHeader( bbIn );
   tree->irsb=bbIn;
   tree->regLoopDefs=VG_(newFM)(HG_(zalloc),MALLOC_CC(tree_init+tree->regLoopDefs),HG_(free),cmp_unsigned_Words);
   tree->stmtAddressProps=NULL;
   tree->loadIdx=NULL;
   tree->tstruct=NULL;
   tree->wasSimplified=False;
   _tree_buildTmp(tree);
   _tree_buildRegister(tree);
}

typedef struct s_AddressProp AddressProp;

static void addressProp_free(AddressProp * addrP);

static
void _tree_freeData(IrsbTree * tree)
{
   if(tree->tstruct) {
      HG_(free)(tree->tstruct);
      tree->tstruct=NULL;
   }
#if USE_DEPENDENCY_CHECKER
   if(tree->stmtAddressProps) {
      VG_(deleteFM) ( tree->stmtAddressProps, NULL, (void(*)(UWord))addressProp_free );
      tree->stmtAddressProps=NULL;
   }
#endif
   if(tree->regLoopDefs) {
      VG_(deleteFM) ( tree->regLoopDefs, NULL, NULL );
      tree->regLoopDefs=NULL;
   }
   if(tree->loadIdx) tl_assert(0);
}

/*----------------------------------------------------------------*/
/*--- IRSB CONCAT                                          ---*/
/*----------------------------------------------------------------*/

static
IRExpr * _concat_irsb_exprRename ( IRExpr * e, IRTemp * tyMap ) {
   // should happen almost time
   if(e->tag==Iex_RdTmp) {
      if(tyMap[e->Iex.RdTmp.tmp]==(IRTemp)-1) {
         VG_(printf)("tyMap[%d]==-1\n",e->Iex.RdTmp.tmp);
         tl_assert(0);
      }
      return IRExpr_RdTmp(tyMap[e->Iex.RdTmp.tmp]);
   }
   switch( e->tag ) {
      case Iex_CCall: {
         //e->Iex.CCall.cee.regparms;
         //IRExpr_CCall()
         //ppIRExpr(e);
         int i;
         IRExpr ** args=shallowCopyIRExprVec(e->Iex.CCall.args);
         for (i = 0; e->Iex.CCall.args[i] != NULL; i++) {
            IRExpr * arg=_concat_irsb_exprRename(e->Iex.CCall.args[i], tyMap);
            args[i]=arg;
         }
         return IRExpr_CCall(
               e->Iex.CCall.cee, // reuse
               e->Iex.CCall.retty,
               args
               );
      } break;
      case Iex_Const: {
         return e; // reuse
      } break;
      case Iex_Get: {
         return e; // reuse
      } break;
      case Iex_GetI: {
         return e; // reuse
      } break;
      /*case Iex_RdTmp: {
         return IRExpr_RdTmp(tyMap[e->Iex.RdTmp.tmp]);
      } break;*/
      case Iex_Mux0X: {
         return IRExpr_Mux0X(
               _concat_irsb_exprRename(e->Iex.Mux0X.cond, tyMap),
               _concat_irsb_exprRename(e->Iex.Mux0X.expr0, tyMap),
               _concat_irsb_exprRename(e->Iex.Mux0X.exprX, tyMap)
               );
      } break;
      case Iex_Qop: {
         return IRExpr_Qop(e->Iex.Qop.op,
               _concat_irsb_exprRename(e->Iex.Qop.arg1,tyMap),
               _concat_irsb_exprRename(e->Iex.Qop.arg2,tyMap),
               _concat_irsb_exprRename(e->Iex.Qop.arg3,tyMap),
               _concat_irsb_exprRename(e->Iex.Qop.arg4,tyMap)
               );
      } break;
      case Iex_Triop: {
         return IRExpr_Triop(e->Iex.Triop.op,
                        _concat_irsb_exprRename(e->Iex.Triop.arg1,tyMap),
                        _concat_irsb_exprRename(e->Iex.Triop.arg2,tyMap),
                        _concat_irsb_exprRename(e->Iex.Triop.arg3,tyMap)
                        );
      } break;
      case Iex_Binop: {
         return IRExpr_Binop(e->Iex.Binop.op,
                        _concat_irsb_exprRename(e->Iex.Binop.arg1,tyMap),
                        _concat_irsb_exprRename(e->Iex.Binop.arg2,tyMap)
                        );
      } break;
      case Iex_Unop: {
         return IRExpr_Unop(e->Iex.Unop.op,
               _concat_irsb_exprRename(e->Iex.Unop.arg,tyMap)
               );
      } break;
      case Iex_Load: { // cannot happen in flat IRSB
         //IRExpr * addr = st->Ist.Load.addr;
         //tl_assert(addr->tag==Iex_RdTmp || addr->tag==Iex_Const);
         //addr = concat_irsb_exprRename(addr);
         return IRExpr_Load(
               e->Iex.Load.end,
               e->Iex.Load.ty,
               _concat_irsb_exprRename(e->Iex.Load.addr,tyMap)
               );

      } break;
      default:
         tl_assert(0);
   }
}

static
const Char * jumpKindToStr( IRJumpKind irJumpKind )
{
   Char * jumpKind;
#define __concatBB_jumpKindToStr(kind) case kind: { jumpKind=STRINGIFY(kind); break; }
      switch(irJumpKind) {
         __concatBB_jumpKindToStr(Ijk_Boring);
         __concatBB_jumpKindToStr(Ijk_Call);
         __concatBB_jumpKindToStr(Ijk_Ret);
         __concatBB_jumpKindToStr(Ijk_ClientReq);
         __concatBB_jumpKindToStr(Ijk_Yield);
         __concatBB_jumpKindToStr(Ijk_EmWarn);
         __concatBB_jumpKindToStr(Ijk_EmFail);
         __concatBB_jumpKindToStr(Ijk_NoDecode);
         __concatBB_jumpKindToStr(Ijk_MapFail);
         __concatBB_jumpKindToStr(Ijk_TInval);
         __concatBB_jumpKindToStr(Ijk_NoRedir);
         __concatBB_jumpKindToStr(Ijk_SigTRAP);
         __concatBB_jumpKindToStr(Ijk_SigSEGV);
         __concatBB_jumpKindToStr(Ijk_Sys_syscall);
         __concatBB_jumpKindToStr(Ijk_Sys_int32);
         __concatBB_jumpKindToStr(Ijk_Sys_int128);
         __concatBB_jumpKindToStr(Ijk_Sys_int129);
         __concatBB_jumpKindToStr(Ijk_Sys_int130);
         __concatBB_jumpKindToStr(Ijk_Sys_sysenter);
         default: {
            jumpKind="Ijk_??";
            break;
         }
      }
#undef __concatBB_jumpKindToStr
      return jumpKind;
}

void HG_(concat_irsb) (
                IRSB* bbIn,
                IRSB* bbOut, /*out*/
                Addr64 skipAddr,
                VexGuestExtents* vge,
                VgCallbackClosure* closure,
                VexGuestLayout* layout
               )
{
   Int     i;
   // t1 = newIRTemp(bb->tyenv, ty);
   IRTemp tyMap[bbIn->tyenv->types_used];
   Int currentBasicBlock=0;
   Bool skipping=False;

#ifndef NDEBUG
   for(i=0;i<bbIn->tyenv->types_used;i++) {
      tyMap[i]=(IRTemp)-1;
   }
#endif

   // Ignore any IR preamble preceding the first IMark
   i = 0;
   while (i < bbIn->stmts_used && bbIn->stmts[i]->tag != Ist_IMark) {
      i++;
   }

   if(bbOut->stmts_used>0) {
      addStmtToIRSB(bbOut,
            IRStmt_IMark((UWord)bbIn,-1) // separator between blocks
            );
   }


   if(0) {
      VG_(printf)("HG_(concat_irsb)::out=\n");
      ppIRSB(bbOut);
      VG_(printf)("HG_(concat_irsb)::in=\n");
      ppIRSB(bbIn);
      VG_(printf)("HG_(concat_irsb)::end\n");
   }

   for (/*use current i*/; i < bbIn->stmts_used; i++) {
      IRStmt* st = bbIn->stmts[i];
      tl_assert(st);
      tl_assert(isFlatIRStmt(st));
      switch (st->tag) {
         case Ist_WrTmp: {
            IRExpr * data = st->Ist.WrTmp.data;
            IRTemp origTmp = st->Ist.WrTmp.tmp;
            IRTemp newTmp = newIRTemp(bbOut->tyenv, typeOfIRTemp(bbIn->tyenv,origTmp));

            tyMap[origTmp]=newTmp;

            if(0) {
               VG_(printf)("HG_(concat_irsb)::stmt=");
               ppIRStmt(st);
               VG_(printf)("\n");
            }
            addStmtToIRSB(bbOut,
                           IRStmt_WrTmp(newTmp,_concat_irsb_exprRename(data,tyMap))
                           );
         } break;
         case Ist_Dirty: {

            if(st->Ist.Dirty.details->tmp!=IRTemp_INVALID) {
               IRTemp origTmp = st->Ist.Dirty.details->tmp;
               IRTemp newTmp = newIRTemp(bbOut->tyenv, typeOfIRTemp(bbIn->tyenv,origTmp));

               //IRDirty * di = deepCopyIRDirty(st->Ist.Dirty.details);

               tyMap[origTmp]=newTmp;

               //di->tmp = newTmp;
               //addStmtToIRSB(bbOut,IRStmt_Dirty(di));

               // TODO : Ist.Dirty is not correctly handled
               addStmtToIRSB(bbOut,IRStmt_WrTmp(newTmp,IRExpr_Const(IRConst_U1(0))));
            } else {
               addStmtToIRSB(bbOut,st); // reuse
            }

         } break;
            /* None of these can contain anything of interest */
            //addStmtToIRSB(bbOut,st);
            //break;
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_PutI:

         case Ist_MBE:
            //addStmtToIRSB(bbOut,st);
            //break;
            addStmtToIRSB(bbOut,st); // reuse
            break;
         case Ist_IMark: {
            Addr64 curInstEnd= st->Ist.IMark.addr+st->Ist.IMark.len;
            Addr64 curBBend;

            if((skipAddr==st->Ist.IMark.addr)||(currentBasicBlock>=vge->n_used)) {
               if(PRINT_concat_irsb) {
                  ppIRStmt(st);
                  VG_(printf)("\nskipping in %s\n",STRINGIFY(HG_(concat_irsb)));
               }
               skipping=True;
               break;
            }

            curBBend=vge->base[currentBasicBlock]+vge->len[currentBasicBlock];
            if(curInstEnd==curBBend) {
               currentBasicBlock++;
            }
            addStmtToIRSB(bbOut,st); // reuse
         } break;

         case Ist_Store: {
            IRExpr * addr = st->Ist.Store.addr;
            IRExpr * data = st->Ist.Store.data;

            tl_assert(isIRAtom(addr)&&isIRAtom(data));

            if(addr->tag==Iex_RdTmp) {
               tl_assert(tyMap[addr->Iex.RdTmp.tmp]!=(IRTemp)-1);
               addr=IRExpr_RdTmp(tyMap[addr->Iex.RdTmp.tmp]);
            }
            if(data->tag==Iex_RdTmp) {
               tl_assert(tyMap[data->Iex.RdTmp.tmp]!=(IRTemp)-1);
               data=IRExpr_RdTmp(tyMap[data->Iex.RdTmp.tmp]);
            }

            addStmtToIRSB(bbOut,
                  IRStmt_Store(st->Ist.Store.end,addr,data)
                  );

         } break;

         case Ist_Put: {
            IRExpr * data = st->Ist.Put.data;
            //st->Ist.Put.offset
            tl_assert(isIRAtom(data));
            if(data->tag==Iex_RdTmp) {
               tl_assert(tyMap[data->Iex.RdTmp.tmp]!=(IRTemp)-1);
               data=IRExpr_RdTmp(tyMap[data->Iex.RdTmp.tmp]);
            }
            addStmtToIRSB(bbOut,
                  IRStmt_Put(st->Ist.Put.offset,data)
                  );
         } break;

         case Ist_Exit: {
            IRExpr *guard = st->Ist.Exit.guard;

            tl_assert( guard->tag==Iex_RdTmp );

            tl_assert(tyMap[guard->Iex.RdTmp.tmp]!=(IRTemp)-1);

            guard = IRExpr_RdTmp(tyMap[guard->Iex.RdTmp.tmp]);

            addStmtToIRSB(bbOut,
                        IRStmt_Exit(
                              guard,
                              st->Ist.Exit.jk,
                              st->Ist.Exit.dst // reuse
                        )
                        );
         } break;

         default:
            tl_assert(0);

      } /* switch (st->tag) */
      if(skipping) break;
   } /* iterate over bbIn->stmts */

#ifndef NDEBUG
   if(1) {
      IRStmt * st;
      IRTemp fooNext;
      const Char * jumpKind;

      if(skipping) {
         IRStmt * skippedIMark = bbIn->stmts[i];
         st=IRStmt_Comment(" We skipped the last basic blocks. Next imark would be : ",False);
         addStmtToIRSB(bbOut,st);
         tl_assert(skippedIMark->tag==Ist_IMark);
         addStmtToIRSB(bbOut,skippedIMark);
      }

      jumpKind=jumpKindToStr(bbIn->jumpkind);
      st=IRStmt_Comment(" Next pointer %s ",True,jumpKind);
      addStmtToIRSB(bbOut,st);
      fooNext=newIRTemp(bbOut->tyenv, typeOfIRExpr(bbIn->tyenv,bbIn->next));
      if(bbIn->next->tag==Iex_RdTmp) {
         IRStmt_Comment(" jump t%d ",True,bbIn->next->Iex.RdTmp.tmp);
      } else {
         st=IRStmt_WrTmp(fooNext,bbIn->next);
      }
      addStmtToIRSB(bbOut,st);
      st=IRStmt_Comment(" Next pointer end ",False);
      addStmtToIRSB(bbOut,st);
   }
#endif
}


/*----------------------------------------------------------------*/
/*--- OPTIMISER                                          ---*/
/*----------------------------------------------------------------*/

/* Used by the optimiser to specialise calls to helpers. */
extern
IRExpr* guest_x86_spechelper ( HChar* function_name,
                               IRExpr** args );

/* Describes to the optimiser which part of the guest state require
   precise memory exceptions.  This is logically part of the guest
   state description. */
extern
Bool guest_x86_state_requires_precise_mem_exns ( Int, Int );


IRSB * HG_(optimise_irsb) ( IRSB * irsb, Addr64 sbBase )
{
   Addr64             addr;
   Addr64             nraddr=sbBase;
   static Bool  ta_allow_redirection = 1; //m_translate.c

   /* Establish the translation kind and actual guest address to
      start from.  Sets (addr,kind). */
   if (ta_allow_redirection) {
      Bool isWrap;
      Addr64 tmp = VG_(redir_do_lookup)( nraddr, &isWrap );
      if (tmp == nraddr) {
         /* no redirection found */
         addr = nraddr;
      } else {
         /* found a redirect */
         addr = tmp;
      }
   } else {
      addr = nraddr;
   }
   //Int stmts_used=irsb->stmts_used;

   // SSA : look at redundant_get_removal_BB( .)
   irsb = do_iropt_BB (
         irsb,
         guest_x86_spechelper,
         guest_x86_state_requires_precise_mem_exns,
         addr );

   return irsb;
}

static UInt mk_key_GetPut ( Int offset, IRType ty )
{
   /* offset should fit in 16 bits. */
   UInt minoff = offset;
   UInt maxoff = minoff + sizeofIRType(ty) - 1;
   tl_assert((minoff & ~0xFFFF) == 0);
   tl_assert((maxoff & ~0xFFFF) == 0);
   return (minoff << 16) | maxoff;
}

static void irsb_redundantGetRemoval ( IRSB* bb )
{
   //static void redundant_get_removal_BB ( IRSB* bb )
   UInt    key = 0; /* keep gcc -O happy */
   Int     i;
   HWord   val;
   WordFM * env = VG_(newFM)(HG_(zalloc),MALLOC_CC(irsb_redundantGetRemoval+env),HG_(free),cmp_unsigned_Words);


   for (i = 0; i < bb->stmts_used; i++) {
      IRStmt* st = bb->stmts[i];

      if (st->tag == Ist_NoOp)
         continue;

      /* Deal with Gets */
      if (st->tag == Ist_WrTmp
          && st->Ist.WrTmp.data->tag == Iex_Get) {
         /* st is 't = Get(...)'.  Look up in the environment and see
            if the Get can be replaced. */
         IRExpr* get = st->Ist.WrTmp.data;
         key = (HWord)mk_key_GetPut( get->Iex.Get.offset,
                                     get->Iex.Get.ty );
         if (VG_(lookupFM)(env, NULL, &val, (HWord)key)) {
            /* found it */
            /* Note, we could do better here.  If the types are
               different we don't do the substitution, since doing so
               could lead to invalidly-typed IR.  An improvement would
               be to stick in a reinterpret-style cast, although that
               would make maintaining flatness more difficult. */
            IRExpr* valE    = (IRExpr*)val;
            Bool    typesOK = toBool( typeOfIRExpr(bb->tyenv,valE)
                                      == st->Ist.WrTmp.data->Iex.Get.ty );
            if (typesOK)
               bb->stmts[i] = IRStmt_WrTmp(st->Ist.WrTmp.tmp, valE);
         } else {
            /* Not found, but at least we know that t and the Get(...)
               are now associated.  So add a binding to reflect that
               fact. */
            VG_(addToFM)( env, (HWord)key,
                           (HWord)(void*)(IRExpr_RdTmp(st->Ist.WrTmp.tmp)) );
         }
      }

      /* add this one to the env, if appropriate */
      if (st->tag == Ist_Put) {
         tl_assert(isIRAtom(st->Ist.Put.data));
         key = (HWord)mk_key_GetPut( st->Ist.Put.offset,
               typeOfIRExpr(bb->tyenv,st->Ist.Put.data) );
         VG_(addToFM)( env, (HWord)key, (HWord)(st->Ist.Put.data));
      }

   } /* for (i = 0; i < bb->stmts_used; i++) */

   VG_(deleteFM)(env, NULL, NULL);
}

typedef struct s_IRAlias {
   enum {
      Is_AliasNone=0,
      Is_AliasTmp,
      Is_AliasConst
   } typ;
   union {
      IRTemp tmp;
      IRConst * con;
   };
} _optimise_IRAlias;

static
void _optimiseAtomicAlias_expr(IRExpr * e, _optimise_IRAlias * aliases, Int typesUsed)
{
   if(e->tag==Iex_RdTmp) {
       _optimise_IRAlias alias;
       if(!(e->Iex.RdTmp.tmp>=0&&e->Iex.RdTmp.tmp<typesUsed)) {
          VG_(printf)("tmp=t%d invalid (%d)\n",e->Iex.RdTmp.tmp,typesUsed);
          tl_assert(0);
       }
       alias = aliases[e->Iex.RdTmp.tmp];
       switch(alias.typ) {
          case Is_AliasTmp:
             e->Iex.RdTmp.tmp=alias.tmp; // in place
             break;
          case Is_AliasConst:
             // a bit dirty...
             e->tag=Iex_Const; // in place
             e->Iex.Const.con=alias.con;
             break;
          case Is_AliasNone:
             break;
       }
       return;
   }
   switch( e->tag ) {
      case Iex_CCall: {

      } break;
      case Iex_Const: {
      } break;
      case Iex_Get: {
      } break;
      case Iex_GetI: {
         //e->Iex.GetI.ix
      } break;
      case Iex_Mux0X: {
         _optimiseAtomicAlias_expr(e->Iex.Mux0X.cond,aliases,typesUsed);
         _optimiseAtomicAlias_expr(e->Iex.Mux0X.expr0,aliases,typesUsed);
         _optimiseAtomicAlias_expr(e->Iex.Mux0X.exprX,aliases,typesUsed);
      } break;
      case Iex_Qop: {
         _optimiseAtomicAlias_expr(e->Iex.Qop.arg1,aliases,typesUsed);
         _optimiseAtomicAlias_expr(e->Iex.Qop.arg2,aliases,typesUsed);
         _optimiseAtomicAlias_expr(e->Iex.Qop.arg3,aliases,typesUsed);
         _optimiseAtomicAlias_expr(e->Iex.Qop.arg4,aliases,typesUsed);
      } break;
      case Iex_Triop: {
         _optimiseAtomicAlias_expr(e->Iex.Triop.arg1,aliases,typesUsed);
         _optimiseAtomicAlias_expr(e->Iex.Triop.arg2,aliases,typesUsed);
         _optimiseAtomicAlias_expr(e->Iex.Triop.arg3,aliases,typesUsed);
      } break;
      case Iex_Binop: {
         _optimiseAtomicAlias_expr(e->Iex.Binop.arg1,aliases,typesUsed);
         _optimiseAtomicAlias_expr(e->Iex.Binop.arg2,aliases,typesUsed);
      } break;
      case Iex_Unop: {
         _optimiseAtomicAlias_expr(e->Iex.Unop.arg,aliases,typesUsed);
      } break;
      case Iex_Load: { // cannot happen in flat IRSB
         _optimiseAtomicAlias_expr(e->Iex.Load.addr,aliases,typesUsed);
      } break;
      default:
         tl_assert(0);
   }
}

/**
 * Doesn't change irsb indexing
 */
static
void irsb_optimiseAtomicAlias( IRSB * irsb )
{
   Int typesUsed=irsb->tyenv->types_used;
   _optimise_IRAlias aliases[typesUsed];
   Int i;
   for(i=0;i<typesUsed;i++) {
      aliases[i].typ=Is_AliasNone; // means "No alias"
   }
   for(i=0;i<irsb->stmts_used;i++) {
      IRStmt * st=irsb->stmts[i];
      if(st->tag==Ist_WrTmp) {
         switch(st->Ist.WrTmp.data->tag) {
            case Iex_RdTmp: {
               _optimise_IRAlias alias;
               alias.typ=Is_AliasTmp;
               alias.tmp=st->Ist.WrTmp.data->Iex.RdTmp.tmp;
               while(alias.typ==Is_AliasTmp && aliases[alias.tmp].typ!=Is_AliasNone) {
                  alias=aliases[alias.tmp];
               }
               if(alias.typ==Is_AliasTmp && (st->Ist.WrTmp.tmp==alias.tmp)) {
                  ppIRSB(irsb); // TODO is it illegal for me ?
                  tl_assert2(0,"optimiseTmpCopies : tmp aliases itself :t%d\n",alias.tmp);
               }
               aliases[st->Ist.WrTmp.tmp]=alias;
               irsb->stmts[i]=IRStmt_Comment("optimised t%d = t%d copy",True, (Int)st->Ist.WrTmp.tmp, (Int)alias.tmp);
               st=irsb->stmts[i];
            }break;
            case Iex_Const: {
               _optimise_IRAlias alias;
               alias.typ=Is_AliasConst;
               alias.con=st->Ist.WrTmp.data->Iex.Const.con;
               aliases[st->Ist.WrTmp.tmp]=alias;
               irsb->stmts[i]=IRStmt_Comment("optimised t%d = constant %x ",True, st->Ist.WrTmp.tmp, value_of(alias.con));
               st=irsb->stmts[i];
            }break;
            default:
               break;
         }
      }
   }
   /**
    * In tree-irsb, a tmp can be used BEFORE his declaration.
    * So we must separate both loops.
    */
   for(i=0;i<irsb->stmts_used;i++) {
      IRStmt * st=irsb->stmts[i];
      switch(st->tag) {
         case Ist_WrTmp: {
            IRExpr * data = st->Ist.WrTmp.data;
            _optimiseAtomicAlias_expr(data,aliases,typesUsed);
         } break;
         case Ist_NoOp:
         case Ist_AbiHint:
            break;
         case Ist_PutI: {
            /* Handle it always or never : choose. Otherwise it bugs
            IRExpr * data = st->Ist.PutI.data;
            IRExpr * ix = st->Ist.PutI.ix;
            _optimiseTmpCopies_expr(data,aliases,typesUsed);
            _optimiseTmpCopies_expr(ix,aliases,typesUsed);
            */
         }break;
         case Ist_Dirty:

         case Ist_MBE:

         case Ist_IMark:
            break;

         case Ist_Store: {
            IRExpr * addr = st->Ist.Store.addr;
            IRExpr * data = st->Ist.Store.data;
            _optimiseAtomicAlias_expr(addr,aliases,typesUsed);
            _optimiseAtomicAlias_expr(data,aliases,typesUsed);
         } break;

         case Ist_Put: {
            IRExpr * data = st->Ist.Put.data;
            _optimiseAtomicAlias_expr(data,aliases,typesUsed);
         } break;

         case Ist_Exit: {
            IRExpr *guard = st->Ist.Exit.guard;
            _optimiseAtomicAlias_expr(guard,aliases,typesUsed);
         } break;

         default:
            tl_assert(0);
      }
   }
}

/**
 * call optimiseTmpCopies and correct tree
 */
static inline
void tree_optimiseAtomicAlias(IrsbTree * tree)
{
   IRTemp tmpS;
   irsb_optimiseAtomicAlias(tree->irsb);
   // need to correct tmp index
   for(tmpS=0;tmpS<tree->irsb->tyenv->types_used;tmpS++) {
      if(tree->tstruct[tmpS]<0) continue;
      if(tree->irsb->stmts[treeNode_tmp(tree,tmpS)]->tag!=Ist_WrTmp) {
         if(PRINT_isConstTest) VG_(printf)("REMOVE tree->tstruct[t%d]\n",tmpS);
         tree->tstruct[tmpS]=TREE_NODE_TMP_NONE;
      }
   }
}

void HG_(optimise_irsb_fast) ( IRSB * irsb )
{
   irsb_redundantGetRemoval( irsb );
   irsb_optimiseAtomicAlias( irsb );
}

/*----------------------------------------------------------------*/
/*--- EVALUATOR                                          ---*/
/*----------------------------------------------------------------*/

#if USE_INTERPRETER
typedef UWord ArchDat;

typedef struct s_Data_val {
   IRType typ;
   ArchDat data;
} Data_val;


typedef struct _virt_memory virt_memory;

typedef struct s_MemManager {
   void (*loadTmp)( IRTemp tmp, virt_memory * mem, Data_val * res /*out*/ );
   void (*loadMem)( UWord addr, IREndness end, IRType ty, virt_memory * mem, Data_val * res /*out*/ );
   void (*loadReg)( Int offset, virt_memory * mem, Data_val * res /*out*/ );
} MemManager;

typedef struct _virt_memory {
   MemManager* memManager;

   WordFM * registers;
   /* Map: Index --> Data_val */
   WordFM* temporaries;

   /* Map: Address --> mem */
   WordFM* ram;

   //BlockAllocator * blk;

   void* opaque;
} _virt_memory;


static
void expr_interpret ( IRExpr * e,
      virt_memory * mem,
      Data_val * res /*out*/ )
{
   //

   //typeOfIRExpr();
   MemManager * memManager=mem->memManager;

   switch( e->tag ) {
      case Iex_CCall:
         ppIRExpr(e);
         tl_assert(0);
         break;
      case Iex_GetI: {
            /* unhandled */
         } break;
      case Iex_Const: {
         IRConst* con = e->Iex.Const.con;
         switch ( con->tag ) {
            case Ico_V128:
               res->data=con->Ico.V128;
            case Ico_U1:
               res->data=con->Ico.U1;
            case Ico_U8:
               res->data=con->Ico.U8;
            case Ico_U16:
               res->data=con->Ico.U16;
            case Ico_U32:
               res->data=con->Ico.U32;
            case Ico_U64:
            default:
               res->data=con->Ico.U64;
         }
      } break;
      case Iex_Get: {
         if(0)
         switch( e->Iex.Get.ty ) {
            case Ity_I1:
            case Ity_I8:
            case Ity_I16:
            case Ity_I32:
               break;
            default:
               break;
         }
         memManager->loadReg(e->Iex.Get.offset,mem,res);
      } break;
      case Iex_RdTmp: {
         memManager->loadTmp(e->Iex.RdTmp.tmp, mem, res);
      } break;
      case Iex_Mux0X: {
         Data_val cond, exp0, expX;
         VG_(memset)(&cond, 0, sizeof(cond)); //?
         expr_interpret(e->Iex.Mux0X.cond, mem, &cond );
         expr_interpret(e->Iex.Mux0X.cond, mem, &exp0 ); // evaluate both ??
         expr_interpret(e->Iex.Mux0X.cond, mem, &expX );
         if(cond.data) (*res)=exp0;
         else (*res)=expX;
      } break;
      case Iex_Qop: {
         Data_val a1,a2,a3,a4;

         if(PRINT_LOGGING_HG_DEPENDENCY) {
            HG_(setLogFile)( LOG_FILE_HG_DEPENDENCY );
            VG_(printf)("interp.Iex_Qop ");
            ppIROp(e->Iex.Qop.op); // list ops
            VG_(printf)("\n");
            HG_(setLogFile)( NULL );
         }



         expr_interpret(e->Iex.Qop.arg1, mem, &a1 );
         expr_interpret(e->Iex.Qop.arg2, mem, &a2 );
         expr_interpret(e->Iex.Qop.arg3, mem, &a3 );
         expr_interpret(e->Iex.Qop.arg4, mem, &a4 );

         res->data = (a1.data + a2.data) ^ ((a3.data - a4.data) >>4);
         res->typ = a1.typ;
      } break;
      case Iex_Triop: {
         Data_val a1,a2,a3;

         if(PRINT_LOGGING_HG_DEPENDENCY) {
            HG_(setLogFile)( LOG_FILE_HG_DEPENDENCY );
            VG_(printf)("interp.Iex_Triop ");
            ppIROp(e->Iex.Triop.op); // list ops
            VG_(printf)("\n");
            HG_(setLogFile)( NULL );
         }



         expr_interpret(e->Iex.Triop.arg1, mem, &a1 );
         expr_interpret(e->Iex.Triop.arg2, mem, &a2 );
         expr_interpret(e->Iex.Triop.arg3, mem, &a3 );

         res->data = (a1.data + a2.data) ^ ((a3.data) >>4);
         res->typ = a1.typ;
      } break;
      case Iex_Binop: {
         Data_val a1,a2;

         expr_interpret(e->Iex.Binop.arg1, mem, &a1 );
         expr_interpret(e->Iex.Binop.arg2, mem, &a2 );

         switch( e->Iex.Binop.op ) {
            case Iop_Add32:
            case Iop_Add64:
               res->data = (a1.data + a2.data);
               res->typ = a1.typ;
               break;
            case Iop_Sub32:
            case Iop_Sub64:
               res->data = (a1.data - a2.data);
               res->typ = a1.typ;
               break;
            case Iop_Mul32:
            case Iop_Mul64:
               res->data = (a1.data * a2.data);
               res->typ = a1.typ;
               break;
            default:
               if(PRINT_LOGGING_HG_DEPENDENCY) {
                  HG_(setLogFile)( LOG_FILE_HG_DEPENDENCY );
                  VG_(printf)("interp.Iex_Binop ");
                  ppIROp(e->Iex.Binop.op); // list ops
                  VG_(printf)("\n");
                  HG_(setLogFile)( NULL );
               }

               res->data = (a1.data + (a2.data<<2));
               res->typ = a1.typ;
               break;
         }
      } break;
      case Iex_Unop: {
         Data_val a1;
         if(PRINT_LOGGING_HG_DEPENDENCY) {
            HG_(setLogFile)( LOG_FILE_HG_DEPENDENCY );
            ppIROp(e->Iex.Unop.op); // list ops
            VG_(printf)("\n");
            HG_(setLogFile)( NULL );
         }

         expr_interpret(e->Iex.Unop.arg, mem, &a1 );

         res->data = !(a1.data);
         res->typ = a1.typ;
      } break;
      case Iex_Load: { // cannot happen in flat IRSB
         Data_val addr;
         expr_interpret( e->Iex.Load.addr, mem, &addr );
         memManager->loadMem(addr.data,e->Iex.Load.end,e->Iex.Load.ty,mem,res);
      } break;
      default:
         tl_assert(0);
   }
}

static
void stmt_interpret(IRStmt * st)
{
   tl_assert(st);
   tl_assert(isFlatIRStmt(st));
   switch (st->tag) {
      case Ist_NoOp:
      case Ist_AbiHint:
         // Nothing ??
      case Ist_PutI:
      case Ist_Dirty:
         /* None of these can contain anything of interest */
         break;

      case Ist_MBE:
         break;

      case Ist_IMark:
         //iaddr = st->Ist.IMark.addr;
         //ilen  = st->Ist.IMark.len;
         break;

      case Ist_Store: {
         //IRExpr * addr = st->Ist.Store.addr;
         //IRExpr * data = st->Ist.Store.data;




         break;

         //expr_interpret ( IRExpr * e, virt_memory * mem, Data_val * res /*out*/ )

         /* Workaround to handle atomic instructions:
          *    Observe only reads during atomic instructions.
          */
         /*
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
         }*/
      } break;

      case Ist_Put: {
         /*IRExpr * data = st->Ist.Put.data;
         Variable * var_reg = varmap_lookup_or_create( HG_CFG_VAR_REGISTER, st->Ist.Put.offset );
         Variable * var = mk_Variable( HG_CFG_VAR_REGISTER, 0 );
         detect_dependencies( var, data );
         if( !has_dependency( var, HG_CFG_VAR_REGISTER, st->Ist.Put.offset ) )
            swap_dependencies( var_reg, var );
         else
            copy_dependencies( var_reg, var );
         del_Variable( (UWord)var );*/
      } break;

      case Ist_WrTmp: {
         //IRExpr * data = st->Ist.WrTmp.data;
         /*
         Variable * var_temp = varmap_lookup_or_create( HG_CFG_VAR_TEMP, st->Ist.WrTmp.tmp );
         if( data->tag == Iex_Load ) {
            IRExpr * addr = data->Iex.Load.addr;
            Variable * var_load = varmap_lookup_or_create( HG_CFG_VAR_LOAD, iaddr );

            add_dependency( var_temp, HG_CFG_VAR_LOAD, iaddr );
            detect_dependencies( var_load, addr );

         } else {
            detect_dependencies( var_temp, data );
         }*/
      } break;

      case Ist_Exit: {
         /*Variable *condition;
         IRExpr *guard = st->Ist.Exit.guard;

         condition = varmap_lookup_or_create( HG_CFG_VAR_CONDITION, 0 );
         detect_dependencies( condition, guard );*/
      } break;

      default:
         tl_assert(0);

   } /* switch (st->tag) */
}
#endif


// #################################################

/*----------------------------------------------------------------*/
/*--- VIRTUAL MEMORY                                          ---*/
/*----------------------------------------------------------------*/

#if USE_VIRTUAL_MEMORY_V1
static struct {
   ArchDat m_z,m_w;
} __rndSeed = {.m_z=1, .m_w=2};

static void randomData_val(Data_val* dv)
{
   UWord rnd;
   __rndSeed.m_z = 36969 * (__rndSeed.m_z & 65535) + (__rndSeed.m_z >> 16);
   __rndSeed.m_w = 18000 * (__rndSeed.m_w & 65535) + (__rndSeed.m_w >> 16);
   rnd = (__rndSeed.m_z << 16) + __rndSeed.m_w;  /* 32-bit result */

   dv->data = rnd;
}

static BlockAllocator blockAllocator;
/**
 * Make memory pool ?
 */
static
Data_val* allocData_val(HChar* cc)
{
   //return (Data_val*)MALLOC ( cc, sizeof(Data_val) );
   if(!(*(Int*)((void*)&blockAllocator))) {
      blockAllocator_new(&blockAllocator,0x10000);
   }
   return (Data_val*)blockAllocator_alloc(&blockAllocator,sizeof(Data_val));
}

#if !(SKIP_UNUSED)
static
void freeData_val(Data_val* dv)
{
   //FREE(dv);
}
#endif

static
void loadTmp( IRTemp tmp, virt_memory * mem, Data_val * res /*out*/ )
{
   Data_val * var;
   if( !VG_(lookupFM)( mem->temporaries, (UWord*)&var, NULL, (UWord)tmp ) ) {
      tl_assert(0);
   }
   (*res)=(*var);
}

static
void loadMem( UWord addr, IREndness end, IRType ty, virt_memory * mem, Data_val * res /*out*/ )
{
   Data_val * var;
   if( !VG_(lookupFM)( mem->ram, NULL, (UWord*)&var, (UWord)addr ) ) {
      var=allocData_val("loadMem");
      VG_(addToFM)( mem->ram, addr, (UWord)var);
      randomData_val(var);
      var->typ=ty;
   }
   (*res)=(*var);
}

static
void loadReg( Int offset, virt_memory * mem, Data_val * res /*out*/ )
{
   Data_val * var;
   if( !VG_(lookupFM)( mem->registers, NULL, (UWord*)&var, (UWord)offset ) ) {
      var=allocData_val("loadReg");
      VG_(addToFM)( mem->registers, offset, (UWord)var);
      randomData_val(var);
      var->typ=Ity_I64;
   }
   (*res)=(*var);
}
#endif

#if !(SKIP_UNUSED)
static
void ppDataVal(Data_val * dv)
{
   VG_(printf)("%d",(Int)dv->data);
}
static
void ppMemory(virt_memory * mem)
{
   UWord pKey;
   Data_val * dv;
   VG_(printf)("MEMORY:");
   VG_(initIterFM)(mem->temporaries);
   VG_(printf)("Tmps : ");
   while(VG_(nextIterFM)(mem->temporaries,&pKey,(UWord*)&dv))
   {
      VG_(printf)(" t%d = ",(Int)pKey);
      ppDataVal(dv);
   }
   VG_(printf)("\n");
   VG_(doneIterFM)(mem->temporaries);


   VG_(printf)("END-MEMORY");
}

#endif

#if USE_VIRTUAL_MEMORY_V2
static
virt_memory createVirt_memory(
      MemManager * memManager,
      void* (*alloc_nofail)( HChar*, SizeT ),
      void  (*dealloc)(void*)
      )
{
   virt_memory res;
   res.ram=VG_(newFM)(alloc_nofail,"createVirt_memory",dealloc,cmp_unsigned_Words);
   res.registers=VG_(newFM)(alloc_nofail,"createVirt_memory",dealloc,cmp_unsigned_Words);
   res.temporaries=VG_(newFM)(alloc_nofail,"createVirt_memory",dealloc,cmp_unsigned_Words);
   res.memManager=memManager;
   res.opaque=NULL;
   return res;
}
#endif

#if !(SKIP_UNUSED)
static
void evaluateTmp(IrsbTree * tree, IRTemp tmp,virt_memory * mem, Data_val * res)
{
   //IrsbTree tree;
   //IRTemp tmp;
   Int stmtIndex;
   IRStmt * st;
   IRExpr * data;
   tl_assert(tree);
   tl_assert( tmp>=0 && tmp<tree->irsb->tyenv->types_used );
   stmtIndex=treeNode_tmp(tree,tmp);
   tl_assert2(stmtIndex>=0 && stmtIndex<tree->irsb->stmts_used,
         "Wrong stmtIndex %d must be between 0 and %d\ntmp was %d",stmtIndex,tree->irsb->stmts_used,tmp);
   st=tree->irsb->stmts[stmtIndex];
   data = st->Ist.WrTmp.data;
   tl_assert(st->tag==Ist_WrTmp);

   expr_interpret(data,mem,res);
}
#endif

#if !(SKIP_UNUSED)
static
void loadTmp_evaluate( IRTemp tmp, virt_memory * mem, Data_val * res /*out*/ )
{
   Data_val * var;
   tl_assert(0);
   if( !VG_(lookupFM)( mem->temporaries, (UWord*)&var, NULL, (UWord)tmp ) ) {
      IrsbTree * tree = (IrsbTree *)mem->opaque;
      evaluateTmp(tree,tmp,mem,res);
      return;
   }
   (*res)=(*var);
}
#endif

#if !(SKIP_UNUSED)
static
void treeMemManager( MemManager * res )
{
   res->loadMem=loadMem;
   res->loadTmp=loadTmp_evaluate;
   res->loadReg=loadReg;
}
#endif

/*----------------------------------------------------------------*/
/*--- VIRTUAL MEMORY V2                                         ---*/
/*----------------------------------------------------------------*/

#if USE_VIRTUAL_MEMORY_V2
typedef struct s_VirtMem2Opaque {
   IrsbTree * tree;
   BlockAllocator * ba;
} VirtMem2Opaque;


static
void loadTmp_evaluate2( IRTemp tmp, virt_memory * mem, Data_val * res /*out*/ )
{
   Data_val * var;
   if( !VG_(lookupFM)( mem->temporaries, NULL, (UWord*)&var, (UWord)tmp ) ) {
      VirtMem2Opaque * vmo = (VirtMem2Opaque *)mem->opaque;
      // cache results :
      var=blockAllocator_alloc(vmo->ba,sizeof(*var));
      VG_(addToFM)( mem->temporaries, tmp, (UWord)var);
      evaluateTmp(vmo->tree,tmp,mem,var);
   }
   (*res)=(*var);
}

static
void loadMem2( UWord addr, IREndness end, IRType ty, virt_memory * mem, Data_val * res /*out*/ )
{
   Data_val * var;
   if( !VG_(lookupFM)( mem->ram, NULL, (UWord*)&var, (UWord)addr ) ) {
      VirtMem2Opaque * vmo=mem->opaque;
      var=blockAllocator_alloc(vmo->ba,sizeof(*var));
      VG_(addToFM)( mem->ram, addr, (UWord)var);
      randomData_val(var);
      var->typ=ty;
   }
   (*res)=(*var);
}

static
void loadReg2( Int offset, virt_memory * mem, Data_val * res /*out*/ )
{
   Data_val * var;
   if( !VG_(lookupFM)( mem->registers, NULL, (UWord*)&var, (UWord)offset ) ) {
      VirtMem2Opaque * vmo=mem->opaque;
      var=blockAllocator_alloc(vmo->ba,sizeof(*var));
      VG_(addToFM)( mem->registers, offset, (UWord)var);
      randomData_val(var);
      var->typ=Ity_I64;
   }
   (*res)=(*var);
}

static
void treeMemManager2( MemManager * res )
{
   res->loadMem=loadMem2;
   res->loadTmp=loadTmp_evaluate2;
   res->loadReg=loadReg2;
}


static
virt_memory * createVirtMem2(IrsbTree * tree)
{
   BlockAllocator * ba;
   virt_memory * mem;
   MemManager * memManager;
   VirtMem2Opaque * vmo;

   ba=blockAllocator_new2(0x10000);
   memManager=blockAllocator_alloc(ba,sizeof(*memManager));
   mem=blockAllocator_alloc(ba,sizeof(*mem));
   vmo=blockAllocator_alloc(ba,sizeof(*vmo));

   treeMemManager2(memManager);
   (*mem)=createVirt_memory(memManager,HG_(zalloc),HG_(free));
   mem->opaque=vmo;
   vmo->ba=ba;
   vmo->tree=tree;
   return mem;
}

static
void freeVirtMem2(virt_memory * mem)
{
   VirtMem2Opaque * vmo;
   vmo=mem->opaque;
   //freeTree(vmo->tree); don't do that ; we didn't create it, so it is not our job

   VG_(deleteFM)(mem->ram,NULL,NULL);
   VG_(deleteFM)(mem->temporaries,NULL,NULL);
   VG_(deleteFM)(mem->registers,NULL,NULL);

   blockAllocator_free(vmo->ba);
}
#endif


/*----------------------------------------------------------------*/
/*--- TREE Utils                                          ---*/
/*----------------------------------------------------------------*/

typedef struct s_isRecurse_CheckDat {
   //UInt * usedStmts;
#if USE_INTERPRETER_FOR_RECURSE
   virt_memory * mem;
#endif
   UInt depth;
   UInt maxDepth;
} _isRecurse_CheckDat;

static
Char _tree_isRecurse_stmt (IrsbTree * tree, Int root, _isRecurse_CheckDat * hdl);

static
Char _tree_isRecurse_expr ( IrsbTree * tree, IRExpr * e, _isRecurse_CheckDat * hdl ) {
   //UInt * usedStmts = hdl->usedStmts;

   if(PRINT_isRecurse) {
      VG_(printf)("%s",STRINGIFY(_tree_isRecurse_expr));
      ppIRExpr(e);
      VG_(printf)("\n");
   }
   switch( e->tag ) {
      case Iex_CCall: {
         return 2; // can't handle it
         break;/*
         int numArgs,i;
         IRExpr ** args=shallowCopyIRExprVec(e->Iex.CCall.args);
         for (i = 0; e->Iex.CCall.args[i] != NULL; i++) {
            IRExpr * arg=e->Iex.CCall.args[i];
            args[i]=arg;
         }*/
      } break;
      case Iex_Const: {
         return 0;
      } break;
      case Iex_Get: {
         Int stmtIdx=treeNode_register(tree,e->Iex.Get.offset,e->Iex.Get.ty);
         if( stmtIdx<0 ) {
            return 0;
         } else {
            //ppTree_Stmt(tree,stmtIdx,d);
            IRStmt * st=tree->irsb->stmts[stmtIdx];
            //usedStmts[stmtIdx]++;
            hdl->depth++;
            //if(usedStmts[stmtIdx]>3) {
            if(hdl->depth > hdl->maxDepth) {
               return 1;
            } else {
               Char t=_tree_isRecurse_expr(tree,st->Ist.Put.data,hdl);
               //usedStmts[stmtIdx]--;
               hdl->depth--;
               return t;
            }
         }
      } break;
      case Iex_GetI: {
         return 2; // can't handle it
      } break;
      case Iex_RdTmp: {
         return _tree_isRecurse_stmt(tree,treeNode_tmp(tree,e->Iex.RdTmp.tmp),hdl);
      } break;
      case Iex_Mux0X: {
         Char res=0,t;
         t=_tree_isRecurse_expr(tree,e->Iex.Mux0X.cond,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         t=_tree_isRecurse_expr(tree,e->Iex.Mux0X.expr0,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         t=_tree_isRecurse_expr(tree,e->Iex.Mux0X.exprX,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         return res;
      } break;
      case Iex_Qop: {
         Char res=0,t;
         t=_tree_isRecurse_expr(tree,e->Iex.Qop.arg1,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         t=_tree_isRecurse_expr(tree,e->Iex.Qop.arg2,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         t=_tree_isRecurse_expr(tree,e->Iex.Qop.arg3,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         t=_tree_isRecurse_expr(tree,e->Iex.Qop.arg4,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         return res;
      } break;
      case Iex_Triop: {
         Char res=0,t;
         t=_tree_isRecurse_expr(tree,e->Iex.Triop.arg1,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         t=_tree_isRecurse_expr(tree,e->Iex.Triop.arg2,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         t=_tree_isRecurse_expr(tree,e->Iex.Triop.arg3,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         return t;
      } break;
      case Iex_Binop: {
         Char res=0,t;
         t=_tree_isRecurse_expr(tree,e->Iex.Binop.arg1,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         t=_tree_isRecurse_expr(tree,e->Iex.Binop.arg2,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         return t;
      } break;
      case Iex_Unop: {
         return _tree_isRecurse_expr(tree,e->Iex.Unop.arg,hdl);
      } break;
      case Iex_Load: {
         IRExpr * addr=e->Iex.Load.addr;
         Char res = _tree_isRecurse_expr(tree,addr,hdl);
         if(res>0) return res;
#if USE_INTERPRETER_FOR_RECURSE
         if(hdl->mem) {
            Data_val resDv;
            virt_memory * mem=hdl->mem;
            tl_assert(addr->tag==Iex_RdTmp || addr->tag==Iex_Const);
            expr_interpret(e,mem,&resDv);
            {
               //VirtMem2Opaque * vmo;
               //IrsbTree * tree=vmo->tree;
               //Int stmtIdx=tree->storeIdx[resDv.data];
               //_isRecurse_Stmt(tree,stmtIdx,hdl);
            }

         }
#endif
         return res;
      } break;
      default:
         tl_assert(0);
   }
}

static
Char _tree_isRecurse_stmt (IrsbTree * tree, Int root, _isRecurse_CheckDat * hdl)
{
   IRSB* bbIn = tree->irsb;
   IRStmt* st;
   //UInt * usedStmts = hdl->usedStmts;
   Char result=0;
   tl_assert(root>=0);
   tl_assert(root<bbIn->stmts_used);
   st = bbIn->stmts[root];

   tl_assert(hdl);
   //Char FLOOD_STACK[4000];
   //Int i;
   //for(i=0;i<4000;i++) FLOOD_STACK[i]=i;
   //usedStmts[root]++;
   if(PRINT_isRecurse) {
      VG_(printf)(" depth=%d\n",hdl->depth);
   }
   hdl->depth++;

   if(hdl->depth > hdl->maxDepth) {
      return 1;
   }

   if(PRINT_isRecurse) {
      VG_(printf)("%s ",STRINGIFY(_tree_isRecurse_stmt));
      ppIRStmt(st);
      VG_(printf)(" depth=%d\n",hdl->depth);
   }

   switch (st->tag) {


      case Ist_PutI:
      case Ist_Dirty:
         /* None of these can contain anything of interest */
         result = 2; // can't handle it
         break;

      case Ist_AbiHint:
      case Ist_MBE:

      case Ist_NoOp:
      case Ist_IMark:
         result = -1;
         break;

      case Ist_Store: {
         Char res=0,t;
         t=_tree_isRecurse_expr(tree,st->Ist.Store.addr,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         t=_tree_isRecurse_expr(tree,st->Ist.Store.data,hdl);
         if(t>0) return t;
         else if(t<0) res=t;
         result = res;
      } break;

      case Ist_Put: {
         tl_assert(0);
      } break;

      case Ist_WrTmp: {
         result = _tree_isRecurse_expr(tree,st->Ist.WrTmp.data,hdl);
      } break;

      case Ist_Exit: {
         IRExpr *guard = st->Ist.Exit.guard;

         result = _tree_isRecurse_expr(tree,guard,hdl);
      } break;

      default:
         tl_assert(0);

   } /* switch (st->tag) */
   //usedStmts[root]--;
   hdl->depth--;
   return result;
}

static
Char tree_isRecurse (IrsbTree * tree, Int root)
{
   //UInt SZ=tree->irsb->stmts_used*sizeof(UInt);
   //UInt * used =alloca(SZ);
   _isRecurse_CheckDat hdl;
   Char result;
   if(PRINT_isRecurse) VG_(printf)("# tree@%d : \n",root);
   //VG_(memset)(used,0,SZ);
   //hdl.usedStmts=used;
   hdl.depth=0;
   hdl.maxDepth=tree->irsb->stmts_used+2;
   if(hdl.maxDepth>ISRECURSE_MAX_DEPTH)
      hdl.maxDepth=ISRECURSE_MAX_DEPTH;
#if USE_INTERPRETER_FOR_RECURSE
   hdl.mem=NULL;//createVirtMem2(tree); // todo
#endif
   result=_tree_isRecurse_stmt(tree,root,&hdl);

   if(PRINT_isRecurse) VG_(printf)("%s return %d \n",STRINGIFY(tree_isRecurse),result);
   //freeVirtMem2(hdl.mem);
   return result;
}

/*------------------------------------------------*/
/*--- Iterator for trees                       ---*/
/*------------------------------------------------*/

#if USE_TREE_ITERATOR

typedef
   enum e_IteratValue_Typ {
      Iv_ERROR=0,
      Iv_Stmt,
      Iv_Expr,
      //Iv_END,
      /**
       * Recursion detected
       */
      Iv_RECURSE,
   }
   IteratValue_Typ;

typedef struct s_TreeIteratValue {
   IteratValue_Typ typ;
   Int stmt_idx;
   Int curDepth;
   union {
      struct {
         IRExpr * expr;
      } expr;
      struct {
         IRStmt * stmt;
      } stmt;
   };
   // USED BY ITERATOR ALGORITHM :
   IRExpr * subExpr[5];
   Bool stmt_done;
} TreeIteratValue;

typedef struct s_TreeIterator {
   TreeIteratValue * stack;
   Int stackId;
   Int stackSize;

   IrsbTree * tree;

   Int curStmt;
   Bool loopGetPut;
   Bool loopLoadStore;
} * TreeIterator;

#define CHECK(val,a,b) (tl_assert(!(val>=a&&val<=b)));
static
TreeIteratValue * _treeIterator_allocValue(TreeIterator iter)
{
   TreeIteratValue * val2;
   DEBUG_PRINT_FILE(_treeIterator_allocValue);
   if(512&0) VG_(printf)(__AT__"(_treeIterator_allocValue) iter=%p\n",iter);
   if(iter->stackId >= iter->stackSize) return NULL;
   val2=&(iter->stack[++(iter->stackId)]);
   VG_(memset)(val2, 0, sizeof(*val2));
   return val2;
}

static
void _treeIterator_stmt ( TreeIterator iter, Int root, TreeIteratValue * val );
static
void _treeIterator_expr ( TreeIterator iter, IRExpr * e, TreeIteratValue * val ) {
   IrsbTree * tree;
   if(512&0) {
	   VG_(printf)(__AT__"iter=%p\n",iter);
	   if(0x410C667C8==iter||iter==0x410E1EBD8) {
		   static int roundCnt=0;
		   roundCnt++;
		   VG_(printf)(__AT__"roundCnt=%d\n",roundCnt);
		   if(roundCnt>431) { // 3 431
			   VG_(printf)(__AT__"\n");
		   }
	   }
   }
   tree = iter->tree;
   DEBUG_PRINT_FILE(_treeIterator_expr);
   val->expr.expr=e;
   switch( e->tag ) {
      case Iex_GetI:
      case Iex_CCall: {
         if(PRINT_LOGGING_HG_DEPENDENCY) {
            VG_(printf)("%s on ",STRINGIFY(_treeIterator_expr));
            ppIRExpr(e);
            VG_(printf)("\n");
         }
         val->typ=Iv_ERROR;
         break;
      } break;
      case Iex_Const: {
         val->typ=Iv_Expr;
      } break;
      case Iex_Get: {
         Int stmtIdx;
#ifdef __amd64
         TreeIterator iterBugWorkaround=iter; // workaround : compiler bug
         if(512&0) VG_(printf)(__AT__"iter=%p@%p\n",iter,&iter);
         VG_(printf)("",&iterBugWorkaround); // ensure compiler don't do reordering
#endif
         if(!iter->loopGetPut) {
            val->typ=Iv_Expr;
            break;
         }
         stmtIdx=treeNode_register(tree,e->Iex.Get.offset,e->Iex.Get.ty);
#ifdef __amd64
         VG_(printf)("",&iter);// ensure compiler don't do reordering
         iter=iterBugWorkaround;
#endif
         if( stmtIdx<0 ) {
            val->typ=Iv_Expr;
         } else {
            //ppTree_Stmt(tree,stmtIdx,d);
            //IRStmt * st=tree->irsb->stmts[stmtIdx];
            /*val->typ=Iv_Stmt;
            val->stmt_idx=stmtIdx;
            val->stmt.stmt=st;*/
            //treeIterator_stmt(iter,stmtIdx,val);break;
            TreeIteratValue * val2;
            if(512&0) VG_(printf)(__AT__"iter=%p\n",iter);
            val2=_treeIterator_allocValue(iter);
            if(!val2) {
               val->typ=Iv_ERROR;
               break;
            }
            _treeIterator_stmt(iter,stmtIdx,val2);
            val->typ=Iv_Expr;
         }
      } break;
      case Iex_RdTmp: {
         Int stmtIdx=treeNode_tmp(tree,e->Iex.RdTmp.tmp);
         TreeIteratValue * val2=_treeIterator_allocValue(iter);
         if(!val2) {
            val->typ=Iv_RECURSE;
            break;
         }
         _treeIterator_stmt(iter,stmtIdx,val2);
         val->typ=Iv_Expr;
      } break;
      case Iex_Mux0X: {
         val->typ=Iv_Expr;
         val->subExpr[0]=e->Iex.Mux0X.cond;
         val->subExpr[1]=e->Iex.Mux0X.expr0;
         val->subExpr[2]=e->Iex.Mux0X.exprX;
         val->subExpr[3]=NULL;
      } break;
      case Iex_Qop: {
         val->typ=Iv_Expr;
         val->subExpr[0]=e->Iex.Qop.arg1;
         val->subExpr[1]=e->Iex.Qop.arg2;
         val->subExpr[2]=e->Iex.Qop.arg3;
         val->subExpr[3]=e->Iex.Qop.arg4;
         val->subExpr[4]=NULL;
      } break;
      case Iex_Triop: {
         val->typ=Iv_Expr;
         val->subExpr[0]=e->Iex.Triop.arg1;
         val->subExpr[1]=e->Iex.Triop.arg2;
         val->subExpr[2]=e->Iex.Triop.arg3;
         val->subExpr[3]=NULL;
      } break;
      case Iex_Binop: {
         val->typ=Iv_Expr;
         val->subExpr[0]=e->Iex.Binop.arg1;
         val->subExpr[1]=e->Iex.Binop.arg2;
         val->subExpr[2]=NULL;
      } break;
      case Iex_Unop: {
         val->typ=Iv_Expr;
         val->subExpr[0]=e->Iex.Unop.arg;
         val->subExpr[1]=NULL;
      } break;
      case Iex_Load: {
         val->typ=Iv_Expr;
         val->subExpr[0]=e->Iex.Load.addr;
         val->subExpr[1]=NULL;
         tl_assert2(!iter->loopLoadStore,"NOT IMPLEMENTED\n");
      } break;
      default:
         tl_assert(0);
   }
}

static
void _treeIterator_stmt ( TreeIterator iter, Int root, TreeIteratValue * val )
{
   IrsbTree * tree=iter->tree;
   IRSB* bbIn = tree->irsb;
   IRStmt* st;
   DEBUG_PRINT_FILE(_treeIterator_stmt);
   tl_assert(root>=0);
   tl_assert(root<bbIn->stmts_used);
   st = bbIn->stmts[root];

   val->stmt_idx=root;
   val->stmt.stmt=st;
   switch (st->tag) {
      case Ist_NoOp:
      case Ist_AbiHint:
      case Ist_PutI:
      case Ist_Dirty:


      case Ist_MBE:

      case Ist_IMark:
         if(PRINT_LOGGING_HG_DEPENDENCY) {
            VG_(printf)("%s.error on ",STRINGIFY(_treeIterator_stmt));
            ppIRStmt(st);
            VG_(printf)("\n");
         }
         val->typ=Iv_ERROR;
         break;

      case Ist_Store: {
         val->typ=Iv_Stmt;
         val->subExpr[0]=st->Ist.Store.addr;
         val->subExpr[1]=st->Ist.Store.data;
         val->subExpr[2]=NULL;
      } break;

      case Ist_Put: {
         val->typ=Iv_Stmt;
         val->subExpr[0]=st->Ist.Put.data;
         val->subExpr[1]=NULL;
      } break;

      case Ist_WrTmp: {
         val->typ=Iv_Stmt;
         val->subExpr[0]=st->Ist.WrTmp.data;
         val->subExpr[1]=NULL;
      } break;

      case Ist_Exit: {
         IRExpr *guard = st->Ist.Exit.guard;
         val->typ=Iv_Stmt;
         val->subExpr[0]=guard;
         val->subExpr[1]=NULL;
      } break;

      default:
         tl_assert(0);

   } /* switch (st->tag) */
}

/*
typedef struct s_TreeIterator {
   IteratValue val;
   struct s_TreeIterator * next;
} s_TreeIterator;*/

/**
 * Creates a tree iterator starting at statement number 'stmtIdx' as root.
 * It was a depth-first iterator.
 */
static
TreeIterator treeIterator_New(IrsbTree * tree, Int stmtIdx)
{
   IRSB * irsb=tree->irsb;
   TreeIterator res;
   res=HG_(zalloc)(MALLOC_CC(treeIterator_New+res),sizeof(*res));
   res->stackSize=irsb->stmts_used*2;
   res->stack=HG_(zalloc)(MALLOC_CC(treeIterator_New+res->stack),(res->stackSize+3)*sizeof(TreeIteratValue));
   res->stackId=0;
   res->tree=tree;
   res->stack[0].typ=Iv_Stmt;
   res->stack[0].stmt_idx=stmtIdx;
   res->stack[0].stmt.stmt=irsb->stmts[stmtIdx];
   res->loopGetPut=True;
   res->loopLoadStore=False;
   _treeIterator_stmt(res,stmtIdx,&(res->stack[0]));
   return res;
}

static
void treeIterator_Free(TreeIterator iter)
{
   tl_assert(iter->stack);
   HG_(free)(iter->stack);
   iter->stack=NULL;
   HG_(free)(iter);
}

/**
 * Configure if we follow get/put and load/store associations.
 * - load/store is not implemented
 */
static inline
void treeIterator_cfgLoop(TreeIterator iter, Bool loopGetPut, Bool loopLoadStore)
{
   iter->loopGetPut=loopGetPut;
   iter->loopLoadStore=loopLoadStore;
}

/**
 * if childId<0 skips all childs
 * else skips given child
 */
static
void treeIterator_Skip(TreeIterator iter, Int childId)
{
   DEBUG_PRINT_FILE(treeIterator_Skip);
   if(childId>=0)
      iter->stack[iter->stackId].subExpr[childId]=NULL;
   else {
      Int expId;
      for(expId=0;expId<5;expId++) {
         iter->stack[iter->stackId].subExpr[expId]=NULL;
      }
   }

}

static
void ppTreeIterator(TreeIterator iter)
{
   TreeIteratValue * val;
   Int i;
   VG_(printf)(STRINGIFY(TreeIterator)"(curStmt=%d,stackId=%d,stackSize=%d)\n",iter->curStmt,iter->stackId,iter->stackSize);
   for(i=0;i<=iter->stackId;i++) {
      val=&(iter->stack[i]);
      VG_(printf)(STRINGIFY(TreeIteratorValue)"(typ=%d,curDepth=%d)",val->typ,val->curDepth);
      switch(val->typ) {
         case Iv_Expr:
            ppIRExpr(val->expr.expr);
            break;
         case Iv_Stmt:
            ppIRStmt(val->stmt.stmt);
            break;
         default:
            break;
      }
      VG_(printf)("\n");
   }
   VG_(printf)(STRINGIFY(TreeIterator)":END\n");
}

static
TreeIteratValue * treeIterator_Next(TreeIterator iter)
{
   TreeIteratValue val;
   TreeIteratValue * stackTop;
   TreeIteratValue * res;
   IrsbTree * tree;
   Bool loop;
   DEBUG_PRINT_FILE(treeIterator_Next);
   tree=iter->tree;

   if(512&0) VG_(printf)(__AT__"iter=%p\n",iter);
   do {
	   if(512&0) VG_(printf)(__AT__"(loop)iter=%p\n",iter);
      if(iter->stackId<0) {

         //res=iter->stack;
         //res->typ=Iv_END;
         res=NULL;
         break;
      }
      if(iter->stackId >= iter->stackSize) {
         res=iter->stack;
         res->typ=Iv_RECURSE;
         break;
      }
      stackTop=&(iter->stack[iter->stackId]);
      loop=True;
      switch(stackTop->typ) {
         case Iv_Stmt:
            if(!(stackTop->stmt_done)) {
               stackTop->stmt_done=True;
               res=stackTop;
               loop=False;
               break;
            }
         case Iv_Expr: {
            Int expId;
            if(512&0) VG_(printf)(__AT__"(Iv_Expr begin)iter=%p\n",iter);
            iter->curStmt=stackTop->stmt_idx;
            for(expId=0;expId<5;expId++) {
               IRExpr * e=stackTop->subExpr[expId];
               if(e) {
                  //VG_(printf)("\titer->stackId=%d, expId=%d\n\t",iter->stackId,expId);
                  //ppIRExpr(e);VG_(printf)("\n");
#ifdef __amd64
                  TreeIterator iterBugWorkaround=iter; // workaround : compiler bug
                  VG_(printf)("",&iterBugWorkaround); // ensure compiler don't do reordering
                  if(512&0) VG_(printf)(__AT__"(pre memset)iter=%p\n",iter);
#endif
                  VG_(memset)(&val, 0, sizeof(val));
#ifdef __amd64
                  if(512&0) VG_(printf)(__AT__"(post memset)iter=%p\n",iter);
#endif
                  _treeIterator_expr(iter,e,&val);
#ifdef __amd64
                  VG_(printf)("",&iter);// ensure compiler don't do reordering
                  if(iter!=iterBugWorkaround) VG_(printf)(__AT__":GCC bug workaround used\n");
                  if(512&0) VG_(printf)(__AT__"(Iv_Expr in for)iter=%p, iterBugWorkaround=%p\n",iter,iterBugWorkaround);
                  iter=iterBugWorkaround;
#endif
                  stackTop->subExpr[expId]=NULL; // mark it as done
                  break;
               }
            }
            if(512&0) VG_(printf)(__AT__"(Iv_Expr after for)iter=%p\n",iter);
            //
            if(expId==5) {
               //VG_(memset)(&(iter->stack[(iter->stackId)]), 0, sizeof(*iter->stack));
               iter->stackId--;
               //VG_(printf)("iter->stackId-- : %d\n",iter->stackId);
            } else {
               loop=False;
               if(512&0) VG_(printf)(__AT__"(before call)iter=%p\n",iter);
               res=_treeIterator_allocValue(iter);
               //res=&(iter->stack[++(iter->stackId)]);
               if(!res) {
                  res=iter->stack;
                  res->typ=Iv_RECURSE;
                  break;
               }
               // FIXME isn't it always equal to stackTop->stmt_idx ?
               val.stmt_idx=iter->curStmt;
               //VG_(printf)("iter->stackId++ : %d\n",iter->stackId);
               (*res)=val;
            }
         }break;
         /*{
            Int idx=stackTop->stmt_idx;
            if(!idx) { // statement already done
               iter->stackId--;
               break;
            }
            stackTop->stmt_idx=0;
            treeIterator_stmt(tree,idx,&val);
            if(val.typ==Iv_END) {
               iter->stackId--;
            } else {
               stackTop=NULL;
               res=&(iter->stack[++(iter->stackId)]);
               (*res)=val;
            }
         } break;*/
         default: {
            ppTreeIterator(iter);
            VG_(printf)("%s = %d\n",STRINGIFY(stackTop->typ),stackTop->typ);
            tl_assert(0);
         }
      }
   } while(loop);
   if(res) {
      res->curDepth=iter->stackId;
   }
   DEBUG_PRINT_FILE(treeIterator_Next+__return__);
   return res;
}

#endif //USE_TREE_ITERATOR


/*----------------------------------------------------------------*/
/*--- DEPENDENCY CHECKER                                          ---*/
/*----------------------------------------------------------------*/

#if USE_DEPENDENCY_CHECKER

static
void treeSimplify(IrsbTree * tree);

// for dependency checker
struct s_AddressProp {
#if USE_INTERPRETER
   Data_val res;
#endif
   OSet * neededLoads;
   Bool buildLoadDependencies_DONE;
   Bool replaceLoadByMatchingStores_DONE;
};

static
AddressProp * addressProp_new(IrsbTree * tree)
{
   AddressProp * addrProp;
   addrProp=HG_(zalloc)(MALLOC_CC(addressProp_new+addrProp),sizeof(*addrProp));
   addrProp->neededLoads=NULL;
   addrProp->buildLoadDependencies_DONE=False;
   addrProp->replaceLoadByMatchingStores_DONE=False;
   return addrProp;
}

static
void addressProp_free(struct s_AddressProp * addrP)
{
   if(addrP->neededLoads) //VG_(deleteFM)(addrP->neededLoads,NULL,NULL);
      VG_(OSetWord_Destroy)(addrP->neededLoads);
   HG_(free)(addrP);
}


static
AddressProp * treeNode_get_AddressProp(IrsbTree * tree, Int storeStmt)
{
   AddressProp * addrProp;
   if(!tree->stmtAddressProps)
      tree->stmtAddressProps=VG_(newFM)(HG_(zalloc),MALLOC_CC(treeNode_get_AddressProp+tree->stmtAddressProps),HG_(free),cmp_unsigned_Words);
   if(!VG_(lookupFM)(tree->stmtAddressProps,NULL,(UWord*)&addrProp,storeStmt)) {
      addrProp=addressProp_new(tree);
      VG_(addToFM)(tree->stmtAddressProps,storeStmt,(UWord)addrProp);
   }
   return addrProp;
}

static
void addressProp_addLoadDepend(AddressProp * addrProp, Int loadIdx)
{
   if(!addrProp->neededLoads)
      addrProp->neededLoads = VG_(OSetWord_Create)( HG_(zalloc), MALLOC_CC(addressProp_addLoadDepend+addrProp->neededLoads), HG_(free) );
            //VG_(newFM)(HG_(zalloc),"addressProp_addLoadDepend",HG_(free),cmp_unsigned_Words);
   //VG_(addToFM)(addrProp->neededLoads,loadIdx,0);
   if(!VG_(OSetWord_Contains) (addrProp->neededLoads,loadIdx))
      VG_(OSetWord_Insert)(addrProp->neededLoads,loadIdx);
}

static inline
SizeT addressProp_neededLoadsCount(AddressProp * addrProp)
{
   return VG_(OSetWord_Size)( addrProp->neededLoads );
}

static inline
void addressProp_initIter(AddressProp * addrProp)
{
   VG_(OSetWord_ResetIter)(addrProp->neededLoads);
}
static inline
Bool addressProp_nextIter(AddressProp * addrProp, /*OUT*/UWord* val )
{
   return VG_(OSetWord_Next)(addrProp->neededLoads,val);
}
static inline
void addressProp_doneIter(AddressProp * addrProp )
{
}

#if USE_INTERPRETER
static inline
void addressProp_setDataVal(AddressProp * addrProp, Data_val dv)
{
   addrProp->res=dv;
}
#endif

static
void tree_buildLoadDependencies( IrsbTree * tree, Int stmtIdx )
{
   IRSB * irsb = tree->irsb;
   Int i=stmtIdx;
   TreeIterator tIt = treeIterator_New(tree,i);
   TreeIteratValue * val;
   //AddressProp * addrP;
   struct _stack {
      AddressProp * addrP;
      Int depth;
      Int stmtIdx;
      struct _stack * next;
   } * addrPstack;
   addrPstack=NULL;
   ALLOCASTACK_ALLOC_PUSH(addrPstack);
   //addrP
   addrPstack->addrP=treeNode_get_AddressProp(tree,i);
   addrPstack->depth=-1;
   addrPstack->stmtIdx=i;

   if(addrPstack->addrP->buildLoadDependencies_DONE) {
      return;
   }
   addrPstack->addrP->buildLoadDependencies_DONE=True;

   do {
      val=treeIterator_Next(tIt);
      if(!val) break;
      while(val->curDepth<=addrPstack->depth) {
         ALLOCASTACK_POP(addrPstack);
         if(!addrPstack) break;
         //addrP=addrPstack->addrP;
      }
      switch(val->typ) {
         case Iv_Expr: {
            if(val->expr.expr->tag==Iex_Load) {
               /**
                * Add the dependency
                */
               addressProp_addLoadDepend(addrPstack->addrP,val->stmt_idx);
               if(PRINT_LOGGING_HG_DEPENDENCY) {
                  HG_(setLogFile)( LOG_FILE_HG_DEPENDENCY );
                  ppIRStmt(irsb->stmts[addrPstack->stmtIdx]);
                  VG_(printf)("\tdepends on\t");
                  ppIRStmt(irsb->stmts[val->stmt_idx]);
                  VG_(printf)("\n");
                  HG_(setLogFile)( NULL );
               }
               ALLOCASTACK_ALLOC_PUSH(addrPstack);
               addrPstack->addrP=treeNode_get_AddressProp(tree,val->stmt_idx);
               addrPstack->stmtIdx=val->stmt_idx;
               addrPstack->depth=val->curDepth;

               /* TODO
               addrPstack->addrP->buildLoadDependencies_DONE=True;
               => as we don't stop iterating, it seems to be done...
               */
            }
         }break;
         case Iv_Stmt: {
            IRStmt * st =val->stmt.stmt;
            if(st->tag==Ist_Store) {
               /**
                * Child 0 : Address
                * Child 1 : Data
                * => skip data as it wasn't usefull for dependencies
                */
               treeIterator_Skip(tIt,1);
            }
         } break;
         case Iv_RECURSE: {

            val=NULL;
         }break;
         default: {
            val=NULL;
         }break;
      }
   }while(val);
   treeIterator_Free(tIt);
}


#if !(SKIP_UNUSED)
static
void tree_buildStores( IrsbTree * tree, virt_memory * mem )
{
   Int i;
   IRSB * irsb = tree->irsb;
   Addr64 addr=0;

   //for(i=irsb->stmts_used-1;i>=0;i--) {
   for(i=0;i<irsb->stmts_used;i++) {
      if(irsb->stmts[i]->tag==Ist_IMark) {
         addr=irsb->stmts[i]->Ist.IMark.addr;
      }
      if(irsb->stmts[i]->tag==Ist_Store) {
         IRExpr * staddr = irsb->stmts[i]->Ist.Store.addr;
         Data_val res;
         //WordFM * oldRam;
         //UWord pKey,pVal,size;
         //tl_assert( ! isRecurse(tree,i) ); // TODO_ : what if ?
         /*VG_(initIterFM)(mem->ram);

         size=VG_(sizeFM)(mem->ram);
         while(VG_(nextIterFM) ( mem->ram, &pKey, &pVal )) {

         }
         oldRam = mem->ram;*/

         tree_buildLoadDependencies( tree, i );


         expr_interpret(staddr,mem,&res);
         addressProp_setDataVal(treeNode_get_AddressProp(tree,i),res);
         //mem->ram-oldRam; // used adresses
      }
   }

   ppMemory(mem);
}
#endif

/**
 * Changes IRSB
 */
static
Bool tree_simplifyExpression( IrsbTree * tree, IRTemp tmp )
{
   const Int addToSub=Iop_Sub8-Iop_Add8;
   const Int Iop_None=Iop_Not1;
   TreeIterator tIter;
   TreeIteratValue * val;
   Bool hasChanged=False;
   struct {
      /**
       * lastAddSubOp[0] = Last add
       * lastAddSubOp[1] = Last sub
       * -> used to check if they use the same data typ
       */
      IROp lastAddSubOp[2] ;
      IRStmt * lastStmt;
      /**
       * While we evaluate the operations, this holds the current sum.
       */
      Addr sumation;
   } lpt;
   lpt.lastAddSubOp[0]=Iop_None;
   lpt.lastAddSubOp[1]=Iop_None;
   lpt.lastStmt=NULL;
   lpt.sumation=0;

   tIter=treeIterator_New(tree,treeNode_tmp(tree,tmp));
   treeIterator_cfgLoop(tIter,False,False);
   do {
      val=treeIterator_Next(tIter);
      if(!val) break;
      switch(val->typ) {
         case Iv_Expr: {
            //expr.expr->tag==Iex_Load
         }break;
         case Iv_Stmt: {
            if(val->stmt.stmt->tag==Ist_WrTmp) {
               IRExpr * e=val->stmt.stmt->Ist.WrTmp.data;
               switch(e->tag) {
                  case Iex_RdTmp: {
                  }break;
                  case Iex_Binop: {
                     // tx = Add/Sub ( Y, Z )
                     IROp op = e->Iex.Binop.op;
                     IRTemp argTmp;
                     IRConst * argConst=NULL;
                     Char curIdx=0,otherIdx=1,sign=1;

                     tl_assert( isIRAtom(e->Iex.Binop.arg1)&&isIRAtom(e->Iex.Binop.arg2) );

                     if(e->Iex.Binop.arg1->tag==Iex_Const) {
                        argConst=e->Iex.Binop.arg1->Iex.Const.con;
                        if(e->Iex.Binop.arg2->tag!=Iex_RdTmp) break; // two constants
                        argTmp=e->Iex.Binop.arg2->Iex.RdTmp.tmp;
                     }
                     if(e->Iex.Binop.arg2->tag==Iex_Const) {
                        argConst=e->Iex.Binop.arg2->Iex.Const.con;
                        if(e->Iex.Binop.arg1->tag!=Iex_RdTmp) break; // two constants
                        argTmp=e->Iex.Binop.arg1->Iex.RdTmp.tmp;
                     }
                     if(!argConst) break; // no constant ; two temps

                     if(op>=Iop_Add8&&op<=Iop_Add64) ; // use default
                     else if(op>=Iop_Sub8&&op<=Iop_Sub64) {
                        curIdx=1;otherIdx=0;sign=-1;
                     } else {
                        break; // not an add nor a sub
                     }
                     /**
                      * Common code for adds and subs.
                      *   adds got : curIdx=0;otherIdx=1;sign= 1;
                      *   subs got : curIdx=1;otherIdx=0;sign=-1;
                      */
                     if( (lpt.lastAddSubOp[curIdx]!=Iop_None&&(op!=lpt.lastAddSubOp[curIdx])) ) {
                        /*
                         * Another addition typ. (ie, Add32, followed by Add8)
                         */
                        lpt.lastAddSubOp[otherIdx]=Iop_None;
                        lpt.sumation=sign*value_of(argConst);
                        lpt.lastStmt=val->stmt.stmt;
                     }
                     else if(
                           (lpt.lastAddSubOp[otherIdx]!=Iop_None&&(op+addToSub*sign!=lpt.lastAddSubOp[otherIdx]))
                           ) {
                        /*
                         * Incompatible addition typ. (ie, Sub32, followed by Add8)
                         */
                        lpt.lastAddSubOp[otherIdx]=Iop_None;
                        lpt.sumation=sign*value_of(argConst);
                        lpt.lastStmt=val->stmt.stmt;
                        if(PRINT_simplifyExpression) {
                           ppIROp(op);VG_(printf)(" INCOMPATIBLE TO ");
                           ppIROp(lpt.lastAddSubOp[0]);VG_(printf)(" ");ppIROp(lpt.lastAddSubOp[1]);
                           VG_(printf)("\n");
                        }
                     } else {
                        /* not incompatible add
                         * nor incompatible to sub
                         * -> We can accumulate
                         */
                        if(!lpt.lastStmt) {
                           tl_assert(lpt.sumation==0);
                           lpt.lastStmt=val->stmt.stmt;
                        }
                        lpt.sumation+=sign*value_of(argConst);
                     }
                     /**
                      * Save the operation type.
                      */
                     lpt.lastAddSubOp[curIdx]=op;

                     if(lpt.sumation==0) {
                        /**
                         * TODO :
                         * The normalization creates some Bugs.
                         * See why
                         * || lpt.sumation!=value_of(argConst)
                         */
                        // Simplify !!
                        tl_assert(lpt.lastStmt->tag==Ist_WrTmp);
                        if(PRINT_simplifyExpression) {
                           VG_(printf)("SIMPLIFY EXPR : cur=");
                           ppIRStmt(val->stmt.stmt);
                           VG_(printf)("\n");
                           ppIRStmt(lpt.lastStmt);
                        }

                        /**
                         * Change the instruction
                         */
                        if(lpt.sumation==0) {
                           // tx = ty
                           tl_assert(lpt.lastStmt->Ist.WrTmp.tmp!=argTmp);
                           lpt.lastStmt->Ist.WrTmp.data=IRExpr_RdTmp(argTmp); // in place
                           hasChanged=True;
                        } else {
                           // TODO dissabled code - see why it doesn't work
                           /**
                            * Normalize it to get an Add
                            * - Sub -> equivalent Add
                            * - Add(Add()) -> One "great" Add
                            */
                           IROp addOp = lpt.lastAddSubOp[0];
                           IRExpr * origTmp;
                           IRConst * origCon;
                           if(addOp==Iop_None) {
                              tl_assert(lpt.lastAddSubOp[1]!=Iop_None);
                              addOp = lpt.lastAddSubOp[1]-addToSub;
                           }
                           if(lpt.lastStmt->Ist.WrTmp.data->Iex.Binop.arg1->tag==Iex_RdTmp) {
                              origTmp = lpt.lastStmt->Ist.WrTmp.data->Iex.Binop.arg1;
                              origCon = lpt.lastStmt->Ist.WrTmp.data->Iex.Binop.arg2->Iex.Const.con;
                           } else {
                              origTmp = lpt.lastStmt->Ist.WrTmp.data->Iex.Binop.arg2;
                              origCon = lpt.lastStmt->Ist.WrTmp.data->Iex.Binop.arg1->Iex.Const.con;
                           }

                           // in place
                           lpt.lastStmt->Ist.WrTmp.data=IRExpr_Binop(addOp,origTmp,IRExpr_Const( createConst( origCon->tag, lpt.sumation ) ));
                        }

                        if(PRINT_simplifyExpression) {
                           VG_(printf)(" TO ");
                           ppIRStmt(lpt.lastStmt);
                           VG_(printf)("\n");
                        }
                     } else {
                        if(PRINT_simplifyExpression) {
                           VG_(printf)("Simplify work : ");
                           ppIRStmt(val->stmt.stmt);
                           VG_(printf)("\n struct=");
                           ppIROp(lpt.lastAddSubOp[0]);VG_(printf)(" ");ppIROp(lpt.lastAddSubOp[1]);
                           VG_(printf)(" ");
                           if(lpt.lastStmt)ppIRStmt(lpt.lastStmt);
                           VG_(printf)(" %lld\n",(Long)lpt.sumation);
                        }
                     }
                  } break;
                  default: {
                     //treeIterator_Skip(tIt,-1);
                     if(PRINT_simplifyExpression) {
                        VG_(printf)("Invalidate : ");
                        ppIRStmt(val->stmt.stmt);
                        VG_(printf)("\n");
                     }
                     lpt.lastAddSubOp[0]=Iop_None;
                     lpt.lastAddSubOp[1]=Iop_None;
                     lpt.lastStmt=NULL;
                     lpt.sumation=0;
                  }break;
               }
            } else {
               ppIRStmt(val->stmt.stmt);
               tl_assert(0); // should not happen... I think
            }
         }break;
         case Iv_RECURSE: {

            val=NULL;
         }break;
         default: {
            val=NULL;
         }break;
      }
   }while(val);
   treeIterator_Free(tIter);
   /**
    * TODO_ What does hasChanged mean ?
    */
   return hasChanged;
}

static
Bool tree_removeUselessRegisterLoops( IrsbTree * tree )
{
   UWord registerOffset,registerWithOffset;
   UWord stmtIndex;
   TreeIterator tIter;
   TreeIteratValue * val;
   Bool canBeRemoved;
   Bool isLooping;
   Bool hasChanged=False;
   //Int removable[tree->irsb->tyenv]
   struct _removable {
      struct _removable * next;
      Int removable;
   } * removableStack=NULL;
   VG_(initIterFM) ( tree->regLoopDefs );
   while(VG_(nextIterFM) ( tree->regLoopDefs, &registerWithOffset, &stmtIndex )) {
      registerOffset=treeNode_regOffset(registerWithOffset);
      tIter=treeIterator_New(tree, stmtIndex);
      canBeRemoved=True;
      isLooping=False;
      do {
         val=treeIterator_Next(tIter);
         if(!val) break;
         switch(val->typ) {
            case Iv_Expr: {
               IRExpr *e=val->expr.expr;
               switch(e->tag) {
                  case Iex_Get: {
                     if(e->Iex.Get.offset!=registerOffset) {
                        canBeRemoved=False;
                     } else {
                        // don't continue loop : need skipping in statement
                        //treeIterator_Skip(tIter,-1);
                        isLooping=True;
                     }
                  }break;
                  case Iex_RdTmp: break;
                  default : {
                     canBeRemoved=False;
                  }break;
               }
            }break;
            case Iv_Stmt: {
               IRStmt * st=val->stmt.stmt;
               if(st->tag==Ist_Put) {
                  if(st->Ist.Put.offset==registerOffset) {
                     // we have to skip here, due to iterator implementation
                     treeIterator_Skip(tIter,-1);
                  }
               }
            }break;
            case Iv_RECURSE: {
               // should't happen as we skip the recursions on registers
               tl_assert(0);
            }break;
            case Iv_ERROR: {
               canBeRemoved=False;
            }break;
            default: {
               tl_assert(0);
            }break;
         }
      }while(val&&canBeRemoved);
      if(canBeRemoved) {
         hasChanged=True;
         ALLOCASTACK_ALLOC_PUSH(removableStack);
         removableStack->removable=registerWithOffset; // need complete key
      }
      treeIterator_Free(tIter);
   }
   VG_(doneIterFM) ( tree->regLoopDefs );
   while(removableStack) {
      if(PRINT_removeUselessRegisterLoops) VG_(printf)("removeUselessRegisterLoops : r(%d)\n",removableStack->removable);

      tl_assert(treeNode_delRegisterWithOffset(tree,removableStack->removable));
      ALLOCASTACK_POP(removableStack);
   }
   if(hasChanged && PRINT_removeUselessRegisterLoops) {
      UWord cnt=VG_(sizeFM)( tree->regLoopDefs );
      if(cnt) VG_(printf)("Keep just %d loops\n",(Int)cnt) ;
      else VG_(printf)("No loops anymore !\n") ;
   }
   return hasChanged;
}

static
Bool tree_isSameSubExpressionTree(IrsbTree * tree, IRExpr * addr1, IRExpr * addr2)
{
   TreeIterator tIter1,tIter2;
   TreeIteratValue * val1, *val2;
   Bool result=True;
   tl_assert(addr1->tag==Iex_RdTmp && addr2->tag==Iex_RdTmp);

   tl_assert(tree->wasSimplified);

   tIter1=treeIterator_New(tree,treeNode_tmp(tree,addr1->Iex.RdTmp.tmp));
   tIter2=treeIterator_New(tree,treeNode_tmp(tree,addr2->Iex.RdTmp.tmp));
   do {
      val1=treeIterator_Next(tIter1);
      val2=treeIterator_Next(tIter2);
      if(!val1) {
         if(val2) result=False;
         break;
      }
      if(val1->typ!=val2->typ) {
         result=False;
         break;
      }
      switch(val1->typ) {
         case Iv_Expr: {
            IRExpr *e1=val1->expr.expr;
            IRExpr *e2=val2->expr.expr;
            //retryRdTmpskip:
            if(e1->tag!=e2->tag) {

               /*while(e1->tag==Iex_RdTmp) {
                  do {
                     val1=treeIterator_Next(tIter1);
                     if(val1&&val1->typ==Iv_Expr) break;
                  }while(val1);
                  if(!val1)break;
                  e1=val1->expr.expr;
                  goto retryRdTmpskip;
               }
               while(e2->tag==Iex_RdTmp) {
                  do {
                     val2=treeIterator_Next(tIter2);
                     if(val2&&val2->typ==Iv_Expr) break;
                  }while(val2);
                  if(!val2)break;
                  e2=val2->expr.expr;
                  goto retryRdTmpskip;
               }*/
               result=False;
               val1=NULL; // exit
               break;
            }
            switch(e1->tag) {
               case Iex_Get: {
                  if(e1->Iex.Get.offset!=e2->Iex.Get.offset) {
                     // we don't handle if two registers have same content
                     result=False;
                     val1=NULL; // exit
                     break;
                  }
               }break;
               case Iex_RdTmp: {
                  // TODO what if for one of them, their is a copy ?
                  // expr1 : t8
                  // expr2 : t9=t8
                  // -> can happen if it isn't optimised
               }break;
               case Iex_Binop: {
                  if(e1->Iex.Binop.op!=e2->Iex.Binop.op) {
                     /*
                     TODO : Add(a,-b) == Sub(a,b)
                     IROp op1=e1->Iex.Binop.op,op2=e2->Iex.Binop.op;
                     if(
                           ((op1>=Iop_Add8&&op1<=Iop_Add64)
                           &&(op2>=Iop_Sub8&&op2<=Iop_Sub64))
                           ||
                           ((op2>=Iop_Add8&&op2<=Iop_Add64)
                           &&(op1>=Iop_Sub8&&op1<=Iop_Sub64))
                           ) {
                        IRConst * c1, *c2;
                        if(e1->Iex.Binop.arg1->tag==Iex_Const);
                        value_of()
                     }*/
                     result=False;
                     val1=NULL; // exit
                     break;
                  }
               }break;
               case Iex_Triop: {
                  if(e1->Iex.Triop.op!=e2->Iex.Triop.op) {
                     result=False;
                     val1=NULL; // exit
                     break;
                  }
               }break;
               case Iex_Qop: {
                  if(e1->Iex.Qop.op!=e2->Iex.Qop.op) {
                     result=False;
                     val1=NULL; // exit
                     break;
                  }
               }break;
               case Iex_Unop: {
                  if(e1->Iex.Unop.op!=e2->Iex.Unop.op) {
                     result=False;
                     val1=NULL; // exit
                     break;
                  }
               }break;
               case Iex_Const: {
                  if(!eqIRAtom(e1,e2)) {
                     result=False;
                     val1=NULL; // exit
                     break;
                  }
               } break;
               case Iex_Load: {
                  // look if same address tree
                  break;
               }
               default : {
                  result=False;
                  val1=NULL; // exit
               }break;
            }
         }break;
         case Iv_Stmt: {
            //IRStmt * st1=val1->stmt.stmt;
         }break;
         case Iv_RECURSE:
         case Iv_ERROR: {
            result=False;
            val1=NULL; // exit
         }break;
         default: {
            tl_assert(0);
         }break;
      }
   }while(val1&&val2);

   treeIterator_Free(tIter1);
   treeIterator_Free(tIter2);

   return result;
}

static
Bool tree_isSameAddress(IrsbTree * tree, IRExpr * addr1, IRExpr * addr2) {
   // pre : addr1 and addr2 are atomic (temporaries or const)
   Bool res;
   if(eqIRAtom(addr1,addr2)) {
      return True;
   }
   if(addr1->tag==Iex_Const || addr2->tag==Iex_Const) {
      return False;
   }
   // now we got two different temporaries
   res=tree_isSameSubExpressionTree(tree, addr1, addr2);
   if(PRINT_LOGGING_HG_DEPENDENCY) {
      if(res) {
         VG_(printf)("isSameTree return true !\n");
         ppIRExpr(addr1);
         VG_(printf)(" == ");
         ppIRExpr(addr2);
         VG_(printf)("\n");
      } else if(0) {
         ppIRSB(tree->irsb);
         VG_(printf)("isSameTree return false !\n");
         ppIRExpr(addr1);
         VG_(printf)(" != ");
         ppIRExpr(addr2);
         VG_(printf)("\n");
      }
   }
   return res;
}

/**
 * TODO : use this
 */
Bool HG_(tree_isSameAddress)(IrsbTree * tree, IRExpr * addr1, IRExpr * addr2)
{
   if(!tree->wasSimplified)
   {
      /**
       * Handles correctly non recursive tree (without loops)
       */
      treeSimplify(tree);
   }

   return tree_isSameAddress( tree, addr1, addr2 );

}


static
Bool tree_replaceLoadByMatchingStores(IrsbTree * tree, Int loadStmtIdx)
{
   //IRStmt * st=val->stmt.stmt;
   IRExpr * e;
   IRSB * irsb=tree->irsb;
   Bool found=False;
   IRStmt * stmt = irsb->stmts[ loadStmtIdx ];
   if(stmt->tag!=Ist_WrTmp) {
      return False;
   }
   DEBUG_PRINT_FILE(tree_replaceLoadByMatchingStores);
   e=stmt->Ist.WrTmp.data;
   if(e->tag==Iex_Load) {
      Int stStmt;
      IRExpr * ldAddr=e->Iex.Load.addr;
      //e->Iex.Load.addr
      tl_assert(isIRAtom(ldAddr));
      //eqIRAtom

      if(PRINT_LOGGING_HG_DEPENDENCY) {
         HG_(setLogFile)( LOG_FILE_HG_DEPENDENCY );
         ppIRStmt(stmt);VG_(printf)(" FIND MATCHING STORE\n");
         if(ldAddr->tag==Iex_RdTmp) {
            ppIRStmt(irsb->stmts[treeNode_tmp(tree,ldAddr->Iex.RdTmp.tmp)]);
         }
         VG_(printf)(" ~\n");
         HG_(setLogFile)( NULL );
      }

      //ppIRExpr(e);
      // FIND MATCHING STORE
      // DONE : find LAST store before load
      //for(stStmt=0;stStmt<irsb->stmts_used;stStmt++)
      for(stStmt=loadStmtIdx-1;stStmt!=loadStmtIdx;stStmt--)
      {
         IRStmt * st;
         if(stStmt<0) stStmt=irsb->stmts_used-1;
         st=irsb->stmts[ stStmt ];
         if(st->tag==Ist_Store) {
            AddressProp * addrP = treeNode_get_AddressProp(tree,stStmt);
            do{
               if(addrP->replaceLoadByMatchingStores_DONE) {
                  break;
               } // avoid infinite loops
               addrP->replaceLoadByMatchingStores_DONE=True;
               tree_buildLoadDependencies(tree, stStmt);
               if(addrP->neededLoads && addressProp_neededLoadsCount(addrP)>0) {
                  UWord loadIdx;
                  Bool hasChanged;
                  do {
                     hasChanged=False;
                     addressProp_initIter ( addrP );
                     while(addressProp_nextIter( addrP , &loadIdx )) {
                        hasChanged |= tree_replaceLoadByMatchingStores(tree, loadIdx);
                     }
                     addressProp_doneIter ( addrP );
                     found|=hasChanged; // really ?
                  }while(hasChanged);
               }
            }while(0);

            if(tree_isSameAddress(tree, st->Ist.Store.addr,ldAddr)) {
               if(PRINT_LOGGING_HG_DEPENDENCY) {
                  HG_(setLogFile)( LOG_FILE_HG_DEPENDENCY );
                  VG_(printf)("Load-Store match ; isSameAddress(");
                  ppIRExpr(st->Ist.Store.addr);
                  VG_(printf)(" , ");
                  ppIRExpr(ldAddr);
                  VG_(printf)(")\n\t");
                  ppIRStmt(st);VG_(printf)(" <~> ");ppIRStmt(stmt);
                  VG_(printf)("\n");
                  HG_(setLogFile)( NULL );
               }

               {
                  // such things can happen :
                  // t9 = Ld(t0)
                  // St(t0) = t9
                  // -> finally, we get t9=t9
                  ///   ~~ St(t0) = Ld(t0)
                  IRExpr * stDat=st->Ist.Store.data;
                  if(stDat->tag==Iex_RdTmp && stDat->Iex.RdTmp.tmp==stmt->Ist.WrTmp.tmp) {
                     // TODO : should we permit them ?
                     if(PRINT_LOGGING_HG_DEPENDENCY) {
                        VG_(printf)("Avoid t%d = t%d\n",stmt->Ist.WrTmp.tmp,stDat->Iex.RdTmp.tmp);
                     }
                     break; // avoid t9=t9
                     ppIRSB(irsb);
                     ppIRStmt(st);VG_(printf)(" <~> ");ppIRStmt(stmt);
                     tl_assert2(0, "We will have a loop !\n");
                  }
               }

               // in place
               stmt->Ist.WrTmp.data=st->Ist.Store.data;


               if(PRINT_LOGGING_HG_DEPENDENCY) {
                  HG_(setLogFile)( LOG_FILE_HG_DEPENDENCY );
                  VG_(printf)("Load changed to:\n\t");
                  ppIRStmt(stmt);
                  VG_(printf)("\n");
                  HG_(setLogFile)( NULL );
               }
               if(isIRAtom(stmt->Ist.WrTmp.data)) {
                  tree_optimiseAtomicAlias(tree);
               }
               found=True;
               break;
            } else if(PRINT_LOGGING_HG_DEPENDENCY){
               HG_(setLogFile)( LOG_FILE_HG_DEPENDENCY );
               VG_(printf)("Load<>Store ; isSameAddress(");
               ppIRExpr(st->Ist.Store.addr);
               VG_(printf)(" , ");
               ppIRExpr(ldAddr);
               VG_(printf)(")");
               //ppIRStmt(st);VG_(printf)(" , ");ppIRStmt(stmt);
               {
                  VG_(printf)("\n");
                  ppTreeExt(tree,stStmt,True,0,10,False/*Don't follow getLoops*/,True);
                  VG_(printf)("\n <> ");
#ifndef NDEBUG
                  ppTreeExt(tree,ppTreeGetStmtIdx(tree,stmt),True,0,10,False,True);
#endif
                  VG_(printf)("\n");
               }
               VG_(printf)("\n");
               HG_(setLogFile)( NULL );
            }
         }
      }
   }
   DEBUG_PRINT_FILE(tree_replaceLoadByMatchingStores+__return__);
   return found;
}

static
void treeSimplify(IrsbTree * tree)
{
   IRTemp tmpS;
   Bool hasChanged=False;
   tree->wasSimplified = True;
   if(PRINT_isConstTest) VG_(printf)("DO simplifyExpression\n");
   for(tmpS=0;tmpS<tree->irsb->tyenv->types_used;tmpS++) {
      if(tree->tstruct[tmpS]<0) continue;
      if(PRINT_isConstTest) VG_(printf)("DO simplifyExpression.t%d\n",tmpS);
      hasChanged|=tree_simplifyExpression(tree,tmpS);
   }
   if(hasChanged) {
      if(PRINT_isConstTest) {
         VG_(printf)("DONE simplifyExpression\n");
         ppIRSB(tree->irsb);
      }
      tree_removeUselessRegisterLoops( tree );

      tree_optimiseAtomicAlias( tree );

      if(PRINT_isConstTest) {
         VG_(printf)("DONE optimiseTmpCopies\n");
         ppIRSB(tree->irsb);
         VG_(printf)("DONE optimiseTmpCopies : tree\n");
         HG_(ppTree_irsb)(tree);
      }
   }
}

Bool HG_(isConstTest)( IrsbTree * tree, Int testIndex )
{
   IRSB * irsb=tree->irsb;
   IRStmt * jump;
   IRExpr * guard;
   //virt_memory * mem;
   //Data_val res;
   Char isRecursive;

   jump=irsb->stmts[testIndex];
   tl_assert(jump->tag==Ist_Exit);
   guard = jump->Ist.Exit.guard;
   tl_assert(guard->tag==Iex_RdTmp);

   if(!tree->wasSimplified) {
      treeSimplify(tree);
   }

   isRecursive=tree_isRecurse(tree,testIndex);
   tl_assert(isRecursive>=0);
   if(isRecursive) return False;

   {
         TreeIterator tIt = treeIterator_New(tree,testIndex);
         TreeIteratValue * val;

         do {
            val=treeIterator_Next(tIt);
            DEBUG_VG_PRINTF(HG_(isConstTest),0,"tIt=%p, val=%p",tIt,val);
            if(!val) break;
            switch(val->typ) {
               case Iv_Expr: {
                  //expr.expr->tag==Iex_Load
               }break;
               case Iv_Stmt: {
                  if(val->stmt.stmt->tag==Ist_WrTmp) {
                     IRStmt * stmt=val->stmt.stmt;
                     IRExpr * e=stmt->Ist.WrTmp.data;
                     if(e->tag==Iex_Load) {
                        //treeIterator_Skip(tIt,-1);
                        Bool hasChangedOnce;
                        tl_assert( tree->irsb->stmts[ val->stmt_idx ] == stmt );
                        hasChangedOnce=tree_replaceLoadByMatchingStores(tree,val->stmt_idx);
                        /*while(hasChangedOnce) {
                           Int clrStmt;
                           for(clrStmt=0;clrStmt<irsb->stmts_used;clrStmt++) {
                              treeNode_get_AddressProp(tree,clrStmt)->replaceLoadByMatchingStores_DONE=False;
                           }
                           if(!replaceLoadByMatchingStores(tree,stmt)) break;
                        }*/
                        if(hasChangedOnce) {
                           treeIterator_Free(tIt);
                           tIt = treeIterator_New(tree,testIndex);
                        }
                     }
                  }
               }break; // TODO : remove Store data
               case Iv_RECURSE: {

                  val=NULL;
               }break;
               default: {
                  val=NULL;
               }break;
            }
         }while(val);
         DEBUG_VG_PRINTF(HG_(isConstTest),0,"exiting while(val)");
         treeIterator_Free(tIt);
   }

   isRecursive=tree_isRecurse(tree,testIndex);
   tl_assert(isRecursive>=0);
   if(isRecursive) {
      DEBUG_PRINT_FILE(HG_(isConstTest)+__return__);
      return False;
   }

   DEBUG_PRINT_FILE(HG_(isConstTest)+__return__);
   /**
    * Here we can add some step with the interpreter.
    */
   return True;

#if 0
   mem = createVirtMem2(tree);

   evaluateTmp(tree, guard->Iex.RdTmp.tmp,mem,&res);

   freeVirtMem2(mem);

   tl_assert(0);
   return True;
#endif
}


void HG_(getDependendLoadAddresses)( IrsbTree * tree, Int stmtIdx, Addr64 * addresses, Int maxSize )
{
   AddressProp * addrP;
   IRSB * irsb=tree->irsb;
   UWord pKey;
   Int i;
   Int addrIdx=0;

   DEBUG_PRINT_FILE(HG_(getDependendLoadAddresses));

   tree_buildLoadDependencies(tree, stmtIdx);
   addrP=treeNode_get_AddressProp(tree, stmtIdx);

   if(!addrP->neededLoads) {
      //ppIRSB(tree->irsb);
      //ppTree(tree,stmtIdx);
      //VG_(printf)("\nHas no load dependencies\n");
      //tl_assert(0);
      addresses[0]=0;
      DEBUG_PRINT_FILE(HG_(getDependendLoadAddresses)+__return__);
      return;
   }
   addressProp_initIter(addrP);
   while(addressProp_nextIter(addrP,&pKey))
   {
      for(i=pKey;i>=0;i--) {
         IRStmt * st=irsb->stmts[i];
         if(st->tag==Ist_IMark) {
            addresses[addrIdx++]=st->Ist.IMark.addr;
            break;
         }
      }
      if(addrIdx==maxSize) {
         addrIdx--;
         break;
      }
   }
   addresses[addrIdx]=0;
   addressProp_doneIter(addrP);
   DEBUG_PRINT_FILE(HG_(getDependendLoadAddresses)+__return__);
}


#endif // USE_DEPENDENCY_CHECKER


/*----------------------------------------------------------------*/
/*--- ????????????                                          ---*/
/*----------------------------------------------------------------*/

#if USE_VIRTUAL_MEMORY_V2
static void createTestMem(virt_memory * virtmem,IrsbTree * tree)
{
   static MemManager memManager;
   static virt_memory mem;
   static IrsbTree * prev=NULL;
   if(prev!=tree) {
      treeMemManager(&memManager);
      mem=createVirt_memory(&memManager,HG_(zalloc),HG_(free));
      mem.opaque=tree;
      prev=tree;
   } else {
      VG_(printf)("createTestMem.same\n");
   }
   (*virtmem)=mem;
}
#endif

#if !(SKIP_UNUSED) && USE_VIRTUAL_MEMORY_V2
static
void testEval_0(IrsbTree * tree){
   Int i;
   IRSB * bbOut = tree->irsb;
   Addr64 addr=0;

   virt_memory mem;
   createTestMem(&mem,tree);

   for(i=bbOut->stmts_used-1;i>=0;i--) {
   //for(i=0;i<bbOut->stmts_used;i++) {
      if(bbOut->stmts[i]->tag==Ist_IMark) {
         addr=bbOut->stmts[i]->Ist.IMark.addr;
      }
      if(bbOut->stmts[i]->tag==Ist_Store) {
         IRExpr * staddr = bbOut->stmts[i]->Ist.Store.addr;
         Data_val res;
         VG_(printf)("# store-tree@%llx : ",addr);
         expr_interpret(staddr,&mem,&res);
         VG_(printf)("\t= %lx\n",res.data);
      }
   }
}
#endif

#if !(SKIP_UNUSED) && USE_VIRTUAL_MEMORY_V2
static
void testEval(IrsbTree * tree){
   Int i;
   IRSB * bbOut = tree->irsb;
   Addr64 addr=0;

   static virt_memory * mem=NULL;
   static IrsbTree * prev=NULL;
   //ppTree(tree,tree->irsb->stmts_used-1);
   if(prev!=tree) {
      if(mem) freeVirtMem2(mem);
      mem=createVirtMem2(tree);
      prev=tree;
   }
   //ppTree(tree,tree->irsb->stmts_used-1);

#if 0
   {
      BlockAllocator * ba;
      Int i,j,sa;
      ba=blockAllocator_new2(0x200);
      createVirtMem2(tree);

      for(i=2;i<0xFF;i++) {
         Char * al=blockAllocator_alloc(ba,i);
         sa+=i;
         VG_(printf)("%d alloc at %lx / %d\n",i,al,sa);
         for(j=0;j<i;j++) {
            al[j]=i;
         }
      }
   }
#endif

   for(i=bbOut->stmts_used-1;i>=0;i--) {
   //for(i=0;i<bbOut->stmts_used;i++) {
      if(bbOut->stmts[i]->tag==Ist_IMark) {
         addr=bbOut->stmts[i]->Ist.IMark.addr;
      }
      if(bbOut->stmts[i]->tag==Ist_Store) {
         IRExpr * staddr = bbOut->stmts[i]->Ist.Store.addr;
         Data_val res;
         VG_(printf)("# store-tree@%llx : ",addr);
         expr_interpret(staddr,mem,&res);
         VG_(printf)("\t= %lx\n",res.data);
         {
            TreeIterator tIt = treeIterator_New(tree,i);
            TreeIteratValue * val;
            Bool loop=True;
            VG_(printf)("ITERATOR::\n");
            do {
               val=treeIterator_Next(tIt);
               if(!val) break;
               switch(val->typ) {
                  case Iv_Expr: {
                     ppIRExpr(val->expr.expr);
                     VG_(printf)("  /  l%d, d%d\n",val->stmt_idx,val->curDepth);
                  }break;
                  case Iv_Stmt: {
                     ppIRStmt(val->stmt.stmt);
                     VG_(printf)("  /  l%d, d%d\n",val->stmt_idx,val->curDepth);
                  }break;
                  case Iv_RECURSE: {
                     VG_(printf)("Iv_RECURSE\n");
                  }
                  default:
                     loop=False;
               }
            }while(loop);
            VG_(printf)("END:ITERATOR\n");
            treeIterator_Free(tIt);
         }
      }
   }

   tree_buildStores(tree,mem);
}
#endif

void HG_(ppTree)(IrsbTree * tree, Int root)
{
   ppTree ( tree, root);
}

IrsbTree * HG_(newTree)( IRSB* bbIn )
{
   IrsbTree * tree=HG_(zalloc)("newTree",sizeof(*tree));
   tree_init ( tree, bbIn );
   return tree;
}

void HG_(freeTree)( IrsbTree * tree )
{
   _tree_freeData(tree);
   tree->irsb=NULL;
   tree->tstruct=NULL;
   HG_(free)(tree);
}



#if !(SKIP_UNUSED)
// TODO_ : delete this.
//  was used to avoid "non used function" compiler warnings
IRSB * HG_(foo_avoidNonUsedFuncts)( void );
IRSB * HG_(foo_avoidNonUsedFuncts)( void )
{
   void *p;
   p=stmt_interpret;
   p=freeData_val;
   p=loadTmp;
   //p=build_Tree_stores;
   //p=isConstTest;
   p=testEval;
   p=testEval_0;
   return (IRSB *)p;
}
#endif
