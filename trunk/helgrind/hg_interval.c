

#include "pub_tool_basics.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_wordfm.h"

#include "pub_tool_xarray.h"


#include "hg_basics.h"

#include "hg_interval.h"

// -------------

#define IV_UNION_MAGIC ((UInt)0xCA15CCEC)
#define iv_magic_assert(__ivu) tl_assert2((__ivu)->iv_magic==IV_UNION_MAGIC,"%p not a IntervalUnion !\n",(__ivu))

struct _IntervalUnion {
   UInt iv_magic;
   /*void*     (*alloc)(HChar*,SizeT);

   void      (*dealloc)(void*);*/
   HChar*    cc;
   WordFM * collect;
   void * opaque;
};

static
void joinInterval(IntervalUnion * ivu, Interval * dest, Interval * src, Bool upDateIvu)
{
   if(src->start<dest->start) {
      if(upDateIvu) {
         tl_assert(VG_(delFromFM)(ivu->collect,NULL,NULL,(UWord)dest->start));
         tl_assert(!VG_(addToFM)(ivu->collect,(UWord)src->start,(UWord)dest));
      }
      dest->start=src->start;
   }
   if(src->end>dest->end) {
      if(upDateIvu) {
         tl_assert(VG_(delFromFM)(ivu->collect,NULL,NULL,(UWord)dest->end));
         tl_assert(!VG_(addToFM)(ivu->collect,(UWord)src->end,(UWord)dest));
      }
      dest->end=src->end;
   }
}

static
Bool ivUnion_isSane(IntervalUnion * ivu)
{
   Interval * ivc;
   UWord key;
   UWord lastEnd=0, lastStart=0;

   if(ivu->iv_magic!=IV_UNION_MAGIC) return False;

   VG_(initIterFM)(ivu->collect);
   while(VG_(nextIterFM)(ivu->collect,&key,(UWord*)&ivc)) {
      if(ivc->start>=ivc->end) {
         //if(1) VG_(printf)("ivUnion_isSane:1\n");
         return False;
      }
      if(key==ivc->start) {
         if(lastEnd) {
            if(ivc->start<=lastEnd) {
               //if(1) VG_(printf)("ivUnion_isSane:2\n");
               return False;
            }
         }
         lastStart=ivc->start;
      }else if(key==ivc->end) {
         if(lastStart) {
            if(ivc->start!=lastStart) {
               //if(1) VG_(printf)("ivUnion_isSane:3 lastStart=%lx != ivc->start=%lx\n",lastStart,ivc->start);
               return False;
            }
         }
         lastEnd=ivc->end;
      } else {
         //if(1) VG_(printf)("ivUnion_isSane:4\n");
         return False;
      }
   }
   VG_(doneIterFM)(ivu->collect);
   return True;
}


Bool HG_(ivUnion_isSane)(IntervalUnion * ivu)
{
   return ivUnion_isSane(ivu);
}

#define IS_SANE(__ivu) ({Bool __b=ivUnion_isSane(__ivu);if(!__b){ VG_(printf)("IS_SANE FAIL\n");HG_(ivUnionDump)(__ivu);} __b;})

IntervalUnion * HG_(newIvUnion)( HChar* cc )
{
   IntervalUnion * res=HG_(zalloc)(cc,sizeof(IntervalUnion));
   res->collect=VG_(newFM)( HG_(zalloc),
         cc,
         HG_(free),
         NULL );
   res->cc=cc;
   res->opaque=NULL;

#ifdef IV_UNION_MAGIC
   res->iv_magic=IV_UNION_MAGIC;
#endif

   tl_assert(ivUnion_isSane(res));
   return res;
}

void HG_(freeIvUnion) ( IntervalUnion * ivu )
{
   //tl_assert2(ivu->cc!=(void*)1,"Already freed !");
   Interval * ivc;
   UWord key;
   iv_magic_assert(ivu);

   VG_(initIterFM)(ivu->collect);
   while(VG_(nextIterFM)(ivu->collect,&key,(UWord*)&ivc)) {
      if(key==ivc->start)
         HG_(free)(ivc);
   }
   VG_(doneIterFM)(ivu->collect);

   VG_(deleteFM)(ivu->collect,NULL,NULL);
   ivu->collect=NULL;
   ivu->cc=NULL;//(void*)1;
   HG_(free)(ivu);
}

Int HG_(ivUnion_cntParts)(IntervalUnion * ivu)
{
   iv_magic_assert(ivu);
   return VG_(sizeFM)(ivu->collect);
}

void HG_(initIterIvu) ( IntervalUnion * ivu )
{
   iv_magic_assert(ivu);
   VG_(initIterFM)(ivu->collect);
}

Interval * HG_(nextIterIvu) ( IntervalUnion * ivu )
{
   Interval * ivc=NULL;
   UWord key;
   while(VG_(nextIterFM)(ivu->collect,&key,(UWord*)&ivc)&&ivc->start!=key);
   return ivc;
}

void HG_(doneIterIvu) ( IntervalUnion * ivu )
{
   VG_(doneIterFM)(ivu->collect);
   iv_magic_assert(ivu);
}

/**
 * return True if something changed (ie, not completely included)
 */
Bool HG_(ivUnionAddInterval)(IntervalUnion * ivu, HWord start, HWord end, void*opaque)
{
   //VG_(findBoundsFM)(ivu->collect,)
   Interval * ivc;
   Int i;
   Bool isIncluded=False;
   XArray * tmpXa=VG_(newXA) ( HG_(zalloc),
                               MALLOC_CC(addInterval),
                               HG_(free),
                               sizeof(Interval*) );
   Interval * iv = HG_(zalloc)(ivu->cc,sizeof(*iv));
   iv->start=start;
   iv->end=end;
   iv->opaque=opaque;

   if(0) tl_assert(ivUnion_isSane(ivu));

   VG_(initIterAtFM)(ivu->collect,iv->start);
   while(VG_(nextIterFM)(ivu->collect,NULL,(UWord*)&ivc)) {
      Bool intersect=False;

      //if(1) VG_(printf)("ivUnionAddInterval: testing[%lx,%lx]\n",ivc->start,ivc->end);
      if(ivc->start>=iv->start&&ivc->start<=iv->end)
      {
         intersect=True;
      }
      if(ivc->end>=iv->start&&ivc->end<=iv->end)
      {
         intersect=True;
      }
      if(iv->end<=ivc->end&&iv->start>=ivc->start) {
         /* new interval completely included */
         joinInterval(ivu,ivc,iv,True);
         HG_(free)(iv);
         isIncluded=True;
         break;
      }
      if(intersect) {
         //if(1) VG_(printf)("ivUnionAddInterval: intersecting[%lx,%lx]\n",ivc->start,ivc->end);
         VG_(addToXA)(tmpXa,&ivc);
      }

      if(iv->end<ivc->start) break;
   }
   VG_(doneIterFM)(ivu->collect);

   if(!isIncluded) {
      for(i=0;i<VG_(sizeXA)(tmpXa);i++) {
         ivc=*((Interval**)VG_(indexXA)(tmpXa,i));

         if(VG_(delFromFM)(ivu->collect,NULL,NULL,(UWord)ivc->start)) {
            joinInterval(ivu,iv,ivc,False);

            VG_(delFromFM)(ivu->collect,NULL,NULL,(UWord)ivc->end);
            HG_(free)(ivc);
         }
      }
      VG_(addToFM)(ivu->collect,(UWord)iv->start,(UWord)iv);
      VG_(addToFM)(ivu->collect,(UWord)iv->end,(UWord)iv);
   }

   VG_(deleteXA)(tmpXa);

   tl_assert2(IS_SANE(ivu),"Adding %lx,%lx\n",start,end);
   //VG_(addToFM)(ivu->collect,(UWord)iv,0);
   return !(isIncluded);
}

Interval * HG_(ivUnionContains)(IntervalUnion * ivu1, HWord value)
{
   Interval * ivc;
   Interval * res=NULL;
   iv_magic_assert(ivu1);
   VG_(initIterAtFM)(ivu1->collect,value);
   while(VG_(nextIterFM)(ivu1->collect,NULL,(UWord*)&ivc)) {
      Bool intersect=False;
      if(0) VG_(printf)("ivUnionContains(%p,0): testing[%lx,%lx] for %lx\n",ivu1,ivc->start,ivc->end, value);
      if((UWord)ivc->start>(UWord)value)
      {
         res=NULL;
         break;
      }
      if(ivc->start<=value&&ivc->end>value) {
         res=ivc;
         break;
      }
   }
   if(0) VG_(printf)("ivUnionContains(%p,1): return %p\n",ivu1,res);
   VG_(doneIterFM)(ivu1->collect);
   tl_assert(ivUnion_isSane(ivu1));
   return res;
}

void HG_(ivUnionDump_ext)(IntervalUnion * ivu1, Bool withFnName)
{

   static UChar buf_fnname[4096];

   Interval * ivc;
   UWord key;
   UWord lastEnd=0;
   tl_assert(ivu1!=NULL);
   if(ivu1->iv_magic!=IV_UNION_MAGIC) {
      VG_(printf)("ivu1->iv_magic=%lx\n",ivu1->iv_magic);
      tl_assert(0);
   }
   tl_assert(HG_(ivUnion_isSane)(ivu1));

#ifndef NDEBUG
   VG_(printf)("H%lx",HG_(ivUnionHash)(ivu1));
#endif

   VG_(printf)("#%d",VG_(sizeFM)(ivu1->collect));

   VG_(initIterFM)(ivu1->collect);
   while(VG_(nextIterFM)(ivu1->collect,&key,(UWord*)&ivc)) {
      if(key==ivc->start) {
         if(lastEnd) {
            VG_(printf)("-(%d)-",ivc->start-lastEnd);
         }
         if(withFnName) {
            VG_(printf)("[%lx{%s};",ivc->start,VG_(get_fnname_w_offset) ( ivc->start, buf_fnname, sizeof(buf_fnname) )?buf_fnname:"?");
            VG_(printf)("%lx{%s} (%d)]",ivc->end,
                  VG_(get_fnname_w_offset) ( ivc->end, buf_fnname, sizeof(buf_fnname) )?buf_fnname:"?",
                        ivc->end-ivc->start);
            //

         }else VG_(printf)("[%lx;%lx (%d)]",ivc->start,ivc->end,ivc->end-ivc->start);
         lastEnd=ivc->end;
      }
   }
   VG_(printf)("\n");
   VG_(doneIterFM)(ivu1->collect);

   //tl_assert(ivUnion_isSane(ivu));

}

void HG_(ivUnionDump)(IntervalUnion * ivu1)
{
   HG_(ivUnionDump_ext)(ivu1,False);
}


void HG_(ivUnionAddIvUnion)(IntervalUnion * ivuDest, IntervalUnion * ivuSrc)
{
   Interval * ivc;
   UWord key;

   iv_magic_assert(ivuDest);
   iv_magic_assert(ivuSrc);

   VG_(initIterFM)(ivuSrc->collect);
   while(VG_(nextIterFM)(ivuSrc->collect,&key,(UWord*)&ivc)) {
      if(key==ivc->start)
         HG_(ivUnionAddInterval)(ivuDest,ivc->start,ivc->end,ivc->opaque);
   }
   VG_(doneIterFM)(ivuSrc->collect);

   tl_assert(ivUnion_isSane(ivuDest));
}

/**
 * Give some ordering over IntervalUnions
 */
Int HG_(ivUnionCompare)(IntervalUnion * ivu1, IntervalUnion * ivu2)
{
   Interval * ivc1, *ivc2;
   UWord key1, key2;
   Int tmp;

   //VG_(printf)("ivUnionCompare:ivu1=%p, ivu2=%p\n",ivu1,ivu2);
   iv_magic_assert(ivu1);
   iv_magic_assert(ivu2);
   if(ivu1==ivu2) return 0;
   tmp=VG_(sizeFM)(ivu1->collect)-VG_(sizeFM)(ivu2->collect);
   if(tmp!=0) {
      return tmp;
   }
   VG_(initIterFM)(ivu1->collect);
   VG_(initIterFM)(ivu2->collect);
   while(VG_(nextIterFM)(ivu1->collect,&key1,(UWord*)&ivc1)) {
      tl_assert(VG_(nextIterFM)(ivu2->collect,&key2,(UWord*)&ivc2));
      if(key1==ivc1->start) {
         tl_assert(key2==ivc2->start);// by construction : start, end, start, end...
         if(key1!=key2) {
            return key1-key2;
         }
         if(ivc1->end!=ivc2->end) {
            return ivc1->end-ivc2->end;
         }
      }
   }
   VG_(doneIterFM)(ivu1->collect);
   VG_(doneIterFM)(ivu2->collect);

   return 0;
}

/**
 * Return NULL if no intersection
 */
IntervalUnion * HG_(ivUnionIntersect)(IntervalUnion * ivu1, IntervalUnion * ivu2)
{
   Interval * ivc1, *ivc2;
   IntervalUnion*res;
   UWord key1, key2;
   Int tmp,sz1,sz2;
   tl_assert(ivu1);tl_assert(ivu2);
   iv_magic_assert(ivu1);iv_magic_assert(ivu2);

   sz1=VG_(sizeFM)(ivu1->collect); sz2=VG_(sizeFM)(ivu2->collect);
   if(sz1==0||sz2==0) {
      return NULL;
   }
   res=HG_(newIvUnion)(MALLOC_CC(HG_(ivUnionIntersect)));
   if(ivu1==ivu2) {
      HG_(ivUnionAddIvUnion)(res,ivu2);
      tl_assert(ivUnion_isSane(ivu2));
      return res;
   }
   VG_(initIterFM)(ivu1->collect);
   VG_(initIterFM)(ivu2->collect);
   tl_assert(VG_(nextIterFM)(ivu1->collect,&key1,(UWord*)&ivc1));
   tl_assert(VG_(nextIterFM)(ivu2->collect,&key2,(UWord*)&ivc2));
   while(1) {
      if(key1==ivc1->start) {
         tl_assert(key2==ivc2->start);// by construction : start, end, start, end...
         if(((key1<=key2)&&(key2<ivc1->end)) || ((key2<=key1)&&(key1<ivc2->end))) {
            HG_(ivUnionAddInterval)(res,ivc2->start,ivc2->end,ivc2->opaque);
         }
      }
      if(key1>ivc2->end) {
         tl_assert(VG_(nextIterFM)(ivu2->collect,&key2,(UWord*)&ivc2));//end
         if(!VG_(nextIterFM)(ivu2->collect,&key2,(UWord*)&ivc2)) break;
      } else if(key2>ivc1->end) {
         tl_assert(VG_(nextIterFM)(ivu1->collect,&key1,(UWord*)&ivc1));
         if(!VG_(nextIterFM)(ivu1->collect,&key1,(UWord*)&ivc1)) break;
      }
   }
   VG_(doneIterFM)(ivu1->collect);
   VG_(doneIterFM)(ivu2->collect);

   tl_assert(ivUnion_isSane(ivu1));
   tl_assert(ivUnion_isSane(ivu2));
   return res;
}


UWord HG_(ivUnionHash)(IntervalUnion * ivu1)
{
   Interval * ivc;
   UWord key;
   UWord hash=0;
   iv_magic_assert(ivu1);

   VG_(initIterFM)(ivu1->collect);
   while(VG_(nextIterFM)(ivu1->collect,&key,(UWord*)&ivc)) {
      if(key==ivc->start) {
         hash+=(ivc->start<<1)^ivc->end;
      }
   }
   VG_(doneIterFM)(ivu1->collect);
   return hash;
}


void HG_(ivUnionSetOpaque)(IntervalUnion * ivu1, void*opaque)
{
   ivu1->opaque=opaque;
}
void * HG_(ivUnionGetOpaque)(IntervalUnion * ivu1)
{
   return ivu1->opaque;
}
