#define HG_LOGGING_C_

#include "hg_logging.h"

#include "pub_tool_libcprint.h"
#include "pub_tool_libcfile.h"
#include <fcntl.h> // Constants

static Char * memFilter[]={
      "libhb.*",
      /*"libhb.zsm_init.1",
      "libhb.vts_tab_init.1",
      "libhb.vts_set_init.1",
      "libhb.event_map_init.1",*/

      // static :
      "mark_spin_reads_SCH.allreadyTestedIFS",

      // ????
      "hg.lsd.mutex_create.1",
      "hg.mpttT.1",
      // Accepted :
      "mk.ThrLoopExtends.1",
      // probably OK :
      "hg.mk_Thread.1",
      "hg.lsd.create_ThrLSD.*",
      // ... malloc
      "hg.new_MallocMeta.1",

      // ????
      "hg.mk_Lock.*",
      "hg.ids.*",
      "hg.loops.init.*",
      "mk.SB.*",

      /*end*/ NULL};

static Word cmp_unsigned_Words ( UWord w1, UWord w2 );

/*-----------------------------------------------------*/
/*--- Utils                                            */
/*-----------------------------------------------------*/

#include "pub_tool_mallocfree.h"

static
void* lg_zalloc ( HChar* cc, SizeT n )
{
   void* p;
   tl_assert(n > 0);
   if(0)VG_(printf)("lg_zalloc %s %d\n",cc,(Int)n);
   p = VG_(malloc)( cc, n );
   if(0)VG_(printf)("lg_zalloc %s OK\n",cc);
   tl_assert(p);
   VG_(memset)(p, 0, n);
   return p;
}

static
void lg_free ( void* p )
{
   tl_assert(p);
   VG_(free)(p);
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

/*-----------------------------------------------------*/
/*--- Sockets                                          */
/*-----------------------------------------------------*/

Int VG_(fcntl) ( Int fd, Int cmd, Int arg );
Int VG_(connect_via_socket)( UChar* str );

static Int loggingSock=-31415;

static
Int _getLoggingSock(void)
{
   if(loggingSock==-31415) {
      Int mode;
      loggingSock=VG_(connect_via_socket)("127.0.0.1:3048");
      mode = VG_(fcntl)(loggingSock, F_GETFL, 0);
      mode |= O_NONBLOCK;
      VG_(fcntl)(loggingSock, F_SETFL, mode);
   }
   return loggingSock;
}

static
Int sockReadNoBlock(void);
static
Int sockWrite(const Char * message, Int count)
{
   _getLoggingSock();
   if(loggingSock<0) return loggingSock;
   sockReadNoBlock();
   return VG_(write)( loggingSock, message, count );
}

static
Int sockReadNoBlock(void)
{
   static Char buf[1024];
   Int red;
   _getLoggingSock();
   red=VG_(read) ( loggingSock, buf, sizeof(buf)-1);
   if(red<0) {
      buf[0]=0;
      return red;
   }
   buf[red]='\0';
   VG_(printf)("sockReadNoBlock : %s\n",buf);
   return red;
}


/*-----------------------------------------------------*/
/*--- Getting info to code address                     */
/*-----------------------------------------------------*/

dfileInfo_t HG_(getFile)( Addr addr ) {
    static UChar buf_fname[4096];
    static UChar buf_dname[4096];
    static dfileInfo_t res;
    Bool dirname_available;
    //UInt lineno;
    res.dname=buf_dname;
    res.fname=buf_fname;
    if(VG_(get_filename_linenum) ( addr,
          buf_fname, sizeof(buf_fname),
          buf_dname, sizeof(buf_dname),
          &dirname_available,
          &res.line)) {
       if(!dirname_available) res.dname[0]=0;
       res.ok=True;
    } else {
       res.ok=False;
    }
    return res;
}

Bool HG_(filterFile)( Addr addr , UChar * dir, UChar * file, UInt line) {
    static UChar buf_fname[4096];
    static UChar buf_dname[4096];
    Bool dirname_available;
    UInt lineno;
    //Bool
    Bool res = True;
    if(VG_(get_filename_linenum) ( addr,
          buf_fname, sizeof(buf_fname),
          buf_dname, sizeof(buf_dname),
          &dirname_available,
          &lineno)) {

       if(dir && dirname_available )
          if(!VG_(string_match)(dir, buf_dname)) res=False;
       if(file)
          if(!VG_(string_match)(file, buf_fname)) res=False;
       if(line) if(lineno!=line) res=False;
       return res;
    }
    return False;
}

UChar * HG_(filterFunction)( Addr addr , UChar * func) {
   static UChar buf_fnname[4096];
   if(VG_(get_fnname_w_offset) ( addr, buf_fnname, sizeof(buf_fnname) )) {
       //VG_(printf)("In function %s\n", buf_fnname);
      if(VG_(string_match)(func, buf_fnname)) return buf_fnname;
   }
   return NULL;
}

static
Addr getNextEBP(void) {
   Addr ebp;
   // push ebp
   // mov esp ebp
   asm(
      "mov    %%ebp,%0\n"
      :"=m"(ebp)       /* input */
      :
   );
   //esp-=sizeof(Addr)*2; // push ebp && call
   return ebp;
}

Char* VG_(describe_IP)(Addr eip, Char* buf, Int n_buf);


Addr * HG_(getEipStack)(Addr * eipStack,Int start, Int count)
{
   Addr ebp;
   Addr * ebpP;
   Addr eip;
   Int i;

   if(sizeof(HWord)!=4) { // only compatible to x86
	   eipStack[0]=0;
	   return eipStack;
   }
   ebp=getNextEBP();

   for(i=0;i<start+count-1;i++) {
      ebpP=(Addr*)ebp;
      //VG_(printf)("%d:%p {%p,%p ; %p,%p,%p}\n",i, ebp,ebpP[-2],ebpP[-1],ebpP[0],ebpP[1],ebpP[2]);
      //if(!ebpP[0]) break;
      ebp=ebpP[0];
      //VG_(printf)("%d / ebp=%p, eip=%p | %p\n",i,(void*)ebp,(void*)eip,ebpP);
      if(ebp>((Addr)ebpP)+0x80000) break;
      if(ebp<((Addr)ebpP)-0x80000) break;
      eip=ebpP[1];
      if(!(ebp&&eip)) break;
      if(i>=start) {
         eipStack[i-start]=eip;
      }
   }
   if(i-start>=0) eipStack[i-start]=0;
   return eipStack;
}

void HG_(myPPstackTrace)(Addr * eipStack,const Char * prefix)
{
   Addr eip;
   Int i;
   static UChar buf_fnname[4096];

   for(i=0;eipStack[i];i++) {
      eip=eipStack[i];
      buf_fnname[0]=0;
      VG_(describe_IP)(eip, buf_fnname, sizeof(buf_fnname));
      if(prefix) VG_(printf)("%s",prefix);
      VG_(printf)("%d : %s",i,buf_fnname);

      /*if(VG_(get_fnname_w_offset) (eip, buf_fnname, sizeof(buf_fnname))) {
         VG_(printf)(" in func %s",buf_fnname);
      } else if(VG_(get_fnname) (eip, buf_fnname, sizeof(buf_fnname))) {
         VG_(printf)(" in func %s",buf_fnname);
      }*/
      VG_(printf)("\n");
   }
}

/*-----------------------------------------------------*/
/*--- Special printf's                                 */
/*-----------------------------------------------------*/


extern Int   VG_(clo_log_fd);

void HG_(setLogFile)(const Char * fileName)
{
   //static const Int FD_NONE=-31415;
   SysRes res;
   //static Int defaultFd=FD_NONE;
   static Int lastFd[10];
   static Int fdId=-1;

   static const Char * oldFName=NULL;
   UWord flags=O_WRONLY|O_APPEND;

   if(!(fileName) || fileName[0]=='\0') {
      fileName=NULL;
   }

   tl_assert(fdId>=-1&&fdId<10);

   if(!fileName) {
      if(fdId==-1) return;
      if(lastFd[fdId]>0) {
         VG_(close)( VG_(clo_log_fd) ); // CLOSE

         VG_(clo_log_fd)=lastFd[fdId];
      }
      fdId--;
      return;
   }

   /*if(!VG_(access)(fileName,False,True,False)) {
      VG_(printf)(AT " USE CREATE\n",flags);
      flags|=O_CREAT;
   }*/
   res=VG_(open)(fileName, flags,00666);
   if(res.isError&&!(flags&O_CREAT)) {
      flags|=O_CREAT;
      res=VG_(open)(fileName, flags,00666);
   }

   if(!res.isError) {
      ++fdId;
      lastFd[fdId]=VG_(clo_log_fd);
      VG_(clo_log_fd)=res.res;
      oldFName=fileName;
   } else {
      VG_(printf)(__AT__ "ERROR, can't open file %s with flags %x\n",fileName,(Int)flags);
      { // keep same dest
         ++fdId;
         lastFd[fdId]=-1;
      }
      //tl_assert(0);
   }
   //O_CREAT
}



/*-----------------------------------------------------*/
/*--- 'Memcheck' of the tool                           */
/*-----------------------------------------------------*/


static struct {
   SizeT sumAlloc;
   Int cntAlloc;
   WordFM * allocMap;
} mallocStats={0,0,NULL};

#define EIP_STACK_SIZE 10
typedef struct {
   SizeT n;
   //HChar cc[128];
   HChar * cc;
   void*p;
   //HChar * lastAt;
   Addr eipStack[EIP_STACK_SIZE];
} MallocEntry;

/*
HG_(DebugMallocFunc) HG_(debugMalloc_SetAt)( HChar * at )
{
   mallocStats.mallocAT=at;
   return HG_(debugMalloc);
}*/

struct HG_(s_DebugMalloc_cfg) HG_(DebugMalloc_cfg)={
      True, //doGC_stat
      True // doDetail
};

void* HG_(debugPostMalloc) ( void* p, HChar* cc, SizeT n )
{
   //  cat /proc/meminfo
   MallocEntry * m;
   Int allocSize;

   if(0&&PRINT_MEMLEAK)VG_(printf)("debugPostMalloc %s %d...\n",cc,(Int)n);

   if(!mallocStats.allocMap) {
      mallocStats.allocMap=VG_(newFM) ( lg_zalloc,
            MALLOC_CC(HG_(debugPostMalloc)+mallocStats.allocMap),
            lg_free,
            cmp_unsigned_Words );
   }

   allocSize=sizeof(*m)+(VG_(strlen)(cc)+2);
   m=lg_zalloc(MALLOC_CC(HG_(debugPostMalloc)+MallocEntry+m),allocSize);
   m->n=n;
   m->p=p;
   //m->lastAt=mallocStats.mallocAT;
   //VG_(strncpy)(m->cc,cc,sizeof(m->cc));
   //m->cc=cc;
   m->cc=&(((Char*)m)[sizeof(*m)]);
   VG_(strcpy)(m->cc,cc);


   if(0&&PRINT_MEMLEAK) {
      HG_(getEipStack)(m->eipStack,2, 4+1);
      VG_(printf)("Stack in malloc - %d alloced in %d blocks\n",(Int)mallocStats.sumAlloc, (Int)mallocStats.cntAlloc);
      HG_(myPPstackTrace)(m->eipStack,"");
   }
   //sockWrite("Malloc\n",7);

   //m->eipStack[0]=0;
   HG_(getEipStack)(m->eipStack,2/*don't put HG_(debugPostMalloc) neither caller in it*/, EIP_STACK_SIZE);

   VG_(addToFM) ( mallocStats.allocMap, (UWord)p, (UWord)m );

   mallocStats.sumAlloc+=n;
   mallocStats.cntAlloc++;

   if(0&&PRINT_MEMLEAK)VG_(printf)("debugPostMalloc ok - total=%d bytes\n",(Int)mallocStats.sumAlloc);

#ifdef HG_MAX_NON_FREED_MEMORY
   if(mallocStats.sumAlloc>HG_MAX_NON_FREED_MEMORY) {
      HG_(logging_ppMallocStatistics)();
      tl_assert2(0,"Memory limit broken (cur-cc=%s, n=%d)\n",cc,n);
   } /*else if(mallocStats.sumAlloc>HG_MAX_NON_FREED_MEMORY/2 && VG_(string_match)("libhb.event_map_init.3 (OldRef groups)", cc)) {
	      HG_(logging_ppMallocStatistics)();
	      tl_assert2(0,"Memory limit ALMOST broken (cur-cc=%s, n=%d)\n",cc,n);
	   }*/
#endif
   return p;
}

UInt HG_(dprintf)(Int fd, const HChar * format, ...)
{
   /*printf_buf *prbuf;
   ret = VG_(debugLog_vprintf)
            ( add_to_myprintf_buf, prbuf, format, vargs );*/
   //VG_(fprintf)("");
   UInt ret;
   va_list vargs;
   static Char buf[1024*4];

   va_start(vargs, format);
   ret = VG_(vsnprintf)(buf, sizeof(buf), format, vargs);
   va_end(vargs);

   ret = VG_(write)(fd,buf,ret);

   return ret;
}

void HG_(debugPreFree) ( void* p )
{
   MallocEntry * m;

   if(VG_(delFromFM) ( mallocStats.allocMap, NULL, (UWord*)&m, (UWord)p )) {

      if(0)VG_(printf)("debugPreFree %s %d...\n",m->cc,(Int)m->n);

      tl_assert(m->p==p);
      mallocStats.sumAlloc-=m->n;
      mallocStats.cntAlloc--;

      VG_(memset)(p,0xED,m->n); // erase it (don't write 0 as it may be assumed as a NULL pointer)

      lg_free(m);

      if(0)VG_(printf)("debugPreFree OK\n");
   } else {
      tl_assert2(0,"Freeing non allocated data\n");
   }
}

static
Bool filterMallocEntry(MallocEntry * m)
{
   Int i;
   for(i=0;memFilter[i];i++) {
      //VG_(printf)("filterMallocEntry ");VG_(printf)("%s\n",memFilter[i]);
      //VG_(strcmp)(m->cc,memFilter[i])==0
      if(//(VG_(strcmp)(m->cc,memFilter[i])==0) ||
            VG_(string_match)(memFilter[i], m->cc)) return True;
   }
   return False;
}

static
void ppMallocEntry(MallocEntry * m)
{
   //if(m->lastAt)VG_(printf)("%s\t",m->lastAt);
   VG_(printf)("%s : pointer %p of size %d",m->cc, m->p,(Int)m->n);
}

static
void ppMallocEntryStack(MallocEntry * m) {
   HG_(myPPstackTrace)(m->eipStack,"\t\t");
}

/**
 * return -1 if not accessible, offset (in bytes) else
 * @complexity : blockSize*sizeof(void*)
 */
static
Int garbageCollectIsAccessible(void*p, void*block, SizeT blockSize)
{
   void**pBlock;
   Int maxI=blockSize/sizeof(void*);
   Int i,align;
   for(align=0;align<sizeof(void*);align++) {
      if(align==1) maxI--;

      pBlock=(void**)(((Int)block)+align);
      for(i=0;i<maxI;i++) {
         if(pBlock[i]==p) return ((Int)&(pBlock[i]))-(Int)block;
      }
   }
   return -1;
}

/**
 * Complexity : VG_(sizeFM)( mallocStats.allocMap ) * mallocStats.sumAlloc
 */
static
void printGraphRoots(Bool printReachable)
{
   void * p;
   MallocEntry * m;
   Int sumAccessible=0,sumInAccessible=0;
   Int cntAccessible=0,cntInAccessible=0;
   if(!mallocStats.allocMap) return;

   {
      Int numPointers=VG_(sizeFM)(mallocStats.allocMap);
      struct {
         MallocEntry * m;
         MallocEntry * reachableFrom;
         Int reachOffset;
      } * pointers;
      Int curPId=0,i;
      pointers=lg_zalloc(MALLOC_CC(printGraphRoots+pointers),numPointers*sizeof(*pointers));
      /**
       * Copy map 'mallocStats.allocMap' into table 'pointers'
       */
      VG_(initIterFM) ( mallocStats.allocMap );
      while(VG_(nextIterFM) ( mallocStats.allocMap, (UWord*)&p, (UWord*)&m)) {
         pointers[curPId].m=m;
         pointers[curPId].reachableFrom=NULL;
         //VG_(printf)("curPId= %d/%d\n",curPId,numPointers);
         curPId++;
      }
      VG_(doneIterFM) ( mallocStats.allocMap );
      for(curPId=0;curPId<numPointers;curPId++) {
         for(i=0;i<numPointers;i++) {
            Int access;
            if(i==curPId) continue;
            access=garbageCollectIsAccessible(pointers[curPId].m->p,pointers[i].m->p,pointers[i].m->n);
            if(access>0) {
               pointers[curPId].reachableFrom=pointers[i].m;
               pointers[curPId].reachOffset=access;
               break;
            }
         }
      }
      // Print still reachable :
      if(printReachable) VG_(printf)("Malloc graph still reachable :\n");
      for(curPId=0;curPId<numPointers;curPId++) {
         if(pointers[curPId].reachableFrom) {
            if(printReachable) {
               ppMallocEntry(pointers[curPId].m);
               VG_(printf)(" # still reachable\n");
               ppMallocEntryStack(pointers[curPId].m);
               VG_(printf)("\tREACHABLE FROM:[[");
               ppMallocEntry(pointers[curPId].reachableFrom);
               VG_(printf)("]] + %d\n",pointers[curPId].reachOffset);
            }
            sumAccessible+=pointers[curPId].m->n;
            cntAccessible++;
         }
      }
      // Print no reachable :
      VG_(printf)("Malloc graph not reachable :\n");
      for(curPId=0;curPId<numPointers;curPId++) {
         if(!pointers[curPId].reachableFrom) {
            ppMallocEntry(pointers[curPId].m);VG_(printf)(" # not reachable\n");
            ppMallocEntryStack(pointers[curPId].m);
            sumInAccessible+=pointers[curPId].m->n;
            cntInAccessible++;
         }
      }
      lg_free(pointers); //----
   }
   VG_(printf)("GC : %d bytes accessible in %d blocks, \n"
          "\t%d bytes not accessible in %d blocks\n"
          "\tTotal=%d non freed bytes\n",
         sumAccessible,cntAccessible,
         sumInAccessible,cntInAccessible,
         sumAccessible+sumInAccessible);
}


#if 0
#include "pub_core_threadstate.h"
void stackTrace()
{
   Int BACKTRACE_DEPTH=10;
   Addr stacktop;
   Addr ips[BACKTRACE_DEPTH];
   Addr ip=0, sp=0, fp=0, lr=0;
   ThreadState *tst
      = VG_(get_ThreadState)( VG_(lwpid_to_vgtid)( VG_(gettid)() ) );

   // If necessary, fake up an ExeContext which is of our actual real CPU
   // state.  Could cause problems if we got the panic/exception within the
   // execontext/stack dump/symtab code.  But it's better than nothing.
   if (0 == ip && 0 == sp && 0 == fp) {
       GET_REAL_PC_SP_AND_FP(ip, sp, fp);
   }

   stacktop = tst->os_state.valgrind_stack_init_SP;

   VG_(get_StackTrace_wrk)(
      0/*tid is unknown*/,
      ips, BACKTRACE_DEPTH,
      NULL/*array to dump SP values in*/,
      NULL/*array to dump FP values in*/,
      ip, sp, fp, lr, sp, stacktop
   );
   VG_(pp_StackTrace)  (ips, BACKTRACE_DEPTH);
}
#include "pub_tool_stacktrace.h"
#include "pub_tool_execontext.h"
//VG_(pp_ExeContext)(VG_(record_ExeContext)(0, 0));
#endif



void HG_(logging_ppMallocStatistics)(void)
{
   Long gcComplexity;

   if(!mallocStats.allocMap) return;

   VG_(printf)("Malloc statistic : Sum non freed = %d bytes in %d blocks\n",(Int)mallocStats.sumAlloc,mallocStats.cntAlloc);

   gcComplexity = ((Long)mallocStats.cntAlloc) * mallocStats.sumAlloc;
   if(HG_(DebugMalloc_cfg).doDetail) {
      if(!(HG_(DebugMalloc_cfg).doGC_stat) || (gcComplexity > 1000ul*(1024*1024*8))) {
         // Too much for garbage collect algo
         // -> he will take a bit too long
         void * p;
         MallocEntry * m;
         if(mallocStats.allocMap) {
        	WordFM* groupEntries, *sortEntries;
        	UWord k,v;
        	groupEntries=VG_(newFM)(lg_zalloc,MALLOC_CC(groupEntries),lg_free,VG_(strcmp));
        	sortEntries=VG_(newFM)(lg_zalloc,MALLOC_CC(sortEntries),lg_free,NULL);

            VG_(initIterFM) ( mallocStats.allocMap );
            while(VG_(nextIterFM) ( mallocStats.allocMap, (UWord*)&p, (UWord*)&m)) {
                {
             	   UWord val=0;
             	   if(VG_(lookupFM)(groupEntries,NULL,&val,(UWord*)m->cc)) {
             	   }
             	   val+=m->n;
             	   VG_(addToFM)(groupEntries,(UWord*)m->cc,val);
                }
               if(filterMallocEntry(m)) continue;
               ppMallocEntry(m);VG_(printf)("\n");
               ppMallocEntryStack(m);
            }
            VG_(doneIterFM) ( mallocStats.allocMap );


            VG_(initIterFM) ( groupEntries );
            while(VG_(nextIterFM) ( groupEntries, (UWord*)&k, (UWord*)&v)) {
               VG_(addToFM)(sortEntries,v,k);
            }
            VG_(doneIterFM) ( groupEntries );

            VG_(initIterFM) ( sortEntries );
            while(VG_(nextIterFM) ( sortEntries, (UWord*)&k, (UWord*)&v)) {
               VG_(printf)("%s got %d bytes\n",(HChar*)v,k);
            }
            VG_(doneIterFM) ( sortEntries );
         }
      } else {
         VG_(printf)("Malloc graph-roots (GC) :\n");
         printGraphRoots(True);
      }
   }
}



/*-----------------------------------------------------*/
/*--- printf                                    */
/*-----------------------------------------------------*/

#undef vgPlain_printf
UInt HG_(L_printf) ( const HChar *filePos, const HChar *format, ... );
UInt HG_(L_printf) ( const HChar *filePos, const HChar *format, ... )
{
   static HChar newFmt[4096];
   static WordFM * lastPrintFunc=NULL;
   static Bool lastWasNL=True;
   UInt ret;
   va_list vargs;
   Int i,j,k;
   UChar * curFunction;
   Bool printCurFunc=False;

   if(1&&lastWasNL){
      Addr eipStack[10];
      Int eipId;
      static UChar buf_fnname[4096];
      if(!lastPrintFunc) {
         //VG_(printf)("newFM");
         lastPrintFunc=VG_(newFM) ( lg_zalloc,
               MALLOC_CC(HG_(L_printf)+lastPrintFunc),
               lg_free,
               (void*)VG_(strcmp) );
         //VG_(printf)(" newFM OK\n");
      }
      //VG_(printf)(" STRT : \n");
      HG_(getEipStack)(eipStack,1, 10);
      for(eipId=0;eipStack[eipId];eipId++) {
         if(VG_(get_fnname) (eipStack[eipId], buf_fnname, sizeof(buf_fnname))) {
            SizeT len=VG_(strlen)(buf_fnname);
            Addr lEip;

            if(VG_(strcmp)("space",buf_fnname)==0) continue;
            //VG_(printf)("lookupFM",str);
            if(!VG_(lookupFM) ( lastPrintFunc, (UWord*)&curFunction, (UWord*)&lEip, (UWord)buf_fnname )) {
               curFunction=lg_zalloc("HG_(L_printf).str",len+1);
               //VG_(printf)("strcpy",str);
               VG_(strcpy)(curFunction,buf_fnname);
               lEip=eipStack[eipId]+1;
            }
            VG_(addToFM)(lastPrintFunc,(UWord)curFunction,eipStack[eipId]);
            if(1||lEip>=eipStack[eipId]) {
               printCurFunc=True;
            }
            break;
         }
      }
   }

   if(printCurFunc) {
      VG_(printf)("[In %s]:",curFunction);
   }

   for(i=0,j=0;format[i];i++) {
      newFmt[j]=format[i];j++;
      if(format[i]=='\n') {
         //i==0 && lastWasNL
         static Int maxSumWritten=0;
         Int sumWritten=0;
         if(0) {
            for(k=0;filePos[k];k++) {
               newFmt[j]=filePos[k];j++;
               sumWritten++;
            }
         }
         if(printCurFunc&&format[i+1]) {
            for(k=0;curFunction[k];k++) {
               newFmt[j]=curFunction[k];j++;
               sumWritten++;
            }
         }
         // Align everything
         if(sumWritten>maxSumWritten) {
            maxSumWritten=sumWritten;
         } else {
            for(;sumWritten<maxSumWritten;) {
               newFmt[j]=' ';j++;
               sumWritten++;
            }
         }
      }
   }
   newFmt[j]='\0';

   //VG_(strncpy)(newFmt,format,sizeof(newFmt));
   lastWasNL=(newFmt[VG_(strlen)(newFmt)-1]=='\n');

   va_start(vargs, format);
   ret = VG_(vprintf)(newFmt, vargs);
   va_end(vargs);

   return ret;
}


//////////////////////////////////////////

static WordFM * debugConf=NULL;

void HG_(debugConfAdd)(const Char * key, const Char * value)
{
   if(!debugConf) {
      debugConf=VG_(newFM) ( lg_zalloc,
            MALLOC_CC(HG_(debugConfAdd)+debugConf),
            lg_free,
            (void*)VG_(strcmp) );
   }
   VG_(addToFM)(debugConf, (UWord)key, (UWord)value);
}

Char * HG_(debugConfGet)(const Char * key)
{
   Char * value=NULL;
   if(!debugConf) return NULL;
   if(VG_(lookupFM)(debugConf, NULL, (UWord*)&value,(UWord)key)) {
      return value;
   }
   return NULL;
}
