/*
 * hg_annotations.h
 *
 *  Created on: 26 Jun, 2009
 *      Author: gupta
 */

#ifndef HG_ANNOTATIONS_H_
#define HG_ANNOTATIONS_H_

#include "pub_tool_basics.h"
#include "valgrind.h"
#include "helgrind.h"
#include <stdio.h>
#include <assert.h>

// Do a client request.  This is a macro rather than a function 
// so as to avoid having an extra function in the stack trace.
// TODO: merge with hg_intercepts.c

#define DO_CREQ_v_W(_creqF, _ty1F,_arg1F)                \
   do {                                                  \
      Word _unused_res, _arg1;                           \
      assert(sizeof(_ty1F) == sizeof(Word));             \
      _arg1 = (Word)(_arg1F);                            \
      VALGRIND_DO_CLIENT_REQUEST(_unused_res, 0,         \
                                 (_creqF),               \
                                 _arg1, 0,0,0,0);        \
   } while (0)

#define DO_CREQ_v_WW(_creqF, _ty1F,_arg1F, _ty2F,_arg2F) \
   do {                                                  \
      Word _unused_res, _arg1, _arg2;                    \
      assert(sizeof(_ty1F) == sizeof(Word));             \
      assert(sizeof(_ty2F) == sizeof(Word));             \
      _arg1 = (Word)(_arg1F);                            \
      _arg2 = (Word)(_arg2F);                            \
      VALGRIND_DO_CLIENT_REQUEST(_unused_res, 0,         \
                                 (_creqF),               \
                                 _arg1,_arg2,0,0,0);     \
   } while (0)

#define DO_CREQ_v_WWW(_creqF, _ty1F,_arg1F,              \
		      _ty2F,_arg2F, _ty3F, _arg3F)       \
   do {                                                  \
      Word _unused_res, _arg1, _arg2, _arg3;             \
      assert(sizeof(_ty1F) == sizeof(Word));             \
      assert(sizeof(_ty2F) == sizeof(Word));             \
      assert(sizeof(_ty3F) == sizeof(Word));             \
      _arg1 = (Word)(_arg1F);                            \
      _arg2 = (Word)(_arg2F);                            \
      _arg3 = (Word)(_arg3F);                            \
      VALGRIND_DO_CLIENT_REQUEST(_unused_res, 0,         \
                                 (_creqF),               \
                                 _arg1,_arg2,_arg3,0,0); \
   } while (0)

#define DO_CREQ_v_WWWW(_creqF, _ty1F,_arg1F, _ty2F,_arg2F,\
		      _ty3F,_arg3F, _ty4F, _arg4F)       \
   do {                                                  \
      Word _unused_res, _arg1, _arg2, _arg3, _arg4;      \
      assert(sizeof(_ty1F) == sizeof(Word));             \
      assert(sizeof(_ty2F) == sizeof(Word));             \
      assert(sizeof(_ty3F) == sizeof(Word));             \
      assert(sizeof(_ty4F) == sizeof(Word));             \
      _arg1 = (Word)(_arg1F);                            \
      _arg2 = (Word)(_arg2F);                            \
      _arg3 = (Word)(_arg3F);                            \
      _arg4 = (Word)(_arg4F);                            \
      VALGRIND_DO_CLIENT_REQUEST(_unused_res, 0,         \
                              (_creqF),                  \
                             _arg1,_arg2,_arg3,_arg4,0); \
   } while (0)


#endif /* HG_ANNOTATIONS_H_ */
