/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS SOURCE IS GOVERNED BY *
 * THE GNU LESSER/LIBRARY PUBLIC LICENSE, WHICH IS INCLUDED WITH    *
 * THIS SOURCE. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.        *
 *                                                                  *
 * THE OggVorbis SOURCE CODE IS (C) COPYRIGHT 1994-2000             *
 * by Monty <monty@xiph.org> and the XIPHOPHORUS Company            *
 * http://www.xiph.org/                                             *
 *                                                                  *
 ********************************************************************

 function: #ifdef jail to whip a few platforms into the UNIX ideal.
 last mod: $Id: os_types.h,v 1.4 2001/01/22 00:56:30 xiphmont Exp $

 ********************************************************************/
#ifndef _OS_TYPES_H
#define _OS_TYPES_H

/* make it easy on the folks that want to compile the libs with a
   different malloc than stdlib */
#define _ogg_malloc  malloc
#define _ogg_calloc  calloc
#define _ogg_realloc realloc
#define _ogg_free    free

#ifdef _WIN32 
#  ifndef __GNUC__

/* MSVC/Borland */
typedef __int64 ogg_int64_t;
typedef __int32 ogg_int32_t;
typedef unsigned __int32 ogg_uint32_t;
typedef __int16 ogg_int16_t;

#  else

/* Cygwin */
#include <_G_config.h>
typedef _G_int64_t ogg_int64_t;
typedef _G_int32_t ogg_int32_t;
typedef _G_uint32_t ogg_uint32_t;
typedef _G_int16_t ogg_int16_t;

#  endif
#else


#  ifdef macintosh

#include <sys/types.h>

typedef SInt16 ogg_int16_t;
typedef SInt32 ogg_int32_t;
typedef UInt32 ogg_uint32_t;
typedef SInt64 ogg_int64_t;

#  else

#    ifdef __BEOS__

/* Be */
#include <inttypes.h>

#    endif

#include <sys/types.h>
#include <ogg/config_types.h>

#  endif /* macintosh */

#endif  /* _WIN32 */

#endif  /* _OS_TYPES_H */
