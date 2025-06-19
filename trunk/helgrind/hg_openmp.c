/*
 * hg_openmp.c
 *
 *  Created on: 25.05.2009
 */

#include "pub_tool_basics.h"
#include "pub_tool_redir.h"
#include "valgrind.h"
#include "helgrind.h"

#define TRACE_OMP_FNS 1

/*----------------------------------------------------------------*/
/*---                                                          ---*/
/*----------------------------------------------------------------*/

/* Needed for older glibcs (2.3 and older, at least) who don't
   otherwise "know" about pthread_rwlock_anything or about
   PTHREAD_MUTEX_RECURSIVE (amongst things). */
#define _GNU_SOURCE 1

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include "openmp/libgomp_g.h"
/* Include omp.h by parts.  */
#include "openmp/omp-lock.h"
#define _LIBGOMP_OMP_LOCK_DEFINED 1
#include "openmp/omp.h.in"


#define OMP_FUNC(ret_ty, f, args...) \
   ret_ty VG_REPLACE_FUNCTION_ZZ(libgompZdsoZd1,f)(args); \
   ret_ty VG_REPLACE_FUNCTION_ZZ(libgompZdsoZd1,f)(args)

#define OMP_FUNC_W_v( _function_name, _function_name_ZU ) \
   OMP_FUNC( int, _function_name_ZU, void) { \
	  int result; \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      result=hg_##_function_name(); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
      return result;\
   }

#define OMP_FUNC_d_v( _function_name, _function_name_ZU ) \
   OMP_FUNC( double, _function_name_ZU, void) { \
	  double result; \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      result=hg_##_function_name(); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
      return result;\
   }

#define OMP_FUNC_v_W( _function_name, _function_name_ZU ) \
   OMP_FUNC( void, _function_name_ZU, int a ) { \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      hg_##_function_name(a); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
   }

#define OMP_FUNC_v_aptr( _function_name, _function_name_ZU ) \
   OMP_FUNC( void, _function_name_ZU, omp_lock_t *a ) { \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      hg_##_function_name( a ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
   }

#define OMP_FUNC_v_naptr( _function_name, _function_name_ZU ) \
   OMP_FUNC( void, _function_name_ZU, omp_nest_lock_t *a ) { \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      hg_##_function_name( a ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
   }

#define OMP_FUNC_W_aptr( _function_name, _function_name_ZU ) \
   OMP_FUNC( int, _function_name_ZU, omp_lock_t *a ) { \
	  int result; \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
     result= hg_##_function_name( a ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
   return result;\
}

#define OMP_FUNC_W_naptr( _function_name, _function_name_ZU ) \
   OMP_FUNC( int, _function_name_ZU, omp_nest_lock_t *a ) { \
	  int result; \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
     result= hg_##_function_name( a ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
   return result;\
}

#define OMP_FUNC_v_v( _function_name, _function_name_ZU ) \
   OMP_FUNC( void, _function_name_ZU, void ) { \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      hg##_function_name(); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
   }

#define OMP_FUNC_v_a( _function_name, _function_name_ZU, _arg1_type, _arg1_value ) \
   OMP_FUNC( void, _function_name_ZU, _arg1_type _arg1_value ) { \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      hg##_function_name( _arg1_value ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
   }

#define OMP_FUNC_v_aa( _function_name, _function_name_ZU,\
      _arg1_type, _arg1_value, \
      _arg2_type, _arg2_value ) \
   OMP_FUNC( void, _function_name_ZU, \
         _arg1_type _arg1_value, \
         _arg2_type _arg2_value ) { \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      hg##_function_name( _arg1_value, _arg2_value ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
   }

#define OMP_FUNC_b_aa( _function_name, _function_name_ZU,\
      _arg1_type, _arg1_value, \
      _arg2_type, _arg2_value ) \
   OMP_FUNC( bool, _function_name_ZU, \
         _arg1_type _arg1_value, \
         _arg2_type _arg2_value ) { \
      bool result; \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      result = hg##_function_name( _arg1_value, _arg2_value ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
      return result;\
   }

#define OMP_FUNC_v_aaaaa( _function_name, _function_name_ZU,\
      _arg1_type, _arg1_value, \
      _arg2_type, _arg2_value, \
      _arg3_type, _arg3_value, \
      _arg4_type, _arg4_value, \
      _arg5_type, _arg5_value ) \
   OMP_FUNC( void, _function_name_ZU, \
         _arg1_type _arg1_value, \
         _arg2_type _arg2_value, \
         _arg3_type _arg3_value, \
         _arg4_type _arg4_value, \
         _arg5_type _arg5_value ) { \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      hg##_function_name( _arg1_value, _arg2_value, \
         _arg3_value, _arg4_value, \
         _arg5_value ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
   }

#define OMP_FUNC_v_aaaaaa( _function_name, _function_name_ZU,\
      _arg1_type, _arg1_value, \
      _arg2_type, _arg2_value, \
      _arg3_type, _arg3_value, \
      _arg4_type, _arg4_value, \
      _arg5_type, _arg5_value, \
      _arg6_type, _arg6_value ) \
   OMP_FUNC( void, _function_name_ZU, \
         _arg1_type _arg1_value, \
         _arg2_type _arg2_value, \
         _arg3_type _arg3_value, \
         _arg4_type _arg4_value, \
         _arg5_type _arg5_value, \
         _arg6_type _arg6_value ) { \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      hg##_function_name( _arg1_value, _arg2_value, \
         _arg3_value, _arg4_value, \
         _arg5_value, _arg6_value ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
   }

#define OMP_FUNC_v_faaaaa( _function_name, _function_name_ZU,\
      _arg1_type, _arg1_value, \
      _arg2_type, _arg2_value, \
      _arg3_type, _arg3_value, \
      _arg4_type, _arg4_value, \
      _arg5_type, _arg5_value ) \
   OMP_FUNC( void, _function_name_ZU, \
         void (*__fn) (void *), \
         _arg1_type _arg1_value, \
         _arg2_type _arg2_value, \
         _arg3_type _arg3_value, \
         _arg4_type _arg4_value, \
         _arg5_type _arg5_value ) { \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      hg##_function_name( __fn, _arg1_value, _arg2_value, \
         _arg3_value, _arg4_value, \
         _arg5_value ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
   }

#define OMP_FUNC_v_faaaaaa( _function_name, _function_name_ZU,\
      _arg1_type, _arg1_value, \
      _arg2_type, _arg2_value, \
      _arg3_type, _arg3_value, \
      _arg4_type, _arg4_value, \
      _arg5_type, _arg5_value, \
      _arg6_type, _arg6_value ) \
   OMP_FUNC( void, _function_name_ZU, \
         void (*__fn) (void *), \
         _arg1_type _arg1_value, \
         _arg2_type _arg2_value, \
         _arg3_type _arg3_value, \
         _arg4_type _arg4_value, \
         _arg5_type _arg5_value, \
         _arg6_type _arg6_value ) { \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      hg##_function_name( __fn, _arg1_value, _arg2_value, \
         _arg3_value, _arg4_value, \
         _arg5_value, _arg6_value ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
   }

#define OMP_FUNC_b_aaaaaa( _function_name, _function_name_ZU,\
      _arg1_type, _arg1_value, \
      _arg2_type, _arg2_value, \
      _arg3_type, _arg3_value, \
      _arg4_type, _arg4_value, \
      _arg5_type, _arg5_value, \
      _arg6_type, _arg6_value ) \
   OMP_FUNC( bool, _function_name_ZU, \
         _arg1_type _arg1_value, \
         _arg2_type _arg2_value, \
         _arg3_type _arg3_value, \
         _arg4_type _arg4_value, \
         _arg5_type _arg5_value, \
         _arg6_type _arg6_value ) { \
      bool result; \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      result = hg##_function_name( _arg1_value, _arg2_value, \
         _arg3_value, _arg4_value, \
         _arg5_value, _arg6_value ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
      return result;\
   }

#define OMP_FUNC_b_aaaaaaa( _function_name, _function_name_ZU,\
      _arg1_type, _arg1_value, \
      _arg2_type, _arg2_value, \
      _arg3_type, _arg3_value, \
      _arg4_type, _arg4_value, \
      _arg5_type, _arg5_value, \
      _arg6_type, _arg6_value, \
      _arg7_type, _arg7_value ) \
   OMP_FUNC( bool, _function_name_ZU, \
         _arg1_type _arg1_value, \
         _arg2_type _arg2_value, \
         _arg3_type _arg3_value, \
         _arg4_type _arg4_value, \
         _arg5_type _arg5_value, \
         _arg6_type _arg6_value, \
         _arg7_type _arg7_value ) { \
      bool result; \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, "<< " #_function_name ); fflush(stderr); \
      } \
      result = hg##_function_name( _arg1_value, _arg2_value, \
         _arg3_value, _arg4_value, \
         _arg5_value, _arg6_value, _arg7_value ); \
      if (TRACE_OMP_FNS) { \
         fprintf(stderr, " :: " #_function_name " >>\n"); \
      } \
      return result;\
   }

/*----------------------------------------------------------------*/
/*--- barrier.c                                                ---*/
/*----------------------------------------------------------------*/

// GOMP_barrier
OMP_FUNC_v_v( GOMP_barrier, GOMPZubarrier );

/*----------------------------------------------------------------*/
/*--- critical.c                                               ---*/
/*----------------------------------------------------------------*/

OMP_FUNC_v_v( GOMP_critical_start, GOMPZucriticalZustart );
OMP_FUNC_v_v( GOMP_critical_end, GOMPZucriticalZuend ); 
OMP_FUNC_v_a( GOMP_critical_name_start, GOMPZucriticalZunameZustart, void **, pptr );
OMP_FUNC_v_a( GOMP_critical_name_end, GOMPZucriticalZunameZuend, void **, pptr );
OMP_FUNC_v_v( GOMP_atomic_start, GOMPZuatomicZustart );
OMP_FUNC_v_v( GOMP_atomic_end, GOMPZuatomicZuend ); 

/*----------------------------------------------------------------*/
/*--- loop.c                                                   ---*/
/*----------------------------------------------------------------*/

OMP_FUNC_v_aaaaaa( GOMP_loop_static_start, GOMPZuloopZustaticZustart,
      long, a1, long, a2, long, a3, long, a4, long *, a5, long *, a6 );
OMP_FUNC_v_aaaaaa( GOMP_loop_dynamic_start, GOMPZuloopZudynamicZustart,
      long, a1, long, a2, long, a3, long, a4, long *, a5, long *, a6 );
OMP_FUNC_v_aaaaaa( GOMP_loop_guided_start, GOMPZuloopZuguidedZustart,
      long, a1, long, a2, long, a3, long, a4, long *, a5, long *, a6 );
OMP_FUNC_v_aaaaa( GOMP_loop_runtime_start, GOMPZuloopZuruntimeZustart,
      long, a1, long, a2, long, a3, long *, a5, long *, a6 );

OMP_FUNC_v_aaaaaa( GOMP_loop_ordered_static_start, GOMPZuloopZuorderedZustaticZustart,
      long, a1, long, a2, long, a3, long, a4, long *, a5, long *, a6 );
OMP_FUNC_v_aaaaaa( GOMP_loop_ordered_dynamic_start, GOMPZuloopZuorderedZudynamicZustart,
      long, a1, long, a2, long, a3, long, a4, long *, a5, long *, a6 );
OMP_FUNC_v_aaaaaa( GOMP_loop_ordered_guided_start, GOMPZuloopZuorderedZuguidedZustart,
      long, a1, long, a2, long, a3, long, a4, long *, a5, long *, a6 );
OMP_FUNC_v_aaaaa( GOMP_loop_ordered_runtime_start, GOMPZuloopZuorderedZuruntimeZustart,
      long, a1, long, a2, long, a3, long *, a5, long *, a6 );

OMP_FUNC_v_aa( GOMP_loop_static_next, GOMPZuloopZustaticZunext,
      long *, a1, long *, a2 );
OMP_FUNC_v_aa( GOMP_loop_dynamic_next, GOMPZuloopZudynamicZunext,
      long *, a1, long *, a2 );
OMP_FUNC_v_aa( GOMP_loop_guided_next, GOMPZuloopZuguidedZunext,
      long *, a1, long *, a2 );
OMP_FUNC_v_aa( GOMP_loop_runtime_next, GOMPZuloopZuruntimeZunext,
      long *, a1, long *, a2 );

OMP_FUNC_v_aa( GOMP_loop_ordered_static_next, GOMPZuloopZuorderedZustaticZunext,
      long *, a1, long *, a2 );
OMP_FUNC_v_aa( GOMP_loop_ordered_dynamic_next, GOMPZuloopZuorderedZudynamicZunext,
      long *, a1, long *, a2 );
OMP_FUNC_v_aa( GOMP_loop_ordered_guided_next, GOMPZuloopZuorderedZuguidedZunext,
      long *, a1, long *, a2 );
OMP_FUNC_v_aa( GOMP_loop_ordered_runtime_next, GOMPZuloopZuorderedZuruntimeZunext,
      long *, a1, long *, a2 );

OMP_FUNC_v_faaaaaa( GOMP_parallel_loop_static_start, GOMPZuparallelZuloopZustaticZustart,
      void *, a1, unsigned, a2, long, a3, long, a4, long, a5, long, a6 );
OMP_FUNC_v_faaaaaa( GOMP_parallel_loop_dynamic_start, GOMPZuparallelZuloopZudynamicZustart,
      void *, a1, unsigned, a2, long, a3, long, a4, long, a5, long, a6 );
OMP_FUNC_v_faaaaaa( GOMP_parallel_loop_guided_start, GOMPZuparallelZuloopZuguidedZustart,
      void *, a1, unsigned, a2, long, a3, long, a4, long, a5, long, a6 );
OMP_FUNC_v_faaaaa( GOMP_parallel_loop_runtime_start, GOMPZuparallelZuloopZuruntimeZustart,
      void *, a1, unsigned, a2, long, a3, long, a4, long, a5 );

OMP_FUNC_v_v( GOMP_loop_end, GOMPZuloopZuend );
OMP_FUNC_v_v( GOMP_loop_end_nowait, GOMPZuloopZuendZunowait );

/*----------------------------------------------------------------*/
/*--- loop_ull.c                                                   ---*/
/*----------------------------------------------------------------*/

OMP_FUNC_b_aaaaaaa( GOMP_loop_ull_static_start, GOMPZuloopZuullZustaticZustart,
      bool, a1,
      unsigned long long, a2,
      unsigned long long, a3,
      unsigned long long, a4,
      unsigned long long, a5,
      unsigned long long *, a6,
      unsigned long long *, a7 );
OMP_FUNC_b_aaaaaaa( GOMP_loop_ull_dynamic_start, GOMPZuloopZuullZudynamicZustart,
      bool, a1,
      unsigned long long, a2,
      unsigned long long, a3,
      unsigned long long, a4,
      unsigned long long, a5,
      unsigned long long *, a6,
      unsigned long long *, a7 );
OMP_FUNC_b_aaaaaaa( GOMP_loop_ull_guided_start, GOMPZuloopZuullZuguidedZustart,
      bool, a1,
      unsigned long long, a2,
      unsigned long long, a3,
      unsigned long long, a4,
      unsigned long long, a5,
      unsigned long long *, a6,
      unsigned long long *, a7 );
OMP_FUNC_b_aaaaaa( GOMP_loop_ull_runtime_start, GOMPZuloopZuullZuruntimeZustart,
      bool, a1,
      unsigned long long, a2,
      unsigned long long, a3,
      unsigned long long, a4,
      unsigned long long *, a5,
      unsigned long long *, a6 );

OMP_FUNC_b_aaaaaaa( GOMP_loop_ull_ordered_static_start, GOMPZuloopZuullZuorderedZustaticZustart,
      bool, a1,
      unsigned long long, a2,
      unsigned long long, a3,
      unsigned long long, a4,
      unsigned long long, a5,
      unsigned long long *, a6,
      unsigned long long *, a7 );
OMP_FUNC_b_aaaaaaa( GOMP_loop_ull_ordered_dynamic_start, GOMPZuloopZuullZuorderedZudynamicZustart,
      bool, a1,
      unsigned long long, a2,
      unsigned long long, a3,
      unsigned long long, a4,
      unsigned long long, a5,
      unsigned long long *, a6,
      unsigned long long *, a7 );
OMP_FUNC_b_aaaaaaa( GOMP_loop_ull_ordered_guided_start, GOMPZuloopZuullZuorderedZuguidedZustart,
      bool, a1,
      unsigned long long, a2,
      unsigned long long, a3,
      unsigned long long, a4,
      unsigned long long, a5,
      unsigned long long *, a6,
      unsigned long long *, a7 );
OMP_FUNC_b_aaaaaa( GOMP_loop_ull_ordered_runtime_start, GOMPZuloopZuullZuorderedZuruntimeZustart,
      bool, a1,
      unsigned long long, a2,
      unsigned long long, a3,
      unsigned long long, a4,
      unsigned long long *, a5,
      unsigned long long *, a6 );

OMP_FUNC_b_aa( GOMP_loop_ull_static_next, GOMPZuloopZuullZustaticZunext,
      unsigned long long *, a1, unsigned long long *, a2 );
OMP_FUNC_b_aa( GOMP_loop_ull_dynamic_next, GOMPZuloopZuullZudynamicZunext,
      unsigned long long *, a1, unsigned long long *, a2 );
OMP_FUNC_b_aa( GOMP_loop_ull_guided_next, GOMPZuloopZuullZuguidedZunext,
      unsigned long long *, a1, unsigned long long *, a2 );
OMP_FUNC_b_aa( GOMP_loop_ull_runtime_next, GOMPZuloopZuullZuruntimeZunext,
      unsigned long long *, a1, unsigned long long *, a2 );

OMP_FUNC_b_aa( GOMP_loop_ull_ordered_static_next, GOMPZuloopZuullZuorderedZustaticZunext,
      unsigned long long *, a1, unsigned long long *, a2 );
OMP_FUNC_b_aa( GOMP_loop_ull_ordered_dynamic_next, GOMPZuloopZuullZuorderedZudynamicZunext,
      unsigned long long *, a1, unsigned long long *, a2 );
OMP_FUNC_b_aa( GOMP_loop_ull_ordered_guided_next, GOMPZuloopZuullZuorderedZuguidedZunext,
      unsigned long long *, a1, unsigned long long *, a2 );
OMP_FUNC_b_aa( GOMP_loop_ull_ordered_runtime_next, GOMPZuloopZuullZuorderedZuruntimeZunext,
      unsigned long long *, a1, unsigned long long *, a2 );

/*----------------------------------------------------------------*/
/*--- ordered.c                                                ---*/
/*----------------------------------------------------------------*/

OMP_FUNC_v_v( GOMP_ordered_start, GOMPZuorderedZustart );
OMP_FUNC_v_v( GOMP_ordered_end, GOMPZuorderedZuend );

/*----------------------------------------------------------------*/
/*--- parallel.c                                               ---*/
/*----------------------------------------------------------------*/

// GOMP_parallel_start
OMP_FUNC(void, GOMPZuparallelZustart, // GOMP_parallel_start@*
         void (*func) (void *), void *data,
         unsigned num_threads)
{
   if (TRACE_OMP_FNS) {
      fprintf(stderr, "<< GOMP_parallel_start"); fflush(stderr);
   }

   hgGOMP_parallel_start( func, data, num_threads );

   if (TRACE_OMP_FNS) {
      fprintf(stderr, " :: parallel_start >>\n");
   }
}

// GOMP_parallel_end
OMP_FUNC(void, GOMPZuparallelZuend, void) // GOMP_parallel_end
{
   if (TRACE_OMP_FNS) {
      fprintf(stderr, "<< GOMP_parallel_end"); fflush(stderr);
   }

   hgGOMP_parallel_end();

   if (TRACE_OMP_FNS) {
      fprintf(stderr, " :: parallel_end >>\n");
   }
}

/*----------------------------------------------------------------*/
/*--- team.c                                                   ---*/
/*----------------------------------------------------------------*/

// GOMP_task
OMP_FUNC(void, GOMPZutask,
            void (*func) (void *), void *data, void (*cpyfunc) (void *, void *),
            long arg_size, long arg_align, bool if_clause,
            unsigned flags __attribute__((unused)))
{
   if (TRACE_OMP_FNS) {
      fprintf(stderr, "<< GOMP_task"); fflush(stderr);
   }

   hgGOMP_task( func, data, cpyfunc, arg_size, arg_align, if_clause, flags );

   if (TRACE_OMP_FNS) {
      fprintf(stderr, " :: GOMP_task >>\n");
   }
}

OMP_FUNC_v_v(GOMP_taskwait, hgGOMPZutaskwait);

/*----------------------------------------------------------------*/
/*--- sections.c                                               ---*/
/*----------------------------------------------------------------*/

// GOMP_sections_start
OMP_FUNC( unsigned, GOMPZusectionsZustart, unsigned u )
{
   unsigned result;
   
   if (TRACE_OMP_FNS) {
      fprintf(stderr, "<< GOMP_sections_start"); fflush(stderr);
   }

   result = hgGOMP_sections_start(u);

   if (TRACE_OMP_FNS) {
      fprintf(stderr, " :: GOMP_sections_start >>\n");
   }
   
   return result;
}

// GOMP_sections_next
OMP_FUNC( unsigned, GOMPZusectionsZunext, void )
{
   unsigned result;
   
   if (TRACE_OMP_FNS) {
      fprintf(stderr, "<< GOMP_sections_next"); fflush(stderr);
   }

   result = hgGOMP_sections_next();

   if (TRACE_OMP_FNS) {
      fprintf(stderr, " :: GOMP_sections_next >>\n");
   }
   
   return result;
}

// GOMP_parallel_sections_start
OMP_FUNC( void, GOMPZuparallelZusectionsZustart,
            void (*func) (void *), void *data,
            unsigned num_threads, unsigned count )
{
   if (TRACE_OMP_FNS) {
      fprintf(stderr, "<< GOMP_parallel_sections_start"); fflush(stderr);
   }

   hgGOMP_parallel_sections_start( func, data, num_threads, count );

   if (TRACE_OMP_FNS) {
      fprintf(stderr, " :: GOMP_parallel_sections_start >>\n");
   }
}

OMP_FUNC_v_v( GOMP_sections_end, GOMPZusectionsZuend );
OMP_FUNC_v_v( GOMP_sections_end_nowait, GOMPZusectionsZuendZunowait );

/*----------------------------------------------------------------*/
/*--- single.c                                                 ---*/
/*----------------------------------------------------------------*/

// GOMP_single_start
OMP_FUNC( bool, GOMPZusingleZustart, void )
{
   bool result;
   
   if (TRACE_OMP_FNS) {
      fprintf(stderr, "<< GOMP_single_start"); fflush(stderr);
   }

   result = hgGOMP_single_start();

   if (TRACE_OMP_FNS) {
      fprintf(stderr, " :: GOMP_single_start >>\n");
   }
   
   return result;
}

// GOMP_single_copy_start
OMP_FUNC( void*, GOMPZusingleZucopyZustart, void )
{
   void* result;
   
   if (TRACE_OMP_FNS) {
      fprintf(stderr, "<< GOMP_single_copy_start"); fflush(stderr);
   }

   result = hgGOMP_single_copy_start();

   if (TRACE_OMP_FNS) {
      fprintf(stderr, " :: GOMP_single_copy_start >>\n");
   }
   
   return result;
}

// GOMP_single_copy_end
OMP_FUNC( void, GOMPZusingleZucopyZuend, void* pptr )
{
   if (TRACE_OMP_FNS) {
      fprintf(stderr, "<< GOMP_single_copy_end"); fflush(stderr);
   }

   hgGOMP_single_copy_end( pptr );

   if (TRACE_OMP_FNS) {
      fprintf(stderr, " :: GOMP_single_copy_end >>\n");
   }
}

/*----------------------------------------------------------------*/
/*--- omp.h.in                                                    ---*/
/*----------------------------------------------------------------*/

//omp_set_num_threads 
OMP_FUNC_v_W(omp_set_num_threads,ompZusetZunumZuthreads);

//omp_get_num_threads 
OMP_FUNC_W_v(omp_get_num_threads,ompZugetZunumZuthreads);

//omp_get_max_threads 
OMP_FUNC_W_v(omp_get_max_threads,ompZugetZumaxZuthreads);

//omp_get_thread_num
OMP_FUNC_W_v(omp_get_thread_num,ompZugetZuthreadZunum);

//omp_get_num_procs
OMP_FUNC_W_v(omp_get_num_procs,ompZugetZunumZuprocs);

//omp_in_parallel
OMP_FUNC_W_v(omp_in_parallel,ompZuinZuparallel);

//omp_set_dynamic
OMP_FUNC_v_W(omp_set_dynamic,ompZusetZudynamic);

//omp_get_dynamic
OMP_FUNC_W_v(omp_get_dynamic,ompZugetZudynamic);

//omp_set_nested
OMP_FUNC_v_W(omp_set_nested,ompZusetZunested);

//omp_get_nested
OMP_FUNC_W_v(omp_get_nested,ompZugetZunested);

//omp_init_lock
OMP_FUNC_v_aptr(omp_init_lock,ompZuinitZulock);

//omp_destroy_lock
OMP_FUNC_v_aptr(omp_destroy_lock,ompZudestroyZulock);

//omp_set_lock
OMP_FUNC_v_aptr(omp_set_lock,ompZusetZulock);

//omp_unset_lock
OMP_FUNC_v_aptr(omp_unset_lock,ompZuunsetZulock);

//omp_init_nest_lock
OMP_FUNC_v_naptr(omp_init_nest_lock,ompZuinitZunestZulock);

//omp_destroy_nest_lock
OMP_FUNC_v_naptr(omp_destroy_nest_lock,ompZudestroyZunestZulock);

//omp_set_nest_lock
OMP_FUNC_v_naptr(omp_set_nest_lock,ompZusetZunestZulock);

//omp_unset_nest_lock
OMP_FUNC_v_naptr(omp_unset_nest_lock,ompZuunsetZunestZulock);

//omp_test_lock
OMP_FUNC_W_aptr(omp_test_lock,ompZutestZulock);

//omp_test_nest_lock
OMP_FUNC_W_naptr(omp_test_nest_lock,ompZutestZunestZulock);

//omp_get_wtime
OMP_FUNC_d_v(omp_get_wtime,ompZugetZuwtime);

//omp_get_wtick
OMP_FUNC_d_v(omp_get_wtick,ompZugetZuwtick);
