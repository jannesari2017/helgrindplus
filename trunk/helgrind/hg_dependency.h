/*
 * hg_dependency.h
 *
 * (c) 2009 Univercity of Karlsruhe, Germany
 */

#ifndef HG_DEPENDENCY_H_
#define HG_DEPENDENCY_H_

// BEGIN:do I need all this TODO
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
// END:do I need all this


#include "hg_basics.h"
//#include "hg_lock_n_thread.h" // ThrLoopExtends

//#define HG_(str) VGAPPEND(vgHelgrind_,str)



typedef  struct _IrsbTree  IrsbTree;  /* opaque */

/**
 * Create a IrsbTree for bbIn.
 *  - link read of temporaries to their assignment
 *  - link read (GET) of registers to the last PUT
 *    (bbIn was assumed to be a loop - the last PUT was executed before the first GET).
 *
 * @pre HG_(optimise_irsb_fast) should be called before on bbIn
 */
IrsbTree * HG_(newTree)( IRSB* bbIn );

/**
 * Free the data associated to 'tree'
 */
void HG_(freeTree)( IrsbTree * tree );

/**
 * Print the tree starting on statement 'root' (ie, the root of the tree).
 *
 * An IrsbTree is in fact not a tree, but a forest ; you can start from each
 *  statement of the underlying IRSB and get different trees for each chosen root.
 */
void HG_(ppTree)(IrsbTree * tree, Int root);

/**
 * Make a 'ppIRSB' but adds corresponding tree after each line.
 */
void HG_(ppTree_irsb)(IrsbTree * tree);

/**
 * Concatenate bbIn to bbOut, and renames the temporaries accordingly.
 */
void HG_(concat_irsb) (
               IRSB* bbIn,
               IRSB* bbOut, /*out*/
               Addr64 skipAddr,
               VexGuestExtents* vge,
               VgCallbackClosure* closure,
               VexGuestLayout* layout
                );

//IRSB * HG_(optimise_irsb) ( IRSB * irsb, Addr64 sbBase );

/**
 * Optimize irsb :
 *  - remove redundant GET for the same register.
 *  - remove aliases ( like  t5 = t3   OR   t5 = <constant> )
 */
void HG_(optimise_irsb_fast) ( IRSB * irsb );

/**
 *  Check if the statement testIndex was "constant".
 *  The statement has to be a "Ist_Exit" statement.
 */
Bool HG_(isConstTest)( IrsbTree * tree, Int testIndex );

/**
 * Get the addresses of the code-intructions which contains a Load
 *  that is a direct dependency for instruction nr stmtIdx.
 * maxSize is the size of the output buffer addresses.
 *   When all dependencies where written in addresses, a 0 was added to signal the end of output.
 *
 * Use: for(int i=0;addresses[i];i++) { do_something_with(addresses[i]); }  to handle output.
 */
void HG_(getDependendLoadAddresses)( IrsbTree * tree, Int stmtIdx, Addr64 * addresses, Int maxSize );

//-------------------------------------------------



#endif /* HG_DEPENDENCY_H_ */
