

#ifndef HG_INTERVAL_H_
#define HG_INTERVAL_H_

#include "hg_basics.h"

#include "pub_tool_basics.h"


typedef struct {
   UWord start;
   UWord end;
   /*
    * Opaque not handled correctly (in join operation)
    */
   void * opaque;
} Interval;


typedef struct _IntervalUnion IntervalUnion;

IntervalUnion * HG_(newIvUnion)( HChar* cc );
void HG_(freeIvUnion) ( IntervalUnion * ivu );
Bool HG_(ivUnionAddInterval)(IntervalUnion * ivu, HWord start, HWord end, void*opaque);

void HG_(ivUnionDump_ext)(IntervalUnion * ivu1, Bool withFnName);
void HG_(ivUnionDump)(IntervalUnion * ivu1);
void HG_(ivUnionAddIvUnion)(IntervalUnion * ivuDest, IntervalUnion * ivuSrc);
Interval * HG_(ivUnionContains)(IntervalUnion * ivu1, HWord value);

Bool HG_(ivUnion_isSane)(IntervalUnion * ivu);

IntervalUnion * HG_(ivUnionIntersect)(IntervalUnion * ivu1, IntervalUnion * ivu2);

Int HG_(ivUnion_cntParts)(IntervalUnion * ivu);
void HG_(initIterIvu) ( IntervalUnion * ivu );
void HG_(doneIterIvu) ( IntervalUnion * ivu );
Interval * HG_(nextIterIvu) ( IntervalUnion * ivu );


/**
 * retun 0 if equal, otherwise non zero
 *   consistent : HG_(ivUnionCompare)(a,b) == -HG_(ivUnionCompare)(b,a)
 */
Int HG_(ivUnionCompare)(IntervalUnion * ivu1, IntervalUnion * ivu2);
UWord HG_(ivUnionHash)(IntervalUnion * ivu1);

void HG_(ivUnionSetOpaque)(IntervalUnion * ivu1, void*opaque);
void * HG_(ivUnionGetOpaque)(IntervalUnion * ivu1);

#endif
