
#ifndef HG_LOGGING_H_
#define HG_LOGGING_H_

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

/*-----------------------------------------------------*/
/*--- Usefull macros                                   */
/*-----------------------------------------------------*/

//  ---------------
//#define NDEBUG

#define COMMITABLE 1

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define __AT__ __FILE__ ":" TOSTRING(__LINE__)

#define IFNULL(a,b) ((a)?(a):(b))


void HG_(debugConfAdd)(const Char * key, const Char * value);
Char * HG_(debugConfGet)(const Char * key);


/*-----------------------------------------------------*/
/*--- Configure printout level                         */
/*-----------------------------------------------------*/

#if !defined(NDEBUG) && 0
   #define DEBUG_PRINT_FILE(x) VG_(printf)("#DEBUG# "__AT__ " " STRINGIFY(x) "\n");
   #define DEBUG_VG_PRINTF(func,level,format, args...) VG_(printf)("#DEBUG# "STRINGIFY(func)" " format "\n", ##args);
#else
   #define DEBUG_PRINT_FILE(x)
   #define DEBUG_VG_PRINTF(func,level,format, args...)
#endif

#define __DEBUG_CONF(name) HG_(VGAPPEND(__DEBUG_CONF_,name))
#ifdef HG_LOGGING_C_
   #define DEBUG_CONF_INIT(name) Char * __DEBUG_CONF(name)=(Char*)1;
#else
   #define DEBUG_CONF_INIT(name) extern Char * __DEBUG_CONF(name);
#endif
#define DEBUG_CONF(name) (__DEBUG_CONF(name)!=((Char*)1)?__DEBUG_CONF(name):(__DEBUG_CONF(name)=HG_(debugConfGet)(STRINGIFY(name))))
#define DEBUG_CONFD(name,defaultVal) (DEBUG_CONF(name)?__DEBUG_CONF(name):(defaultVal))

DEBUG_CONF_INIT(useOrigAlgo);
DEBUG_CONF_INIT(logFile);
DEBUG_CONF_INIT(locksetEv);
DEBUG_CONF_INIT(locksetLog);

#if !defined(NDEBUG) && 1
//DEBUG_CONF(logFile)
   #define DO_LOG DEBUG_CONF(logFile)
   #define NDO_LOG 0
  //("")
   #define LOG_FILE (DEBUG_CONF(logFile))
#else
   #define DO_LOG 0
   #define NDO_LOG 0
   #define LOG_FILE ((void*)0)
#endif

// PUBLISHED
#define PRINT_ENTERING_MY_ALGO 0

#define PRINT_LOGGING_HG_LOOPS LOG_FILE
#define PRINT_mark_spin_reads_SCH_TREE_BEFORE PRINT_LOGGING_HG_LOOPS
#define PRINT_mark_spin_reads_SCH_COMMENTS PRINT_LOGGING_HG_LOOPS
#define PRINT_mark_spin_reads_SCH_TREE_AFTER PRINT_LOGGING_HG_LOOPS
#define PRINT_mark_spin_reads_STORE_TREES PRINT_LOGGING_HG_LOOPS

#define PRINT_handleHugeBlock LOG_FILE
#define PRINT_Concatenation LOG_FILE
#define PRINT_concat_irsb LOG_FILE

#define PRINT_spin_lockset_log ""
//#define PRINT_spin_lockset_log NULL

#if COMMITABLE
   #define PRINT_spin_lockset_log DEBUG_CONFD(locksetLog,0)
#endif

#define PRINT_spin_lockset_ev DEBUG_CONFD(locksetEv,0)

#define PRINT_spin_lockset_logLoops PRINT_spin_lockset_log
#define PRINT_spin_lockset_msm PRINT_spin_lockset_log
// TODO : del :
//#define PRINT_handleHugeBlock PRINT_spin_lockset_log
#define PRINT_spin_lockset_logLoops PRINT_spin_lockset_log

#define PRINT_getBaseSpin 0&&PRINT_spin_lockset_log


#define PRINT_isRecurse NDO_LOG

#define LOG_FILE_HG_DEPENDENCY LOG_FILE
#define PRINT_LOGGING_HG_DEPENDENCY LOG_FILE_HG_DEPENDENCY

#define PRINT_isConstTest DO_LOG
#define PRINT_simplifyExpression DO_LOG
#define PRINT_removeUselessRegisterLoops NDO_LOG


//#define PRINT_BB_OUT PRINT_spin_lockset_log // TODO !!!!!!!!
#define PRINT_BB_OUT DO_LOG

#define PRINT_SPIN_PROPERTY DO_LOG

#if COMMITABLE
   #define PRINT_graph NULL
#else
   #define PRINT_graph "dots.dot"
#endif

// --------------------

#if 1&&(!defined(NDEBUG) && !(COMMITABLE))
   #define soft_assert(expr) do { if(!(expr)) { VG_(printf)("soft_assert (%s) failed\n",STRINGIFY(expr)); DEBUG_PRINT_STACK(); } } while(0)
   #define assert(expr) tl_assert(expr)
#else
   #define soft_assert(expr)
   #define assert(expr)
#endif

#define DEBUG_PRINTF_E(COND,PRINTF_CODE)\
do {                                 \
   typeof (COND) __cond = (COND);    \
   if(__cond) {                                 \
      HG_(setLogFile)(__cond);                                 \
      PRINTF_CODE;                                 \
      HG_(setLogFile)(NULL);                                 \
   }                                 \
}while(0)

#define DEBUG_PRINTF(COND,fmt,args...) DEBUG_PRINTF_E((COND),({VG_(printf)(fmt,##args);}))



/*-----------------------------------------------------*/
/*--- Getting info to code address                     */
/*-----------------------------------------------------*/

typedef struct {
   UChar * fname;
   UChar * dname;
   UInt line;
   Bool ok;
} dfileInfo_t;

dfileInfo_t HG_(getFile)( Addr addr ) ;

#define HG_printfFile(addr) do{\
   dfileInfo_t __dfileInfo = HG_(getFile)(addr);\
   VG_(printf)("%s / %s @ %d\n",IFNULL(__dfileInfo.dname,""),IFNULL(__dfileInfo.fname,""),__dfileInfo.line);\
}while(0)

Bool HG_(filterFile)( Addr addr , UChar * dir, UChar * file, UInt line);

UChar * HG_(filterFunction)( Addr addr , UChar * func);

Addr * HG_(getEipStack)(Addr * eipStack,Int start, Int count);
void HG_(myPPstackTrace)(Addr * eipStack,const Char * prefix);

#define DEBUG_PRINT_STACK() do {\
   VG_(printf)("Stack at "__AT__" :\n"); {\
   Addr st[20];\
   HG_(myPPstackTrace)(HG_(getEipStack)(st,0,20),"  ");}\
}while(0)

/*-----------------------------------------------------*/
/*--- Special printf's                                 */
/*-----------------------------------------------------*/


void HG_(setLogFile)(const Char * fileName);

/**
 * Write to file descriptor
 */
extern UInt HG_(dprintf)(Int fd, const HChar * format, ...) PRINTF_CHECK(2, 0);

#define DB_PRINTF(format, args...) VG_(printf)(format, ##args)

/**
 * VG_(printf) with line where it was called
 */
#if !defined(NDEBUG) && 0
   UInt HG_(L_printf) ( const HChar *filePos, const HChar *format, ... );
   #define vgPlain_printf(format, args...) vgHelgrind_L_printf (__AT__ "\t", format , ## args)
#endif

/*-----------------------------------------------------*/
/*--- 'Memcheck' of the tool                           */
/*-----------------------------------------------------*/

/**
 * Make malloc statistics
 */
#if COMMITABLE
#define PRINT_MEMLEAK NULL
#else
#define PRINT_MEMLEAK "hg_memLeak.log"
#endif

extern struct HG_(s_DebugMalloc_cfg){
   Bool doGC_stat;
   Bool doDetail;
} HG_(DebugMalloc_cfg);

// use --profile-heap=yes

//#define SCHMALTZ_CORRECT_MEM_LEAKS 1

void HG_(logging_ppMallocStatistics)(void);

//typedef void* (*HG_(DebugMallocFunc)) ( HChar* cc, SizeT n );
void* HG_(debugPostMalloc) ( void* p, HChar* cc, SizeT n );
//HG_(DebugMallocFunc) HG_(debugMalloc_SetAt)( HChar * at );
void HG_(debugPreFree) ( void* p );

#if !defined(NDEBUG) && !(COMMITABLE)
   #define LOGGING_MALLOC
   // 200 mega
   #define HG_MAX_NON_FREED_MEMORY (1024*1024*2000)
#endif

#ifndef HG_ZALLOC
   #define HG_UNREGISTER_NOFREE
#endif




#endif /* HG_LOGGING_H_ */
